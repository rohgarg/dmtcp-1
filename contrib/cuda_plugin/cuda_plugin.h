#ifndef __CUDA_PLUGIN_H
#define __CUDA_PLUGIN_H

#include "dmtcp.h"
#include "dmtcp_dlsym.h"

#define DEBUG_SIGNATURE "[CUDA Plugin]"
#ifdef CUDA_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do { fprintf(stderr, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)
#else // ifdef CUDA_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do {} while (0)
#endif // ifdef CUDA_PLUGIN_DEBUG


#define   _real_cudaMalloc      NEXT_FNC_DEFAULT(cudaMalloc)
#define   _real_dlopen          NEXT_FNC_DEFAULT(dlopen)
#define   _real_dlclose         NEXT_FNC_DEFAULT(dlclose)
#define   _real_dlsym           NEXT_FNC_DEFAULT(dlsym)
#define   _real_cuLaunchKernel  NEXT_FNC_DEFAULT(cuLaunchKernel)

#endif // ifndef  __CUDA_PLUGIN_H
