/*
 * JPEG 2000 decoder using NVIDIA nvJPEG2000 GPU library (dynamic loading)
 *
 * Copyright (c) 2024 mpv AC-4 Edition contributors
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------------------------------------------
 * Overview
 * ---------------------------------------------------------------------------
 * This decoder dynamically loads nvjpeg2k_0.dll (nvJPEG2000 library from
 * NVIDIA) and nvcuda.dll (CUDA Driver API, always present with any NVIDIA
 * driver) to accelerate JPEG 2000 decoding on NVIDIA GPUs.
 *
 * If either DLL is unavailable (no NVIDIA GPU, no driver, DLL not on PATH),
 * init() returns AVERROR_DECODER_NOT_FOUND and FFmpeg falls back to the
 * software JPEG 2000 decoder automatically.
 *
 * Color space handling:
 *   - DCI JPEG 2000 content is encoded in XYZ colour space (12-bit/component).
 *   - We output AV_PIX_FMT_XYZ12LE so that mpv / libplacebo handles the
 *     XYZ → display colour-space transformation in its shader pipeline.
 *   - For non-DCI content (RGB/YUV), we output AV_PIX_FMT_GBRP16LE (planar).
 *
 * Data flow:
 *   compressed J2K packet → nvjpeg2kStreamParse (CPU)
 *   → nvjpeg2kDecode (GPU, writes planar UINT16 to pinned host memory)
 *   → CPU repack loop → AVFrame
 *
 * Pinned host memory (cuMemAllocHost) is used so that nvjpeg2kDecode can
 * write directly to host-accessible buffers without a separate cudaMemcpy.
 * This relies on the CUDA unified virtual address space (available on all
 * CUDA-capable GPUs since Fermi).
 */

#include "config.h"

#ifdef _WIN32

#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "nvjpeg2k_loader.h"

/* ------------------------------------------------------------------ */
/* Decoder private context                                             */
/* ------------------------------------------------------------------ */

#define MAX_COMPONENTS 4

typedef struct NVJPEG2KContext {
    AVClass              *av_class;

    /* DLL handles */
    HMODULE               nvjpeg2k_dll;
    HMODULE               nvcuda_dll;

    /* nvJPEG2000 function pointers */
    NVJPEG2KFunctions     fn;

    /* nvJPEG2000 state */
    nvjpeg2kHandle_t      handle;
    nvjpeg2kDecodeState_t decode_state;
    nvjpeg2kStream_t      jpeg_stream;

    /* Pinned host buffers for each component (reused across frames) */
    void                 *host_buf[MAX_COMPONENTS];
    size_t                host_pitch[MAX_COMPONENTS];   /* bytes per row */
    size_t                host_buf_size[MAX_COMPONENTS];/* total bytes   */
    uint32_t              buf_width[MAX_COMPONENTS];
    uint32_t              buf_height[MAX_COMPONENTS];

    /* Last decoded image geometry */
    uint32_t              num_components;
    uint32_t              image_width;
    uint32_t              image_height;
} NVJPEG2KContext;

/* ------------------------------------------------------------------ */
/* Helpers: load a single function pointer from a DLL                  */
/* ------------------------------------------------------------------ */

static int load_sym(AVCodecContext *avctx, HMODULE dll,
                    const char *dll_name, const char *fn_name,
                    void **out)
{
    *out = (void *)GetProcAddress(dll, fn_name);
    if (!*out) {
        av_log(avctx, AV_LOG_WARNING,
               "nvjpeg2k: symbol %s not found in %s\n", fn_name, dll_name);
        return AVERROR(ENOSYS);
    }
    return 0;
}

#define LOAD_SYM(dll, dll_name, field, sym) \
    do { \
        void *_p; \
        if (load_sym(avctx, dll, dll_name, sym, &_p) < 0) return AVERROR(ENOSYS); \
        ctx->fn.field = (void *)_p; \
    } while (0)

/* ------------------------------------------------------------------ */
/* Free pinned host buffers                                            */
/* ------------------------------------------------------------------ */

static void free_host_buffers(NVJPEG2KContext *ctx)
{
    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (ctx->host_buf[i]) {
            if (ctx->fn.MemFreeHost)
                ctx->fn.MemFreeHost(ctx->host_buf[i]);
            ctx->host_buf[i]      = NULL;
            ctx->host_buf_size[i] = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* init / open                                                         */
/* ------------------------------------------------------------------ */

static av_cold int nvjpeg2k_decode_init(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;
    nvjpeg2kStatus_t st;

    /* --- Load nvjpeg2k_0.dll --- */
    ctx->nvjpeg2k_dll = LoadLibraryW(L"nvjpeg2k_0.dll");
    if (!ctx->nvjpeg2k_dll) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: nvjpeg2k_0.dll not found — "
               "falling back to software decoder\n");
        return AVERROR_DECODER_NOT_FOUND;
    }

    /* --- Load nvcuda.dll (CUDA Driver API) --- */
    ctx->nvcuda_dll = LoadLibraryW(L"nvcuda.dll");
    if (!ctx->nvcuda_dll) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: nvcuda.dll not found — "
               "falling back to software decoder\n");
        FreeLibrary(ctx->nvjpeg2k_dll);
        ctx->nvjpeg2k_dll = NULL;
        return AVERROR_DECODER_NOT_FOUND;
    }

    /* --- Resolve nvJPEG2000 symbols --- */
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", CreateSimple,
             "nvjpeg2kCreateSimple");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", Destroy,
             "nvjpeg2kDestroy");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", DecodeStateCreate,
             "nvjpeg2kDecodeStateCreate");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", DecodeStateDestroy,
             "nvjpeg2kDecodeStateDestroy");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamCreate,
             "nvjpeg2kStreamCreate");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamDestroy,
             "nvjpeg2kStreamDestroy");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamParse,
             "nvjpeg2kStreamParse");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamGetImageInfo,
             "nvjpeg2kStreamGetImageInfo");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", StreamGetImageComponentInfo,
             "nvjpeg2kStreamGetImageComponentInfo");
    LOAD_SYM(ctx->nvjpeg2k_dll, "nvjpeg2k_0.dll", Decode,
             "nvjpeg2kDecode");

    /* --- Resolve CUDA Driver API symbols --- */
    /* cuMemAllocHost_v2 is the versioned alias always available */
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemAllocHost,
             "cuMemAllocHost_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemFreeHost,
             "cuMemFreeHost");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", Memcpy2D,
             "cuMemcpy2D_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", CtxSynchronize,
             "cuCtxSynchronize");

    /* --- Initialise nvJPEG2000 library handle --- */
    st = ctx->fn.CreateSimple(&ctx->handle);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: nvjpeg2kCreateSimple failed (%d)\n", (int)st);
        return AVERROR_EXTERNAL;
    }

    st = ctx->fn.DecodeStateCreate(ctx->handle, &ctx->decode_state);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: nvjpeg2kDecodeStateCreate failed (%d)\n", (int)st);
        ctx->fn.Destroy(ctx->handle);
        ctx->handle = NULL;
        return AVERROR_EXTERNAL;
    }

    st = ctx->fn.StreamCreate(&ctx->jpeg_stream);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: nvjpeg2kStreamCreate failed (%d)\n", (int)st);
        ctx->fn.DecodeStateDestroy(ctx->decode_state);
        ctx->fn.Destroy(ctx->handle);
        ctx->handle = NULL;
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_INFO,
           "nvjpeg2k: GPU JPEG 2000 decoder initialised successfully\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Ensure pinned host buffers are large enough for the current frame   */
/* ------------------------------------------------------------------ */

static int ensure_host_buffers(AVCodecContext *avctx,
                                NVJPEG2KContext *ctx,
                                const nvjpeg2kImageInfo_t *info,
                                const nvjpeg2kImageComponentInfo_t comp_info[],
                                uint32_t ncomp)
{
    for (uint32_t c = 0; c < ncomp; c++) {
        uint32_t w     = comp_info[c].component_width;
        uint32_t h     = comp_info[c].component_height;
        /* 2 bytes per sample (UINT16) */
        size_t   pitch = (size_t)w * 2;
        size_t   total = pitch * h;

        if (total > ctx->host_buf_size[c]) {
            /* Release old buffer */
            if (ctx->host_buf[c]) {
                ctx->fn.MemFreeHost(ctx->host_buf[c]);
                ctx->host_buf[c] = NULL;
            }
            /* Allocate new pinned host buffer */
            CUresult cr = ctx->fn.MemAllocHost(&ctx->host_buf[c], total);
            if (cr != CUDA_SUCCESS || !ctx->host_buf[c]) {
                av_log(avctx, AV_LOG_ERROR,
                       "nvjpeg2k: cuMemAllocHost failed for component %u "
                       "(%u x %u), error %d\n", c, w, h, (int)cr);
                return AVERROR(ENOMEM);
            }
            ctx->host_buf_size[c] = total;
        }
        ctx->host_pitch[c] = pitch;
        ctx->buf_width[c]  = w;
        ctx->buf_height[c] = h;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* decode_frame                                                        */
/* ------------------------------------------------------------------ */

static int nvjpeg2k_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                  int *got_frame, AVPacket *avpkt)
{
    NVJPEG2KContext    *ctx = avctx->priv_data;
    nvjpeg2kStatus_t   st;
    nvjpeg2kImageInfo_t img_info;
    nvjpeg2kImageComponentInfo_t comp_info[MAX_COMPONENTS];
    nvjpeg2kImage_t    decoded_img;
    int                ret = 0;
    uint32_t           ncomp;

    if (!avpkt->size)
        return 0;

    /* --- 1. Parse the JPEG 2000 bitstream --- */
    st = ctx->fn.StreamParse(ctx->handle,
                             avpkt->data, (size_t)avpkt->size,
                             0, 0,
                             ctx->jpeg_stream);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: StreamParse failed (%d)\n", (int)st);
        return AVERROR_INVALIDDATA;
    }

    /* --- 2. Get image info --- */
    st = ctx->fn.StreamGetImageInfo(ctx->jpeg_stream, &img_info);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: StreamGetImageInfo failed (%d)\n", (int)st);
        return AVERROR_INVALIDDATA;
    }

    ncomp = img_info.num_components;
    if (ncomp > MAX_COMPONENTS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: too many components (%u > %d)\n",
               ncomp, MAX_COMPONENTS);
        return AVERROR_INVALIDDATA;
    }

    for (uint32_t c = 0; c < ncomp; c++) {
        st = ctx->fn.StreamGetImageComponentInfo(ctx->jpeg_stream,
                                                  &comp_info[c], c);
        if (st != NVJPEG2K_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR,
                   "nvjpeg2k: StreamGetImageComponentInfo[%u] failed (%d)\n",
                   c, (int)st);
            return AVERROR_INVALIDDATA;
        }
    }

    /* --- 3. Determine output pixel format --- */
    /*
     * DCI JPEG 2000: 3 components, XYZ colour space.
     *   → Output AV_PIX_FMT_XYZ12LE (packed, 48 bpp)
     * Other (e.g. sRGB): 3 components
     *   → Output AV_PIX_FMT_GBRP16LE (planar 16-bit)
     * Greyscale: 1 component
     *   → Output AV_PIX_FMT_GRAY16LE
     */
    enum AVPixelFormat pix_fmt;

    if (ncomp == 3) {
        /* Heuristic: DCI XYZ typically has 12-bit precision */
        if (comp_info[0].precision == 12 &&
            comp_info[1].precision == 12 &&
            comp_info[2].precision == 12 &&
            avctx->color_primaries == AVCOL_PRI_SMPTE428 /*DCI XYZ*/) {
            pix_fmt = AV_PIX_FMT_XYZ12LE;
        } else {
            pix_fmt = AV_PIX_FMT_GBRP16LE;
        }
    } else if (ncomp == 1) {
        pix_fmt = AV_PIX_FMT_GRAY16LE;
    } else {
        /* Fallback for 4-component or exotic */
        pix_fmt = AV_PIX_FMT_GBRP16LE;
        ncomp   = 3; /* only decode first 3 */
    }

    /* --- 4. Allocate/resize pinned host buffers --- */
    ret = ensure_host_buffers(avctx, ctx, &img_info, comp_info, ncomp);
    if (ret < 0)
        return ret;

    /* --- 5. Build nvjpeg2kImage_t pointing to pinned host memory --- */
    /*
     * nvJPEG2000 supports writing directly to pinned (page-locked) host
     * memory when pixel_data contains CUDA unified virtual addresses
     * obtained via cuMemAllocHost.  This avoids a separate cudaMemcpy.
     */
    void        *pixel_data[MAX_COMPONENTS];
    size_t       pitch_arr[MAX_COMPONENTS];

    for (uint32_t c = 0; c < ncomp; c++) {
        pixel_data[c] = ctx->host_buf[c];
        pitch_arr[c]  = ctx->host_pitch[c];
    }

    decoded_img.pixel_data     = pixel_data;
    decoded_img.pitch_in_bytes = pitch_arr;
    decoded_img.pixel_type     = NVJPEG2K_UINT16;
    decoded_img.num_components = ncomp;

    /* --- 6. GPU decode --- */
    st = ctx->fn.Decode(ctx->handle,
                        ctx->decode_state,
                        ctx->jpeg_stream,
                        &decoded_img,
                        NULL /* default CUDA stream */);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "nvjpeg2k: Decode failed (%d)\n", (int)st);
        return AVERROR_EXTERNAL;
    }

    /* Wait for GPU to finish writing to pinned host memory */
    ctx->fn.CtxSynchronize();

    /* --- 7. Allocate AVFrame --- */
    ret = ff_set_dimensions(avctx, (int)img_info.image_width, (int)img_info.image_height);
    if (ret < 0)
        return ret;
    avctx->pix_fmt = pix_fmt;

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    /* --- 8. Copy decoded data into AVFrame --- */
    if (pix_fmt == AV_PIX_FMT_XYZ12LE) {
        /*
         * nvjpeg2k gives us planar UINT16 (12-bit left-justified? or
         * right-justified?).  NVIDIA docs: values stored right-justified
         * (bit 0 = LSB), i.e. range 0–4095 for 12-bit.
         *
         * AV_PIX_FMT_XYZ12LE: packed, 3×uint16_le per pixel, range 0–65535.
         * We scale: out_val = in_val << 4  (12-bit → 16-bit).
         *
         * Layout: X in plane[0], Y in plane[1], Z in plane[2].
         * Packed output: X Y Z X Y Z … (little-endian uint16 each).
         */
        uint32_t W = img_info.image_width;
        uint32_t H = img_info.image_height;
        uint8_t *dst  = frame->data[0];
        int      dstride = frame->linesize[0]; /* bytes */

        const uint16_t *src_x = (const uint16_t *)ctx->host_buf[0];
        const uint16_t *src_y = (const uint16_t *)ctx->host_buf[1];
        const uint16_t *src_z = (const uint16_t *)ctx->host_buf[2];
        size_t sx_pitch = ctx->host_pitch[0] / 2; /* stride in uint16 elements */
        size_t sy_pitch = ctx->host_pitch[1] / 2;
        size_t sz_pitch = ctx->host_pitch[2] / 2;

        for (uint32_t y = 0; y < H; y++) {
            uint16_t *d = (uint16_t *)(dst + (size_t)y * dstride);
            const uint16_t *rx = src_x + (size_t)y * sx_pitch;
            const uint16_t *ry = src_y + (size_t)y * sy_pitch;
            const uint16_t *rz = src_z + (size_t)y * sz_pitch;
            for (uint32_t x = 0; x < W; x++) {
                d[3*x + 0] = (uint16_t)(rx[x] << 4);
                d[3*x + 1] = (uint16_t)(ry[x] << 4);
                d[3*x + 2] = (uint16_t)(rz[x] << 4);
            }
        }

        /* Set colour metadata for mpv / libplacebo */
        frame->color_primaries = AVCOL_PRI_SMPTE428;   /* DCI XYZ */
        frame->color_trc       = AVCOL_TRC_SMPTE428;   /* linear  */
        frame->colorspace      = AVCOL_SPC_RGB;

    } else if (pix_fmt == AV_PIX_FMT_GRAY16LE) {
        uint32_t W = comp_info[0].component_width;
        uint32_t H = comp_info[0].component_height;
        uint8_t *dst    = frame->data[0];
        int      dstride = frame->linesize[0];
        const uint16_t *src = (const uint16_t *)ctx->host_buf[0];
        size_t   spitch     = ctx->host_pitch[0] / 2;
        int      shift      = 16 - (int)comp_info[0].precision;

        for (uint32_t y = 0; y < H; y++) {
            uint16_t *d = (uint16_t *)(dst + (size_t)y * dstride);
            const uint16_t *s = src + (size_t)y * spitch;
            for (uint32_t x = 0; x < W; x++)
                d[x] = (uint16_t)(s[x] << shift);
        }

    } else {
        /*
         * AV_PIX_FMT_GBRP16LE: 3 planes G, B, R (note: G first).
         * nvjpeg2k component order matches J2K file (R=0, G=1, B=2).
         * Map: plane[0]=G, plane[1]=B, plane[2]=R.
         */
        int plane_map[3] = {1, 2, 0}; /* G→R, B→G, R→B in GBR layout */
        for (uint32_t c = 0; c < ncomp && c < 3; c++) {
            int p = plane_map[c];
            uint32_t W = comp_info[c].component_width;
            uint32_t H = comp_info[c].component_height;
            uint8_t *dst = frame->data[p];
            int dstride  = frame->linesize[p];
            const uint16_t *src = (const uint16_t *)ctx->host_buf[c];
            size_t spitch = ctx->host_pitch[c] / 2;
            int shift = 16 - (int)comp_info[c].precision;

            for (uint32_t y = 0; y < H; y++) {
                uint16_t *d = (uint16_t *)(dst + (size_t)y * dstride);
                const uint16_t *s = src + (size_t)y * spitch;
                for (uint32_t x = 0; x < W; x++)
                    d[x] = (uint16_t)(s[x] << shift);
            }
        }
        frame->color_primaries = AVCOL_PRI_UNSPECIFIED;
        frame->color_trc       = AVCOL_TRC_UNSPECIFIED;
        frame->colorspace      = AVCOL_SPC_UNSPECIFIED;
    }

    frame->flags |= AV_FRAME_FLAG_KEY;
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->pts = avpkt->pts;
    frame->pkt_dts = avpkt->dts;
    *got_frame = 1;
    return avpkt->size;
}

/* ------------------------------------------------------------------ */
/* close / uninit                                                      */
/* ------------------------------------------------------------------ */

static av_cold int nvjpeg2k_decode_close(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;

    free_host_buffers(ctx);

    if (ctx->jpeg_stream && ctx->fn.StreamDestroy) {
        ctx->fn.StreamDestroy(ctx->jpeg_stream);
        ctx->jpeg_stream = NULL;
    }
    if (ctx->decode_state && ctx->fn.DecodeStateDestroy) {
        ctx->fn.DecodeStateDestroy(ctx->decode_state);
        ctx->decode_state = NULL;
    }
    if (ctx->handle && ctx->fn.Destroy) {
        ctx->fn.Destroy(ctx->handle);
        ctx->handle = NULL;
    }

    if (ctx->nvcuda_dll) {
        FreeLibrary(ctx->nvcuda_dll);
        ctx->nvcuda_dll = NULL;
    }
    if (ctx->nvjpeg2k_dll) {
        FreeLibrary(ctx->nvjpeg2k_dll);
        ctx->nvjpeg2k_dll = NULL;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Codec registration                                                  */
/* ------------------------------------------------------------------ */

static const enum AVPixelFormat nvjpeg2k_pix_fmts[] = {
    AV_PIX_FMT_XYZ12LE,
    AV_PIX_FMT_GBRP16LE,
    AV_PIX_FMT_GRAY16LE,
    AV_PIX_FMT_NONE
};

const FFCodec ff_jpeg2000_nvjpeg_decoder = {
    .p.name         = "jpeg2000_nvjpeg",
    CODEC_LONG_NAME("JPEG 2000 (NVIDIA GPU, nvJPEG2000)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_JPEG2000,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .p.wrapper_name = "nvjpeg2k",
    CODEC_PIXFMTS_ARRAY(nvjpeg2k_pix_fmts),
    .priv_data_size = sizeof(NVJPEG2KContext),
    .init           = nvjpeg2k_decode_init,
    .close          = nvjpeg2k_decode_close,
    FF_CODEC_DECODE_CB(nvjpeg2k_decode_frame),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};

#endif /* _WIN32 */
