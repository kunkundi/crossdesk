#include "nvcodec_api.h"

#include "log.h"

TcuInit cuInit_ld = NULL;
TcuDeviceGet cuDeviceGet_ld = NULL;
TcuDeviceGetCount cuDeviceGetCount_ld = NULL;
TcuCtxCreate cuCtxCreate_ld = NULL;
TcuGetErrorName cuGetErrorName_ld = NULL;
TcuCtxPushCurrent cuCtxPushCurrent_ld = NULL;
TcuCtxPopCurrent cuCtxPopCurrent_ld = NULL;
TcuMemAlloc cuMemAlloc_ld = NULL;
TcuMemAllocPitch cuMemAllocPitch_ld = NULL;
TcuMemFree cuMemFree_ld = NULL;
TcuMemcpy2DAsync cuMemcpy2DAsync_ld = NULL;
TcuStreamSynchronize cuStreamSynchronize_ld = NULL;
TcuMemcpy2D cuMemcpy2D_ld = NULL;
TcuMemcpy2DUnaligned cuMemcpy2DUnaligned_ld = NULL;

TcuvidCtxLockCreate cuvidCtxLockCreate_ld = NULL;
TcuvidGetDecoderCaps cuvidGetDecoderCaps_ld = NULL;
TcuvidCreateDecoder cuvidCreateDecoder_ld = NULL;
TcuvidDestroyDecoder cuvidDestroyDecoder_ld = NULL;
TcuvidDecodePicture cuvidDecodePicture_ld = NULL;
TcuvidGetDecodeStatus cuvidGetDecodeStatus_ld = NULL;
TcuvidReconfigureDecoder cuvidReconfigureDecoder_ld = NULL;
TcuvidMapVideoFrame64 cuvidMapVideoFrame64_ld = NULL;
TcuvidUnmapVideoFrame64 cuvidUnmapVideoFrame64_ld = NULL;
TcuvidCtxLockDestroy cuvidCtxLockDestroy_ld = NULL;
TcuvidCreateVideoParser cuvidCreateVideoParser_ld = NULL;
TcuvidParseVideoData cuvidParseVideoData_ld = NULL;
TcuvidDestroyVideoParser cuvidDestroyVideoParser_ld = NULL;

TNvEncodeAPICreateInstance NvEncodeAPICreateInstance_ld = NULL;
TNvEncodeAPIGetMaxSupportedVersion NvEncodeAPIGetMaxSupportedVersion_ld = NULL;

static HMODULE nvcuda_dll = NULL;
static HMODULE nvcuvid_dll = NULL;
static HMODULE nvEncodeAPI64_dll = NULL;

int LoadNvCodecDll() {
  // Load library
  nvcuda_dll = LoadLibrary(TEXT("nvcuda.dll"));
  if (nvcuda_dll == NULL) {
    LOG_ERROR("Unable to load nvcuda.dll");
    return -1;
  }

  cuInit_ld = (TcuInit)GetProcAddress(nvcuda_dll, "cuInit");
  if (cuInit_ld == NULL) {
    LOG_ERROR("Unable to find function cuInit()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuDeviceGet_ld = (TcuDeviceGet)GetProcAddress(nvcuda_dll, "cuDeviceGet");
  if (cuDeviceGet_ld == NULL) {
    LOG_ERROR("Unable to find function cuDeviceGet()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuDeviceGetCount_ld =
      (TcuDeviceGetCount)GetProcAddress(nvcuda_dll, "cuDeviceGetCount");
  if (cuDeviceGetCount_ld == NULL) {
    LOG_ERROR("Unable to find function cuDeviceGetCount()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuCtxCreate_ld = (TcuCtxCreate)GetProcAddress(nvcuda_dll, "cuCtxCreate_v2");
  if (cuCtxCreate_ld == NULL) {
    LOG_ERROR("Unable to find function cuCtxCreate()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuGetErrorName_ld =
      (TcuGetErrorName)GetProcAddress(nvcuda_dll, "cuGetErrorName");
  if (cuGetErrorName_ld == NULL) {
    LOG_ERROR("Unable to find function cuGetErrorName()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuCtxPushCurrent_ld =
      (TcuCtxPushCurrent)GetProcAddress(nvcuda_dll, "cuCtxPushCurrent_v2");
  if (cuCtxPushCurrent_ld == NULL) {
    LOG_ERROR("Unable to find function cuCtxPushCurrent()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuCtxPopCurrent_ld =
      (TcuCtxPopCurrent)GetProcAddress(nvcuda_dll, "cuCtxPopCurrent_v2");
  if (cuCtxPopCurrent_ld == NULL) {
    LOG_ERROR("Unable to find function cuCtxPopCurrent()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }
  cuMemAlloc_ld = (TcuMemAlloc)GetProcAddress(nvcuda_dll, "cuMemAlloc_v2");
  if (cuMemAlloc_ld == NULL) {
    LOG_ERROR("Unable to find function cuMemAlloc()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuMemAllocPitch_ld =
      (TcuMemAllocPitch)GetProcAddress(nvcuda_dll, "cuMemAllocPitch_v2");
  if (cuMemAllocPitch_ld == NULL) {
    LOG_ERROR("Unable to find function cuMemAllocPitch()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuMemFree_ld = (TcuMemFree)GetProcAddress(nvcuda_dll, "cuMemFree_v2");
  if (cuMemFree_ld == NULL) {
    LOG_ERROR("Unable to find function cuMemFree()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuMemcpy2DAsync_ld =
      (TcuMemcpy2DAsync)GetProcAddress(nvcuda_dll, "cuMemcpy2DAsync_v2");
  if (cuMemcpy2DAsync_ld == NULL) {
    LOG_ERROR("Unable to find function cuMemcpy2DAsync()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuStreamSynchronize_ld =
      (TcuStreamSynchronize)GetProcAddress(nvcuda_dll, "cuStreamSynchronize");
  if (cuStreamSynchronize_ld == NULL) {
    LOG_ERROR("Unable to find function cuStreamSynchronize()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuMemcpy2D_ld = (TcuMemcpy2D)GetProcAddress(nvcuda_dll, "cuMemcpy2D_v2");
  if (cuMemcpy2D_ld == NULL) {
    LOG_ERROR("Unable to find function cuMemcpy2D()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  cuMemcpy2DUnaligned_ld = (TcuMemcpy2DUnaligned)GetProcAddress(
      nvcuda_dll, "cuMemcpy2DUnaligned_v2");
  if (cuMemcpy2DUnaligned_ld == NULL) {
    LOG_ERROR("Unable to find function cuMemcpy2DUnaligned()");
    FreeLibrary(nvcuda_dll);
    return -1;
  }

  //
  nvcuvid_dll = LoadLibrary(TEXT("nvcuvid.dll"));
  if (nvcuvid_dll == NULL) {
    LOG_ERROR("Unable to load nvcuvid.dll");
    return -1;
  }

  cuvidCtxLockCreate_ld =
      (TcuvidCtxLockCreate)GetProcAddress(nvcuvid_dll, "cuvidCtxLockCreate");
  if (cuvidCtxLockCreate_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidCtxLockCreate()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidGetDecoderCaps_ld =
      (TcuvidGetDecoderCaps)GetProcAddress(nvcuvid_dll, "cuvidGetDecoderCaps");
  if (cuvidGetDecoderCaps_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidGetDecoderCaps()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidCreateDecoder_ld =
      (TcuvidCreateDecoder)GetProcAddress(nvcuvid_dll, "cuvidCreateDecoder");
  if (cuvidCreateDecoder_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidCreateDecoder()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidDestroyDecoder_ld =
      (TcuvidDestroyDecoder)GetProcAddress(nvcuvid_dll, "cuvidDestroyDecoder");
  if (cuvidDestroyDecoder_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidDestroyDecoder()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidDecodePicture_ld =
      (TcuvidDecodePicture)GetProcAddress(nvcuvid_dll, "cuvidDecodePicture");
  if (cuvidDecodePicture_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidDecodePicture()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidGetDecodeStatus_ld = (TcuvidGetDecodeStatus)GetProcAddress(
      nvcuvid_dll, "cuvidGetDecodeStatus");
  if (cuvidGetDecodeStatus_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidGetDecodeStatus()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidReconfigureDecoder_ld = (TcuvidReconfigureDecoder)GetProcAddress(
      nvcuvid_dll, "cuvidReconfigureDecoder");
  if (cuvidReconfigureDecoder_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidReconfigureDecoder()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidMapVideoFrame64_ld = (TcuvidMapVideoFrame64)GetProcAddress(
      nvcuvid_dll, "cuvidMapVideoFrame64");
  if (cuvidMapVideoFrame64_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidMapVideoFrame64()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidUnmapVideoFrame64_ld = (TcuvidUnmapVideoFrame64)GetProcAddress(
      nvcuvid_dll, "cuvidUnmapVideoFrame64");
  if (cuvidUnmapVideoFrame64_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidUnmapVideoFrame64()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidCtxLockDestroy_ld =
      (TcuvidCtxLockDestroy)GetProcAddress(nvcuvid_dll, "cuvidCtxLockDestroy");
  if (cuvidCtxLockDestroy_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidCtxLockDestroy()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidCreateVideoParser_ld = (TcuvidCreateVideoParser)GetProcAddress(
      nvcuvid_dll, "cuvidCreateVideoParser");
  if (cuvidCreateVideoParser_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidCreateVideoParser()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidParseVideoData_ld =
      (TcuvidParseVideoData)GetProcAddress(nvcuvid_dll, "cuvidParseVideoData");
  if (cuvidParseVideoData_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidParseVideoData()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  cuvidDestroyVideoParser_ld = (TcuvidDestroyVideoParser)GetProcAddress(
      nvcuvid_dll, "cuvidDestroyVideoParser");
  if (cuvidDestroyVideoParser_ld == NULL) {
    LOG_ERROR("Unable to find function cuvidDestroyVideoParser()");
    FreeLibrary(nvcuvid_dll);
    return -1;
  }

  //
  nvEncodeAPI64_dll = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
  if (nvEncodeAPI64_dll == NULL) {
    LOG_ERROR("Unable to load nvEncodeAPI64.dll");
    return -1;
  }

  NvEncodeAPICreateInstance_ld = (TNvEncodeAPICreateInstance)GetProcAddress(
      nvEncodeAPI64_dll, "NvEncodeAPICreateInstance");
  if (NvEncodeAPICreateInstance_ld == NULL) {
    LOG_ERROR("Unable to find function NvEncodeAPICreateInstance()");
    FreeLibrary(nvEncodeAPI64_dll);
    return -1;
  }

  NvEncodeAPIGetMaxSupportedVersion_ld =
      (TNvEncodeAPIGetMaxSupportedVersion)GetProcAddress(
          nvEncodeAPI64_dll, "NvEncodeAPIGetMaxSupportedVersion");
  if (NvEncodeAPIGetMaxSupportedVersion_ld == NULL) {
    LOG_ERROR("Unable to find function NvEncodeAPIGetMaxSupportedVersion()");
    FreeLibrary(nvEncodeAPI64_dll);
    return -1;
  }

  LOG_INFO("Load NvCodec API success");

  return 0;
}

int ReleaseNvCodecDll() {
  if (nvcuda_dll != NULL) {
    FreeLibrary(nvcuda_dll);
    nvcuda_dll = NULL;
  }

  if (nvcuvid_dll != NULL) {
    FreeLibrary(nvcuvid_dll);
    nvcuvid_dll = NULL;
  }

  if (nvEncodeAPI64_dll != NULL) {
    FreeLibrary(nvEncodeAPI64_dll);
    nvEncodeAPI64_dll = NULL;
  }

  LOG_INFO("Release NvCodec API success");

  return 0;
}