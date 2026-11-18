/*
 * JPEG 2000 decoder using NVIDIA nvJPEG2000 GPU library (dynamic loading)
 *
 * Copyright (c) 2024-2026 mpv AC-4 Edition contributors
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

#define MAX_COMPONENTS 4

typedef struct NVJPEG2KContext {
    AVClass              *av_class;

    /* DLL handles */
    HMODULE               nvjpeg2k_dll;
    HMODULE               nvcuda_dll;

    /* Function pointers */
    NVJPEG2KFunctions     fn;

    /* CUDA context & device */
    CUdevice              cuda_dev;
    CUcontext             cuda_ctx;

    /* nvJPEG2000 state */
    nvjpeg2kHandle_t      handle;
    nvjpeg2kDecodeState_t decode_state;
    nvjpeg2kStream_t      jpeg_stream;

    /* Device GPU VRAM buffers for each component */
    CUdeviceptr           dev_buf[MAX_COMPONENTS];
    size_t                dev_buf_size[MAX_COMPONENTS];
    size_t                dev_pitch[MAX_COMPONENTS];

    /* Host staging buffers for CPU packing */
    void                 *host_buf[MAX_COMPONENTS];
    size_t                host_buf_size[MAX_COMPONENTS];

    /* Last decoded image geometry */
    uint32_t              buf_width[MAX_COMPONENTS];
    uint32_t              buf_height[MAX_COMPONENTS];
    uint32_t              num_components;
    uint32_t              image_width;
    uint32_t              image_height;
} NVJPEG2KContext;

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

static void free_buffers(NVJPEG2KContext *ctx)
{
    if (ctx->cuda_ctx && ctx->fn.CtxSetCurrent)
        ctx->fn.CtxSetCurrent(ctx->cuda_ctx);

    for (int i = 0; i < MAX_COMPONENTS; i++) {
        if (ctx->dev_buf[i]) {
            if (ctx->fn.MemFree)
                ctx->fn.MemFree(ctx->dev_buf[i]);
            ctx->dev_buf[i] = 0;
            ctx->dev_buf_size[i] = 0;
        }
        if (ctx->host_buf[i]) {
            av_freep(&ctx->host_buf[i]);
            ctx->host_buf_size[i] = 0;
        }
    }
}

static av_cold int nvjpeg2k_decode_init(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;
    nvjpeg2kStatus_t st;
    CUresult cr;

    /* --- Load nvjpeg2k_0.dll --- */
    ctx->nvjpeg2k_dll = LoadLibraryW(L"nvjpeg2k_0.dll");
    if (!ctx->nvjpeg2k_dll) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: nvjpeg2k_0.dll not found — falling back to software decoder\n");
        return AVERROR_DECODER_NOT_FOUND;
    }

    /* --- Load nvcuda.dll (CUDA Driver API) --- */
    ctx->nvcuda_dll = LoadLibraryW(L"nvcuda.dll");
    if (!ctx->nvcuda_dll) {
        av_log(avctx, AV_LOG_INFO,
               "nvjpeg2k: nvcuda.dll not found — falling back to software decoder\n");
        FreeLibrary(ctx->nvjpeg2k_dll);
        ctx->nvjpeg2k_dll = NULL;
        return AVERROR_DECODER_NOT_FOUND;
    }

    /* Resolve nvJPEG2000 symbols */
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

    /* Resolve CUDA Driver API symbols */
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", Init,
             "cuInit");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", DeviceGet,
             "cuDeviceGet");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", PrimaryCtxRetain,
             "cuDevicePrimaryCtxRetain");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", PrimaryCtxRelease,
             "cuDevicePrimaryCtxRelease_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", CtxSetCurrent,
             "cuCtxSetCurrent");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemAlloc,
             "cuMemAlloc_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemFree,
             "cuMemFree_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", MemcpyDtoH,
             "cuMemcpyDtoH_v2");
    LOAD_SYM(ctx->nvcuda_dll, "nvcuda.dll", CtxSynchronize,
             "cuCtxSynchronize");

    /* Initialise CUDA Driver & Context */
    cr = ctx->fn.Init(0);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuInit failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }

    cr = ctx->fn.DeviceGet(&ctx->cuda_dev, 0);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuDeviceGet failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }

    cr = ctx->fn.PrimaryCtxRetain(&ctx->cuda_ctx, ctx->cuda_dev);
    if (cr != CUDA_SUCCESS || !ctx->cuda_ctx) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuDevicePrimaryCtxRetain failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }

    cr = ctx->fn.CtxSetCurrent(ctx->cuda_ctx);
    if (cr != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: cuCtxSetCurrent failed (%d)\n", (int)cr);
        return AVERROR_EXTERNAL;
    }

    /* Initialise nvJPEG2000 handles */
    st = ctx->fn.CreateSimple(&ctx->handle);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: CreateSimple failed (%d)\n", (int)st);
        return AVERROR_EXTERNAL;
    }

    st = ctx->fn.DecodeStateCreate(ctx->handle, &ctx->decode_state);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: DecodeStateCreate failed (%d)\n", (int)st);
        ctx->fn.Destroy(ctx->handle);
        ctx->handle = NULL;
        return AVERROR_EXTERNAL;
    }

    st = ctx->fn.StreamCreate(&ctx->jpeg_stream);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: StreamCreate failed (%d)\n", (int)st);
        ctx->fn.DecodeStateDestroy(ctx->decode_state);
        ctx->fn.Destroy(ctx->handle);
        ctx->handle = NULL;
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_INFO,
           "nvjpeg2k: GPU JPEG 2000 decoder initialised successfully on NVIDIA device %d\n",
           ctx->cuda_dev);
    return 0;
}

static int ensure_buffers(AVCodecContext *avctx,
                          NVJPEG2KContext *ctx,
                          const nvjpeg2kImageInfo_t *info,
                          const nvjpeg2kImageComponentInfo_t comp_info[],
                          uint32_t ncomp)
{
    ctx->fn.CtxSetCurrent(ctx->cuda_ctx);

    for (uint32_t c = 0; c < ncomp; c++) {
        uint32_t w = comp_info[c].component_width;
        uint32_t h = comp_info[c].component_height;
        size_t pitch = (size_t)w * 2;
        size_t total = pitch * h;

        if (total > ctx->dev_buf_size[c]) {
            if (ctx->dev_buf[c]) {
                ctx->fn.MemFree(ctx->dev_buf[c]);
                ctx->dev_buf[c] = 0;
            }
            CUresult cr = ctx->fn.MemAlloc(&ctx->dev_buf[c], total);
            if (cr != CUDA_SUCCESS || !ctx->dev_buf[c]) {
                av_log(avctx, AV_LOG_ERROR,
                       "nvjpeg2k: cuMemAlloc failed for component %u (size %zu), error %d\n",
                       c, total, (int)cr);
                return AVERROR(ENOMEM);
            }
            ctx->dev_buf_size[c] = total;
        }

        if (total > ctx->host_buf_size[c]) {
            av_freep(&ctx->host_buf[c]);
            ctx->host_buf[c] = av_malloc(total);
            if (!ctx->host_buf[c])
                return AVERROR(ENOMEM);
            ctx->host_buf_size[c] = total;
        }

        ctx->dev_pitch[c]  = pitch;
        ctx->buf_width[c]  = w;
        ctx->buf_height[c] = h;
    }
    return 0;
}

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

    /* Bind CUDA context to current decoding thread */
    ctx->fn.CtxSetCurrent(ctx->cuda_ctx);

    /* --- 1. Parse bitstream --- */
    st = ctx->fn.StreamParse(ctx->handle,
                             avpkt->data, (size_t)avpkt->size,
                             0, 0,
                             ctx->jpeg_stream);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: StreamParse failed (%d)\n", (int)st);
        return AVERROR_INVALIDDATA;
    }

    /* --- 2. Query image metadata --- */
    st = ctx->fn.StreamGetImageInfo(ctx->jpeg_stream, &img_info);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: StreamGetImageInfo failed (%d)\n", (int)st);
        return AVERROR_INVALIDDATA;
    }

    ncomp = img_info.num_components;
    if (ncomp > MAX_COMPONENTS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: too many components (%u > %d)\n",
               ncomp, MAX_COMPONENTS);
        return AVERROR_INVALIDDATA;
    }

    for (uint32_t c = 0; c < ncomp; c++) {
        st = ctx->fn.StreamGetImageComponentInfo(ctx->jpeg_stream, &comp_info[c], c);
        if (st != NVJPEG2K_STATUS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: StreamGetImageComponentInfo[%u] failed (%d)\n",
                   c, (int)st);
            return AVERROR_INVALIDDATA;
        }
    }

    /* --- 3. Determine pixel format --- */
    enum AVPixelFormat pix_fmt;
    if (ncomp == 3) {
        if (comp_info[0].precision == 12 &&
            comp_info[1].precision == 12 &&
            comp_info[2].precision == 12 &&
            avctx->color_primaries == AVCOL_PRI_SMPTE428) {
            pix_fmt = AV_PIX_FMT_XYZ12LE;
        } else {
            pix_fmt = AV_PIX_FMT_GBRP16LE;
        }
    } else if (ncomp == 1) {
        pix_fmt = AV_PIX_FMT_GRAY16LE;
    } else {
        pix_fmt = AV_PIX_FMT_GBRP16LE;
        ncomp   = 3;
    }

    /* --- 4. Allocate GPU VRAM buffers & CPU staging buffers --- */
    ret = ensure_buffers(avctx, ctx, &img_info, comp_info, ncomp);
    if (ret < 0)
        return ret;

    /* --- 5. Setup nvjpeg2kImage_t pointing to GPU device memory --- */
    void  *pixel_data[MAX_COMPONENTS];
    size_t pitch_arr[MAX_COMPONENTS];
    for (uint32_t c = 0; c < ncomp; c++) {
        pixel_data[c] = (void *)(uintptr_t)ctx->dev_buf[c];
        pitch_arr[c]  = ctx->dev_pitch[c];
    }
    decoded_img.pixel_data     = pixel_data;
    decoded_img.pitch_in_bytes = pitch_arr;
    decoded_img.pixel_type     = NVJPEG2K_UINT16;
    decoded_img.num_components = ncomp;

    /* --- 6. GPU Decode --- */
    st = ctx->fn.Decode(ctx->handle,
                        ctx->decode_state,
                        ctx->jpeg_stream,
                        &decoded_img,
                        NULL);
    if (st != NVJPEG2K_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "nvjpeg2k: Decode failed (%d)\n", (int)st);
        return AVERROR_EXTERNAL;
    }

    ctx->fn.CtxSynchronize();

    /* Copy decoded planes from GPU VRAM to CPU staging buffers */
    for (uint32_t c = 0; c < ncomp; c++) {
        size_t total = ctx->dev_pitch[c] * comp_info[c].component_height;
        ctx->fn.MemcpyDtoH(ctx->host_buf[c], ctx->dev_buf[c], total);
    }

    /* --- 7. Allocate AVFrame --- */
    ret = ff_set_dimensions(avctx, (int)img_info.image_width, (int)img_info.image_height);
    if (ret < 0)
        return ret;
    avctx->pix_fmt = pix_fmt;

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    /* --- 8. Pack into AVFrame --- */
    if (pix_fmt == AV_PIX_FMT_XYZ12LE) {
        uint32_t W = img_info.image_width;
        uint32_t H = img_info.image_height;
        uint8_t *dst = frame->data[0];
        int dstride  = frame->linesize[0];

        const uint16_t *src_x = (const uint16_t *)ctx->host_buf[0];
        const uint16_t *src_y = (const uint16_t *)ctx->host_buf[1];
        const uint16_t *src_z = (const uint16_t *)ctx->host_buf[2];
        size_t sx_pitch = ctx->dev_pitch[0] / 2;
        size_t sy_pitch = ctx->dev_pitch[1] / 2;
        size_t sz_pitch = ctx->dev_pitch[2] / 2;

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
        frame->color_primaries = AVCOL_PRI_SMPTE428;
        frame->color_trc       = AVCOL_TRC_SMPTE428;
        frame->colorspace      = AVCOL_SPC_RGB;

    } else if (pix_fmt == AV_PIX_FMT_GRAY16LE) {
        uint32_t W = comp_info[0].component_width;
        uint32_t H = comp_info[0].component_height;
        uint8_t *dst = frame->data[0];
        int dstride  = frame->linesize[0];
        const uint16_t *src = (const uint16_t *)ctx->host_buf[0];
        size_t spitch = ctx->dev_pitch[0] / 2;
        int shift = 16 - (int)comp_info[0].precision;

        for (uint32_t y = 0; y < H; y++) {
            uint16_t *d = (uint16_t *)(dst + (size_t)y * dstride);
            const uint16_t *s = src + (size_t)y * spitch;
            for (uint32_t x = 0; x < W; x++)
                d[x] = (uint16_t)(s[x] << shift);
        }
    } else {
        /*
         * AV_PIX_FMT_GBRP16LE: plane[0]=G, plane[1]=B, plane[2]=R.
         * nvjpeg2k component 0=R, 1=G, 2=B.
         * Mapping: comp 0 (R) -> plane 2; comp 1 (G) -> plane 0; comp 2 (B) -> plane 1.
         */
        int plane_map[3] = {2, 0, 1};
        for (uint32_t c = 0; c < ncomp && c < 3; c++) {
            int p = plane_map[c];
            uint32_t W = comp_info[c].component_width;
            uint32_t H = comp_info[c].component_height;
            uint8_t *dst = frame->data[p];
            int dstride  = frame->linesize[p];
            const uint16_t *src = (const uint16_t *)ctx->host_buf[c];
            size_t spitch = ctx->dev_pitch[c] / 2;
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

static av_cold int nvjpeg2k_decode_close(AVCodecContext *avctx)
{
    NVJPEG2KContext *ctx = avctx->priv_data;

    free_buffers(ctx);

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
    if (ctx->cuda_ctx && ctx->fn.PrimaryCtxRelease) {
        ctx->fn.PrimaryCtxRelease(ctx->cuda_dev);
        ctx->cuda_ctx = NULL;
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
