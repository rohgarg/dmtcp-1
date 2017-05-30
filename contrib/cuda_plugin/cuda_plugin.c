#include <stdio.h>
#include <string.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_runtime_api.h>

#include "config.h"
#include "cuda_plugin.h"


/* Globals */
#define MAX_SIZE  2

static void **cudaMallocPtrs[MAX_SIZE] = {NULL};
static size_t cudaMallocBufSizes[MAX_SIZE] = {0};
static uint64_t idx = 0;

static void
cuda_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
  {
    DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  }
  case DMTCP_EVENT_EXIT:
    DPRINTF("The plugin is being called before exiting.\n");
    break;
  default:
    break;
  }
}

/*
 * Global barriers
 */

static void
pre_ckpt()
{
  DPRINTF("Nothing to do for now\n");
}

static void
resume()
{
  DPRINTF("Nothing to do for now\n");
}

__typeof__(&cuInit)         cuInitFncPtr         = NULL;
__typeof__(&cuMemAlloc)     cuMemAllocFncPtr     = NULL;
__typeof__(&cuMemcpy)       cuMemCpyFncPtr       = NULL;
__typeof__(&cuLaunchKernel) cuLaunchKernelFncPtr = NULL;

void *realLibcudaHandle  = NULL;
void *virtLibcudaHandle  = NULL;
int  realLibcudaFlags    = 0;

static void
resetFncPtrs()
{
  cuInitFncPtr         = _real_dlsym(realLibcudaHandle, "cuInit");
  cuMemAllocFncPtr     = _real_dlsym(realLibcudaHandle, "cuMemAlloc_v2");
  cuMemCpyFncPtr       = _real_dlsym(realLibcudaHandle, "cuMemcpyHtoD_v2");
  cuLaunchKernelFncPtr = _real_dlsym(realLibcudaHandle, "cuLaunchKernel");
}

static void
reInitLibCuda()
{
  static int x = 0;
  while (!x);
  int result = _real_dlclose(realLibcudaHandle);
  result = _real_dlclose(realLibcudaHandle);
  realLibcudaHandle = _real_dlopen("libcuda.so.1", realLibcudaFlags);
  resetFncPtrs();
  CUresult ret = cuInitFncPtr(0); // Flags must be 0, acc. to the doc
  if (ret != CUDA_SUCCESS) {
    DPRINTF("Could not init the CUDA driver\n");
    exit(-1);
  }
}

static void
restart()
{
  DPRINTF("Trying to re-init the CUDA driver\n");
  reInitLibCuda();
  // Replay the cudaMalloc calls
  for (int i = 0; i < idx; i++) {
    _real_cudaMalloc(cudaMallocPtrs[i], cudaMallocBufSizes[i]);
  }
}

/*
 * Wrapper functions
 */

cudaError_t
cudaMalloc(void **devPtr, size_t  size)
{
  cudaError_t ret;
  cudaMallocPtrs[idx] = devPtr;
  cudaMallocBufSizes[idx] = size;
  ret = _real_cudaMalloc(cudaMallocPtrs[idx], cudaMallocBufSizes[idx]);
  idx++; // FIXME: increase idx only if the allocation was successful
  return ret;
}

CUresult
localCuInit(unsigned int  Flags)
{
  return cuInitFncPtr(Flags);
}

CUresult
localCuMemAlloc(CUdeviceptr* dptr, size_t bytesize)
{
  return cuMemAllocFncPtr(dptr, bytesize);
}

static CUresult
localCuMemcpy(CUdeviceptr dst, CUdeviceptr src, size_t ByteCount)
{
  return cuMemCpyFncPtr(dst, src, ByteCount);
}

static CUresult
localCuLaunchKernel(CUfunction f, unsigned int  gridDimX, unsigned int  gridDimY,
               unsigned int  gridDimZ, unsigned int  blockDimX,
               unsigned int  blockDimY, unsigned int  blockDimZ,
               unsigned int  sharedMemBytes, CUstream hStream,
               void** kernelParams, void** extra)
{
  return cuLaunchKernelFncPtr(f, gridDimX, gridDimY, gridDimZ, blockDimX,
                              blockDimY, blockDimZ, sharedMemBytes, hStream,
                              kernelParams, extra);
}

static void*
isOneofWrappedFncs(const char *symbol)
{
  if (strncmp(symbol, "cuLaunchKernel", sizeof("cuLaunchKernel")) == 0) {
    return &localCuLaunchKernel;
  } else if (strncmp(symbol, "cuInit", sizeof("cuInit")) == 0) {
    return &localCuInit;
  } else if (strncmp(symbol, "cuMemAlloc_v2", sizeof("cuMemAlloc_v2")) == 0) {
    return &localCuMemAlloc;
  } else if (strncmp(symbol, "cuMemcpyHtoD_v2", sizeof("cuMemcpyHtoD_v2")) == 0) {
    return &localCuMemcpy;
  } else {
    return NULL;
  }
}

void*
dlopen(const char *filename, int flags)
{
  // str* functions don't like NULL :-(
  if (filename && strstr(filename, "libcuda.so.1") != NULL) {
    realLibcudaFlags = flags;
    realLibcudaHandle = _real_dlopen(filename, flags);
    resetFncPtrs();
    virtLibcudaHandle = (void*)0x257;
    return virtLibcudaHandle;
  }
  return _real_dlopen(filename, flags);
}

void*
dlsym(void *handle, const char *symbol)
{
  if (virtLibcudaHandle && handle == virtLibcudaHandle) {
    void *fncPtr = isOneofWrappedFncs(symbol);
    if (fncPtr) {
      return fncPtr;
    } else {
      return _real_dlsym(realLibcudaHandle, symbol);
    }
  }
  return _real_dlsym(handle, symbol);
}

static DmtcpBarrier cudaPluginBarriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt, "checkpoint" },
  { DMTCP_GLOBAL_BARRIER_RESUME, resume, "resume" },
  { DMTCP_GLOBAL_BARRIER_RESTART, restart, "restart" }
};

DmtcpPluginDescriptor_t cuda_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "cuda",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "CUDA plugin",
  DMTCP_DECL_BARRIERS(cudaPluginBarriers),
  cuda_event_hook
};

DMTCP_DECL_PLUGIN(cuda_plugin);
