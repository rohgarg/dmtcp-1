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

struct VirtualMPIInfo
{
  int world_rank;
  int name_len;
  char *processor_name;
};

struct VirtualMPIInfo virtinfo;

int replay_commands[1024];
int replay_count = 0;
int proxy_started = 0;
bool proxy_inited = false;

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

void setup_virtualization_info(bool islaunch)
{
  int status = 0;

  if (islaunch) {
    int world_rank = 0;
    // On launch, we need to record this info in order
    // to successfully restart
    status = MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    // TODO: Error check
    virtinfo.world_rank = world_rank;
  }
  // On restart, we just need to set our proxy up with the
  // correct virtual values
  JNOTE("SET RANK\n");
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Set_CommRank);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, virtinfo.world_rank);
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  // TODO: error checking
  JASSERT(status == MPI_SUCCESS);

  JTRACE("DONE SET\n");
}

// Proxy Setup
void restart_proxy()
{
  int i = 0;
  int retval = 0;

  // We're resuming from a restart, set it all up
  JTRACE("PLUGIN: Restart - Initialize Proxy Connection");
  // Replay any replay packets, this catchs INIT on restart
  for (i = 0; i < replay_count; i++)
  {
    JTRACE("PLUGIN: Replaying command")(replay_commands[i]);
    exec_proxy_cmd(replay_commands[i]);
    i++;
  }
  proxy_started = 1;

  // Then configure the proxy with the cached virtual info.
  JTRACE("PLUGIN: Configuring Proxy\n");
  setup_virtualization_info(false);
  return;
}

void close_proxy(void)
{
  JTRACE("PLUGIN: Close Proxy Connection\n");
  exec_proxy_cmd(MPIProxy_Cmd_Shutdown_Proxy);
  proxy_started = 0;
}

void add_replay_command(int pcmd)
{
  replay_commands[replay_count] = pcmd;
  replay_count++;
}

/* hello-world */
EXTERNC int
MPI_Init(int *argc, char ***argv)
{
  int retval = 0;
  add_replay_command(MPIProxy_Cmd_Init);
  JTRACE("PLUGIN: MPI_Init!\n");
  retval = exec_proxy_cmd(MPIProxy_Cmd_Init);
  proxy_started = true;
  proxy_inited = true;
  setup_virtualization_info(true);
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
  add_replay_command(MPIProxy_Cmd_Finalize);
  JTRACE("PLUGIN: MPI_Finalize\n");
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
