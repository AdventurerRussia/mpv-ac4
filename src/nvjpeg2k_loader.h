/*
 * nvJPEG2000 dynamic loader header
 * Hand-crafted from DLL exports + NVIDIA nvJPEG2000 SDK documentation.
 * No compile-time dependency on nvJPEG2000 SDK or CUDA toolkit required.
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

#ifndef AVCODEC_NVJPEG2K_LOADER_H
#define AVCODEC_NVJPEG2K_LOADER_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Opaque handle types (forward-declared as incomplete structs)        */
/* ------------------------------------------------------------------ */

typedef struct nvjpeg2kHandle         *nvjpeg2kHandle_t;
typedef struct nvjpeg2kDecodeState    *nvjpeg2kDecodeState_t;
typedef struct nvjpeg2kStream         *nvjpeg2kStream_t;
typedef struct nvjpeg2kDecodeParams   *nvjpeg2kDecodeParams_t;

/* ------------------------------------------------------------------ */
/* CUDA opaque types we need for cuMemcpyDtoH (from nvcuda.dll)       */
/* ------------------------------------------------------------------ */

typedef unsigned long long CUdeviceptr;
typedef int                CUresult;
#define CUDA_SUCCESS 0

/* ------------------------------------------------------------------ */
/* nvJPEG2000 status enum                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    NVJPEG2K_STATUS_SUCCESS           = 0,
    NVJPEG2K_STATUS_NOT_INITIALIZED   = 1,
    NVJPEG2K_STATUS_INVALID_PARAMETER = 2,
    NVJPEG2K_STATUS_BAD_JPEG         = 3,
    NVJPEG2K_STATUS_JPEG_NOT_SUPPORTED = 4,
    NVJPEG2K_STATUS_ALLOCATOR_FAILURE = 5,
    NVJPEG2K_STATUS_EXECUTION_FAILED  = 6,
    NVJPEG2K_STATUS_ARCH_MISMATCH     = 7,
    NVJPEG2K_STATUS_INTERNAL_ERROR    = 8,
    NVJPEG2K_STATUS_IMPLEMENTATION_NOT_SUPPORTED = 9,
} nvjpeg2kStatus_t;

/* ------------------------------------------------------------------ */
/* nvJPEG2000 pixel type enum                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    NVJPEG2K_UINT8  = 0,  /* 8-bit unsigned  */
    NVJPEG2K_UINT16 = 1,  /* 16-bit unsigned */
    NVJPEG2K_INT16  = 2,  /* 16-bit signed   */
} nvjpeg2kImageType_t;

/* ------------------------------------------------------------------ */
/* nvJPEG2000 image info structs                                       */
/* ------------------------------------------------------------------ */

/* Per-component info returned by nvjpeg2kStreamGetImageComponentInfo */
typedef struct {
    uint32_t component_width;   /* width of this component  */
    uint32_t component_height;  /* height of this component */
    uint8_t  precision;         /* bit-depth (e.g. 12)      */
    uint8_t  sgn;               /* 0=unsigned, 1=signed     */
} nvjpeg2kImageComponentInfo_t;

/* Global image info returned by nvjpeg2kStreamGetImageInfo */
typedef struct {
    uint32_t image_width;       /* full image width  */
    uint32_t image_height;      /* full image height */
    uint32_t tile_width;        /* tile width  */
    uint32_t tile_height;       /* tile height */
    uint32_t num_tiles_x;       /* number of tiles in X */
    uint32_t num_tiles_y;       /* number of tiles in Y */
    uint32_t num_components;    /* number of colour components */
} nvjpeg2kImageInfo_t;

/* Decoded image descriptor (device or host pointers per component) */
typedef struct {
    void               **pixel_data;     /* array[num_components] of pointers */
    size_t              *pitch_in_bytes; /* array[num_components] of pitches  */
    nvjpeg2kImageType_t  pixel_type;     /* data type stored                  */
    uint32_t             num_components;
} nvjpeg2kImage_t;

/* ------------------------------------------------------------------ */
/* Function pointer typedefs                                           */
/* ------------------------------------------------------------------ */

/* nvjpeg2kCreateSimple(nvjpeg2kHandle_t *handle) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kCreateSimple)(nvjpeg2kHandle_t *handle);

/* nvjpeg2kDestroy(nvjpeg2kHandle_t handle) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDestroy)(nvjpeg2kHandle_t handle);

/* nvjpeg2kDecodeStateCreate(nvjpeg2kHandle_t handle,
                              nvjpeg2kDecodeState_t *decode_state) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecodeStateCreate)(
    nvjpeg2kHandle_t handle, nvjpeg2kDecodeState_t *decode_state);

/* nvjpeg2kDecodeStateDestroy(nvjpeg2kDecodeState_t decode_state) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecodeStateDestroy)(
    nvjpeg2kDecodeState_t decode_state);

/* nvjpeg2kStreamCreate(nvjpeg2kStream_t *stream) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamCreate)(nvjpeg2kStream_t *stream);

/* nvjpeg2kStreamDestroy(nvjpeg2kStream_t stream) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamDestroy)(nvjpeg2kStream_t stream);

/* nvjpeg2kStreamParse(nvjpeg2kHandle_t handle,
                        const unsigned char *data, size_t length,
                        int save_metadata, int save_stream,
                        nvjpeg2kStream_t stream) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamParse)(
    nvjpeg2kHandle_t handle,
    const unsigned char *data, size_t length,
    int save_metadata, int save_stream,
    nvjpeg2kStream_t stream);

/* nvjpeg2kStreamGetImageInfo(nvjpeg2kStream_t stream,
                               nvjpeg2kImageInfo_t *image_info) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamGetImageInfo)(
    nvjpeg2kStream_t stream, nvjpeg2kImageInfo_t *image_info);

/* nvjpeg2kStreamGetImageComponentInfo(nvjpeg2kStream_t stream,
                                        nvjpeg2kImageComponentInfo_t *comp_info,
                                        uint32_t component_id) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kStreamGetImageComponentInfo)(
    nvjpeg2kStream_t stream,
    nvjpeg2kImageComponentInfo_t *comp_info,
    uint32_t component_id);

/* nvjpeg2kDecodeParamsCreate(nvjpeg2kDecodeParams_t *decode_params) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecodeParamsCreate)(
    nvjpeg2kDecodeParams_t *decode_params);

/* nvjpeg2kDecodeParamsDestroy(nvjpeg2kDecodeParams_t decode_params) */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecodeParamsDestroy)(
    nvjpeg2kDecodeParams_t decode_params);

/* nvjpeg2kDecode(nvjpeg2kHandle_t handle,
                   nvjpeg2kDecodeState_t decode_state,
                   nvjpeg2kStream_t stream,
                   nvjpeg2kImage_t *destination,
                   cudaStream_t stream_cuda)
   Note: last arg is cudaStream_t — we pass NULL (default stream). */
typedef nvjpeg2kStatus_t (*PFN_nvjpeg2kDecode)(
    nvjpeg2kHandle_t handle,
    nvjpeg2kDecodeState_t decode_state,
    nvjpeg2kStream_t jpeg_stream,
    nvjpeg2kImage_t *destination,
    void *cuda_stream);  /* cudaStream_t, NULL = default stream */

/* ------------------------------------------------------------------ */
/* CUDArt / nvcuda function pointer typedefs                           */
/* nvcuda.dll is the driver API — always present with any NVIDIA GPU.  */
/* We only need cuMemAllocHost / cuMemFreeHost / cuMemcpy2D            */
/* ------------------------------------------------------------------ */

/* cuMemAllocHost(void **pp, size_t bytesize) */
typedef CUresult (*PFN_cuMemAllocHost)(void **pp, size_t bytesize);

/* cuMemFreeHost(void *p) */
typedef CUresult (*PFN_cuMemFreeHost)(void *p);

/* cuMemcpy2D — copies 2D region device->host
   We use the full cuMemcpy2D_v2 from nvcuda.dll.
   The struct CUDA_MEMCPY2D matches the driver API layout. */
typedef enum {
    CU_MEMORYTYPE_HOST   = 1,
    CU_MEMORYTYPE_DEVICE = 2,
    CU_MEMORYTYPE_ARRAY  = 3,
    CU_MEMORYTYPE_UNIFIED = 4,
} CUmemorytype;

typedef struct {
    size_t srcXInBytes;
    size_t srcY;
    CUmemorytype srcMemoryType;
    const void  *srcHost;
    CUdeviceptr  srcDevice;
    void        *srcArray;    /* unused */
    size_t       srcPitch;

    size_t dstXInBytes;
    size_t dstY;
    CUmemorytype dstMemoryType;
    void        *dstHost;
    CUdeviceptr  dstDevice;
    void        *dstArray;    /* unused */
    size_t       dstPitch;

    size_t WidthInBytes;
    size_t Height;
} CUDA_MEMCPY2D;

/* cuMemcpy2D(const CUDA_MEMCPY2D *pCopy) */
typedef CUresult (*PFN_cuMemcpy2D)(const CUDA_MEMCPY2D *pCopy);

/* cuCtxSynchronize() */
typedef CUresult (*PFN_cuCtxSynchronize)(void);

/* ------------------------------------------------------------------ */
/* Loader struct — holds all function pointers                         */
/* ------------------------------------------------------------------ */

typedef struct NVJPEG2KFunctions {
    /* nvjpeg2k_0.dll */
    PFN_nvjpeg2kCreateSimple                   CreateSimple;
    PFN_nvjpeg2kDestroy                        Destroy;
    PFN_nvjpeg2kDecodeStateCreate              DecodeStateCreate;
    PFN_nvjpeg2kDecodeStateDestroy             DecodeStateDestroy;
    PFN_nvjpeg2kStreamCreate                   StreamCreate;
    PFN_nvjpeg2kStreamDestroy                  StreamDestroy;
    PFN_nvjpeg2kStreamParse                    StreamParse;
    PFN_nvjpeg2kStreamGetImageInfo             StreamGetImageInfo;
    PFN_nvjpeg2kStreamGetImageComponentInfo    StreamGetImageComponentInfo;
    PFN_nvjpeg2kDecode                         Decode;
    /* nvcuda.dll (driver API) */
    PFN_cuMemAllocHost                         MemAllocHost;
    PFN_cuMemFreeHost                          MemFreeHost;
    PFN_cuMemcpy2D                             Memcpy2D;
    PFN_cuCtxSynchronize                       CtxSynchronize;
} NVJPEG2KFunctions;

#endif /* AVCODEC_NVJPEG2K_LOADER_H */
