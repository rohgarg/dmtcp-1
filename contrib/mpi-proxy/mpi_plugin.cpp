/* CopyLeft Gregory Price (2017) */

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <mpi.h>
#include "mpi_proxy.h"

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"
#include "protectedfds.h"

// #define DEBUG

void mpi_proxy_wait_for_instructions();

int Receive_Int_From_Proxy(int connfd)
{
    int retval;
    int status;
    status = read(connfd, &retval, sizeof(int));
    // TODO: error check
    return retval;
}

int Send_Int_To_Proxy(int connfd, int arg)
{
    int status = write(connfd, &arg, sizeof(int));
    // TODO: error check
    return status;
}

int exec_proxy_cmd(int pcmd)
{
  int answer = 0;
  JTRACE("PLUGIN: Sending Proxy Command");
  if (write(PROTECTED_MPI_PROXY_FD, &pcmd, 4) < 0) {
    JNOTE("ERROR WRITING TO SOCKET")(JASSERT_ERRNO);
  }

  JTRACE("PLUGIN: Receiving Proxy Answer - ");
  if (read(PROTECTED_MPI_PROXY_FD, &answer, 4) < 0) {
    JTRACE("ERROR READING FROM SOCKET")(JASSERT_ERRNO);
  } else {
    JTRACE("Answer Received");
  }
  return answer;
}

void close_proxy(void)
{
  JTRACE("PLUGIN: Close Proxy Connection\n");
  exec_proxy_cmd(MPIProxy_Cmd_Shutdown_Proxy);
  proxy_started = 0;
}

/* hello-world */
EXTERNC int
MPI_Init(int *argc, char ***argv)
{
  JTRACE("PLUGIN: MPI_Init!\n");
  retval = exec_proxy_cmd(MPIProxy_Cmd_Init);
  return retval;
}

EXTERNC int
MPI_Comm_size(int group, int *world_size)
{
  int status = 0;
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Get_CommSize);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, group);
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (!status) // success
    *world_size = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  return status;
}

EXTERNC int
MPI_Comm_rank(int group, int *world_rank)
{
  int status = 0;
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Get_CommRank);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, group);
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (status == MPI_SUCCESS) {
    *world_rank = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  }
  return status;
}


EXTERNC int
MPI_Finalize(void)
{
  return exec_proxy_cmd(MPIProxy_Cmd_Finalize);
}

static void
mpi_plugin_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    JTRACE("*** DMTCP_EVENT_INIT\n");
    break;
  case DMTCP_EVENT_EXIT:
    JTRACE("*** DMTCP_EVENT_EXIT\n");
    close_proxy();
    break;
  default:
    break;
  }
}

static DmtcpBarrier mpiPluginBarriers[] = {
  //{ DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt, "checkpoint" },
  { DMTCP_GLOBAL_BARRIER_RESTART, restart_proxy, "restart" }
};

DmtcpPluginDescriptor_t mpi_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "mpi_plugin",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "MPI Proxy Plugin",
  DMTCP_DECL_BARRIERS(mpiPluginBarriers),
  mpi_plugin_event_hook
};

DMTCP_DECL_PLUGIN(mpi_plugin);
