#include <stdio.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_runtime_api.h>

#include "config.h"
#include "cuda_plugin.h"

static void
cuda_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  /* NOTE:  See warning in plugin/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_INIT:
    DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
    break;
  case DMTCP_EVENT_EXIT:
    DPRINTF("The plugin is being called before exiting.\n");
    break;
  default:
    break;
  }
}

/*
 * Wrapper functions
 */

EXTERNC cudaError_t
cudaMalloc(void **devPtr, size_t  size)
{
  cudaError_t ret;
  ret = _real_cudaMalloc(devPtr, size);
  return ret;
}

DmtcpPluginDescriptor_t cuda_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "cuda",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "CUDA plugin",
  DMTCP_NO_PLUGIN_BARRIERS,
  cuda_event_hook
};

DMTCP_DECL_PLUGIN(cuda_plugin);
