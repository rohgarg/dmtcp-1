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
#include "jfilesystem.h"
#include "protectedfds.h"

// #define DEBUG

int gworld_rank = 0;

void mpi_proxy_wait_for_instructions();

int Receive_Int_From_Proxy(int connfd)
{
    int retval;
    int status;
    status = read(connfd, &retval, sizeof(int));
    // TODO: error check
    return retval;
}

int Receive_Buf_From_Proxy(int connfd, void* buf, int size)
{
  int status = 0;
  status = read(connfd, buf, size);
  return status;
}

int Send_Int_To_Proxy(int connfd, int arg)
{
    int status = write(connfd, &arg, sizeof(int));
    // TODO: error check
    return status;
}

int Send_Buf_To_Proxy(int connfd, const void* buf, int size)
{
    int status = write(connfd, buf, size);
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
}

/* hello-world */
EXTERNC int
MPI_Init(int *argc, char ***argv)
{
  JTRACE("PLUGIN: MPI_Init!\n");
  return exec_proxy_cmd(MPIProxy_Cmd_Init);
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
    JTRACE("*** GOT RANK\n");
    gworld_rank = *world_rank;
  }
  return status;
}

EXTERNC int
MPI_Type_size(MPI_Datatype datatype, int *size)
{
  int status = 0;
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Type_size);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, datatype);

  // get the status
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (status == MPI_SUCCESS) // if successful, ge the size
    *size = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);

  return status;
}

EXTERNC int
MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag,
          MPI_Comm comm)
{
  int status = 0xFFFFFFFF;
  int size = 0;

  status = MPI_Type_size(datatype, &size);
  if (status == MPI_SUCCESS)
  {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Send);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count * size);
    write(PROTECTED_MPI_PROXY_FD, buf, count*size);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, dest);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    // Get the status
    status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  }
  return status;
}

EXTERNC int
MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag,
          MPI_Comm comm, MPI_Status *mpi_status)
{
  int status = 0xFFFFFFFF;
  int size = 0;

  status = MPI_Type_size(datatype, &size);
  if (status == MPI_SUCCESS)
  {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Recv);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, source);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
    if (mpi_status == MPI_STATUS_IGNORE)
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF);
    else
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0x0);


    // Buffer and Status are received after the status is confirmed
    status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    if (status == 0)
    {
      Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, buf, size);
      if (mpi_status != MPI_STATUS_IGNORE)
        Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                mpi_status,
                                sizeof(MPI_Status));
    }
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


static void
pre_ckpt_update_ckpt_dir()
{
  const char *ckptDir = dmtcp_get_ckpt_dir();
  dmtcp::string baseDir;

  if (strstr(ckptDir, dmtcp_get_computation_id_str()) != NULL) {
    baseDir = jalib::Filesystem::DirName(ckptDir);
  } else {
    baseDir = ckptDir;
  }
  dmtcp::ostringstream o;
  o << baseDir << "/ckpt_rank_" << gworld_rank;
  dmtcp_set_ckpt_dir(o.str().c_str());
}

static void
pre_ckpt_drain_data_from_proxy()
{
  // One way to do this is to have two global barriers:
  //  1) Each rank MPI_Sends a known "cookie"
  //  2) Each rank receives all the packets from its proxy
  //     until it sees the known cookie
  //
  // The other way to do this is to have the proxy keep track
  // of undelivered packets (i.e., packets that have not been consumed
  // by the rank). This way we can have just a single global pre-ckpt
  // barrier, where each rank will receive all undelivered packets into
  // a local "vector<buffer>"
  //
  // The problem with the first approach is that we have to worry about
  // communication channels between ranks. MPI guarantees no-overtaking
  //  (i.e., FIFO) between ranks. However, I suspect that the no-overtaking
  // rule is not guaranteed across multiple communication channels that
  // might exists between two ranks.
  //
  // The problem with the second approach is that we introduce some
  // state in the proxy. Note that with the first approach, the proxy can
  // continue to be stateless and dumb.
  //
  // In both the approaches, the resume/restart part remains the same:
  // On resume/restart, MPI_Recv() calls from the user threads
  // must be serviced from the local buffers, until the buffers
  // have been emptied. When the local buffers have been emptied,
  // the MPI_Recv calls can/should be forwarded to the proxy.

  // Proxy_Receive()
}

static DmtcpBarrier mpiPluginBarriers[] = {
  //{ DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt, "checkpoint" },
  //{ DMTCP_GLOBAL_BARRIER_RESTART, restart_proxy, "restart" },
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt_drain_data_from_proxy,
    "Drain-Data-From-Proxy" },
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt_update_ckpt_dir,
    "update-ckpt-dir-by-rank" }
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
