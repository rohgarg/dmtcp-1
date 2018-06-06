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
#include <fcntl.h>
#include <mpi.h>
#include <vector>
#include <map>

#include "mpi_plugin.h"
#include "mpi_proxy.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

int g_world_rank = 0;
int g_world_size = 0;

using namespace dmtcp;

// #define DEBUG
void dpf(const char * msg)
{
#ifdef DEBUG
  printf("%d %s\n", g_world_rank, msg);
  fflush(stdout);
#else
  (void) msg;
#endif
}

// this is the global and local send/recv counts that will be used
// in order to determine whether the network has been succesfully
// drained
int g_world_sent = 0;
int g_world_recv = 0;
int g_local_sent = 0;
int g_local_recv = 0;

// global status booleans used to inform a blocking Isend or Wait
// that it has been completed after restart.
bool g_restart_receive;
bool g_restart_retval;
bool g_pending_send;
bool g_pending_wait;

// cached wait request information - required to update a blocking
// Wait upon restart.
MPI_Request* g_pending_wait_request;
MPI_Status* g_pending_wait_status;

// cached messages drained during a checkpoint
std::vector<Message *> g_message_queue;

// unserviced irecv requests
std::map<MPI_Request*, Async_Message*> g_async_messages;

// serviced but un-MPI_Test/Wait'd irecv requests
std::map<MPI_Request*, Async_Message*> g_irecv_cache;

void mpi_proxy_wait_for_instructions();

int Receive_Int_From_Proxy(int connfd)
{
    int retval;
    int status;
    status = Util::readAll(connfd, &retval, sizeof(int));
    // TODO: error check
    return retval;
}

int Complete_Blocking_Call_Safely(int connfd, bool* g_pending)
{
  int flags = 0;
  int status = EWOULDBLOCK;
  int retval = 0;
  while (status == EWOULDBLOCK && !g_restart_receive)
  {
    DMTCP_PLUGIN_DISABLE_CKPT();
    if (g_restart_receive)
    {
      DMTCP_PLUGIN_ENABLE_CKPT();
      break;
    }
    flags = fcntl(connfd, F_GETFL, 0);
    fcntl(connfd, F_SETFL, flags | O_NONBLOCK);
    status = Util::readAll(connfd, &retval, sizeof(int));
    fcntl(connfd, F_SETFL, flags);
    if (status != EWOULDBLOCK)
      *g_pending = false;
    DMTCP_PLUGIN_ENABLE_CKPT();
  }

  if (g_restart_receive)
    retval = g_restart_retval;
  return retval;
}

int Receive_Buf_From_Proxy(int connfd, void* buf, int size)
{
  int received = 0;
  while (received != size)
    received += Util::readAll(connfd, ((char *)buf)+received, size-received);
  // TODO: error check
  return size == received;
}

int Send_Int_To_Proxy(int connfd, int arg)
{
    int status;
    status = Util::writeAll(connfd, &arg, sizeof(int));
    // TODO: error check
    return status;
}

int Send_Buf_To_Proxy(int connfd, const void* buf, int size)
{
    int status;
    status = Util::writeAll(connfd, buf, size);
    // TODO: error check
    return status;
}

int exec_proxy_cmd(int pcmd)
{
  int answer = 0;
  // FIXME: We must handle the case where someone Finalizes before a checkpoint
  // this causes a blocking condition.  For now we'll simply introduce a sleep
  // into our test cases in order to bypass this, but this will be a blocker
  // in the future
  JTRACE("PLUGIN: Sending Proxy Command");
  if (Util::writeAll(PROTECTED_MPI_PROXY_FD, &pcmd, 4) < 0) {
    JNOTE("ERROR WRITING TO SOCKET")(JASSERT_ERRNO);
  }

  JTRACE("PLUGIN: Receiving Proxy Answer - ");
  if (Util::readAll(PROTECTED_MPI_PROXY_FD, &answer, 4) < 0) {
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


bool matching_buffered_packet(int source, int tag, MPI_Comm comm)
{
  std::vector<Message *>::iterator itt;
  for(itt = g_message_queue.begin(); itt != g_message_queue.end(); itt++)
  {
    Message * msg = *itt;
    if (((msg->status.MPI_SOURCE == source) | (source == MPI_ANY_SOURCE))
        && ((msg->status.MPI_TAG == tag) | (tag == MPI_ANY_TAG))
        && ((msg->comm == comm)))
    {
      return true;
    }
  }
  return false;
}

int mpi_plugin_is_packet_waiting(int source, int tag, MPI_Comm comm, int *flag,
                                  MPI_Status *mpi_status, int *wait_type)
{
  int status = 0;
  MPI_Status ignore;
  if (matching_buffered_packet(source, tag, comm))
  {
    *flag = true;
    *wait_type = MPI_PLUGIN_BUFFERED_PACKET_WAITING;
  }
  else
  {
    // MPI_Proxy_Iprobe
    // send command and arguments
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Iprobe);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, source);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, comm);

    // receive answer
    status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    if (status == 0)
    {
      *flag = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
      Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, mpi_status,
                              sizeof(MPI_Status));
    }
    // Return the correct wait type (either proxy packet waiting, or no packet)
    if (*flag)
      *wait_type = MPI_PLUGIN_PROXY_PACKET_WAITING;
    else
      *wait_type = MPI_PLUGIN_NO_PACKET_WAITING;
  }

  return status;
}

int mpi_plugin_return_buffered_packet(void *buf, int count, int datatype,
                                      int source, int tag, MPI_Comm comm,
                                      MPI_Status *mpi_status, int size)
{
  int status = -1; // FIXME
  int cpysize;
  int element = 0;
  std::vector<Message *>::iterator itt;
  Message * msg;
  for(itt = g_message_queue.begin(); itt != g_message_queue.end(); itt++)
  {
    msg = *itt;
    if (((msg->status.MPI_SOURCE == source) | (source == MPI_ANY_SOURCE))
        && ((msg->status.MPI_TAG == tag) | (tag == MPI_ANY_TAG))
        && ((msg->comm == comm)))
    {
      break;
    }
    element++;
  }
  if (itt == g_message_queue.end())
  {
    // this should never happen!
    return -1;
  }

  cpysize = (size < msg->size) ? size: msg->size;
  memcpy(buf, msg->buf, cpysize);
  g_message_queue.erase(g_message_queue.begin()+element);
  free(msg->buf);
  free(msg);
  // TODO: Actually handle MPI_Status that isn't MPI_STATUS_IGNORE

  return MPI_SUCCESS;
}


EXTERNC int
MPI_Init(int *argc, char ***argv)
{
  int status = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JTRACE("PLUGIN: MPI_Init!\n");
  status = exec_proxy_cmd(MPIProxy_Cmd_Init);
  DMTCP_PLUGIN_ENABLE_CKPT();
  // get our rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &g_world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &g_world_size);
  return status;
}

EXTERNC int
MPI_Comm_size(int group, int *world_size)
{
  int status = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Get_CommSize);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, group);
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (!status) // success
    *world_size = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);

  DMTCP_PLUGIN_ENABLE_CKPT();
  return status;
}

EXTERNC int
MPI_Comm_rank(int group, int *world_rank)
{
  int status = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Get_CommRank);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, group);
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (status == MPI_SUCCESS) {
    *world_rank = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    JTRACE("*** GOT RANK\n");
    g_world_rank = *world_rank;
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return status;
}

EXTERNC int
MPI_Type_size(MPI_Datatype datatype, int *size)
{
  int status = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Type_size);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, datatype);

  // get the status
  status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (status == MPI_SUCCESS) // if successful, ge the size
    *size = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  DMTCP_PLUGIN_ENABLE_CKPT();

  return status;
}

EXTERNC int
MPI_Get_count(const MPI_Status *status, MPI_Datatype datatype, int *count)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Get_count);
  Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, status, sizeof(*status));
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPI_BYTE);
  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  *count = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

// Blocking Call must be handled safely
EXTERNC int
MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest, int tag,
          MPI_Comm comm)
{
  int status = 0xFFFFFFFF;
  int size = 0;

  status = MPI_Type_size(datatype, &size);

  DMTCP_PLUGIN_DISABLE_CKPT();
  g_pending_send = true;
  g_restart_receive = false;
  g_restart_retval = 0;
  if (status == MPI_SUCCESS)
  {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Send);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count * size);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, buf, count*size);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, dest);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    // Get the status
  }
  g_local_sent++;
  DMTCP_PLUGIN_ENABLE_CKPT();

  // Block *safely* until we receive the status back
  // this _Safely call handles the situation where a ckpt/restart
  // occurs during the process of waiting for the Recv to occur
  status = Complete_Blocking_Call_Safely(PROTECTED_MPI_PROXY_FD,
                                          &g_pending_send);

  return status;
}

EXTERNC int
MPI_Isend(const void *buf, int count, MPI_Datatype datatype, int dest, int tag,
          MPI_Comm comm, MPI_Request* request)
{
  int retval = 0xFFFFFFFF;
  int size = 0;
  Async_Message* message;

  retval = MPI_Type_size(datatype, &size);

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (retval == MPI_SUCCESS)
  {
    // TODO: Only need to do this if it's a stack pointer?
    // if this is a heap pointer, we can share the malloc'd page

    // cache this message parameters for handling during checkpoint
    // so that future Test/Wait calls can be handled appropriately
    message = (Async_Message*)malloc(sizeof(Async_Message));
    g_async_messages[request] = message;
    message->serviced = false;
    message->type = ISEND_REQUEST;
    message->sendbuf = buf;
    message->count = count;
    message->datatype = datatype;
    message->size = count * size;
    message->remote_node = dest;
    message->tag = tag;
    message->comm = comm;
    message->request = request;
    message->flag = false;
    memset(&message->status, 0, sizeof(MPI_Status));

    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Isend);

    // Send buf to proxy
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count * size);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, buf, count*size);

    // send rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, dest);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, request, sizeof(MPI_Request));

    // Get the status
    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);

    // TODO: Handle fail case?

    // Get the Request Info
    Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                            request,
                            sizeof(MPI_Request));
  }
  g_local_sent++;
  DMTCP_PLUGIN_ENABLE_CKPT();

  return retval;
}

EXTERNC int
MPI_Test(MPI_Request* request, int* flag, MPI_Status* status)
{
  int retval = 0;
  char* rbuf = NULL;
  int size = 0;
  Async_Message* message = g_async_messages[request];

  DMTCP_PLUGIN_DISABLE_CKPT();

  if (message->serviced) // cached result)
  {
    // if cached, buffer should already be populated on drain
    // TODO: Actually handle MPI_Status
    // memcpy(status, &message->status, sizeof(MPI_Status));
    *flag = message->flag;
  }
  else // not serviced during checkpoint/restart - check for result on proxy
  {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Test);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, request, sizeof(MPI_Request));
    // TODO handle an actual status
    // Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF);

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    if (retval == 0)
    {
      Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                              request,
                              sizeof(MPI_Request));
      *flag = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
      // TODO: Handle MPI_Status correctly

      // Drain if it is complete and this was Irecv
      if (*flag && message->type == IRECV_REQUEST) // drain is ready!
      {
        Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                message->recvbuf,
                                message->size);
      }
    }
    // TODO: else - error handling?
  }
  // if flag is set, the async message has completed and we may delete it from
  // our g_async_messages cache
  if (*flag)
  {
    g_async_messages.erase(request);
    free(message);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return retval;
}

int irecv_wait(MPI_Request* request, MPI_Status* status)
{
  int retval = 0;
  int flag = 0;
  // Simple Recv wait solution: Spin on MPI_Test

  // TODO: this irecv may have been serviced during a checkpoint,
  // we need to check the receive cache here to see if that has been
  // drained already
  Async_Message* message = g_async_messages[request];
  while (!flag)
  {
    DMTCP_PLUGIN_DISABLE_CKPT();

    if (message->serviced) // message has been drained
    {
      if (status != MPI_STATUS_IGNORE)
        memcpy(status, &message->status, sizeof(MPI_Status));
      // clean up our message queue
      flag = 1;
    }
    else // message has not been drained, do a test
    {
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Test);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, *message->request);
      // FIXME: handle actual MPI_Status.  Defautl to IGNORE for now
      // Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF);

      retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
      if (retval == 0)
      {
        *message->request = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
        flag = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
        // Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
        //                        status,
        //                        sizeof(MPI_Status));

        if (flag == 1) // drain is ready!
        {
          Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  message->recvbuf,
                                  message->size);
          g_local_recv++;
        }
      }
    }
    // give ourselves a moment to checkpoint while spinning
    DMTCP_PLUGIN_ENABLE_CKPT();
  }

  return 0;
}

int isend_wait(MPI_Request* request, MPI_Status* status)
{
  int retval = 0;
  int flags = 0;
  int sockstat = EWOULDBLOCK;
  Async_Message* message = g_async_messages[request];

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (message->serviced)
  {
    // This Isend has already been serviced before the call to wait
    // due to a checkpoint occuring
    DMTCP_PLUGIN_ENABLE_CKPT();
    return 0;
  }
  else
  {
    // Send Wait request
    g_pending_wait = true;
    g_restart_receive = false;
    g_restart_retval = 0;
    g_pending_wait_request = request;
    g_pending_wait_status = status;
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Wait);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, request, sizeof(MPI_Request));

    // FIXME: handle actual MPI_Status value.  Default to STATUS_IGNORE for now
    // Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, status, sizeof(MPI_Status));
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  // We can induce a sleep here to for an in-flight Wait on an Isend
  // for testing Checkpoint/Restart
  // sleep(4);

  // Block *safely* until we receive the status back
  // handle the ckpt/restart case during this blocking call gracefully
  while (sockstat == EWOULDBLOCK && !g_restart_receive)
  {
    DMTCP_PLUGIN_DISABLE_CKPT();
    if (g_restart_receive)
    {
      DMTCP_PLUGIN_ENABLE_CKPT();
      break;
    }
    flags = fcntl(PROTECTED_MPI_PROXY_FD, F_GETFL, 0);
    fcntl(PROTECTED_MPI_PROXY_FD, F_SETFL, flags | O_NONBLOCK);
    sockstat = Util::readAll(PROTECTED_MPI_PROXY_FD, &retval, sizeof(int));
    fcntl(PROTECTED_MPI_PROXY_FD, F_SETFL, flags);
    if (sockstat != EWOULDBLOCK)
    {
      // we waited successfully, get the rest
      // this must be done before re-enabling checkpoint
      Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                              request,
                              sizeof(MPI_Request));
      // FIXME: handle actual MPI_Status values
      // Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
      //                        status,
      //                        sizeof(MPI_Status));
      g_pending_wait = false;
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
  }

  if (g_restart_receive)
    retval = g_restart_retval;

  return retval;
}

// Blocking Call must be handled safely
EXTERNC int
MPI_Wait(MPI_Request* request, MPI_Status* status)
{
  int retval = 0;
  Async_Message* message = g_async_messages[request];
  // FIXME: handle fail case of request not in map
  // dispatch to irecv/isend_wait
  switch(message->type)
  {
    case ISEND_REQUEST:
      retval = isend_wait(request, status);
      break;
    case IRECV_REQUEST:
      retval = irecv_wait(request, status);
      break;
    default:
      // UNKNOWN TYPE?!
      break;
  }
  // Now that this request is serviced, we may delete it from the async cache
  g_async_messages.erase(request);
  free(message);

  return retval;
}

/*
  FIXME: https://stackoverflow.com/questions/22410827/mpi-reuse-mpi-request

  It's just fine to reuse your MPI_Request objects as long as they're completed
  before you use them again (either by completing the request or freeing the
  request object manually using MPI_REQUEST_FREE).

  Variables of type MPI_Request are not request objects themselves but rather
  just opaque handles (something like an abstract pointer) to the real MPI
  request objects. Assigning a new value to such a variable in no way affects
  the MPI object and only breaks the association to it. Therefore the object
  might become inaccessible in the sense that if no handle to it exists in your
  program, it can no longer be passed to MPI calls. This is the same as losing a
  pointer to a dynamically allocated memory block, thus leaking it.

  When it comes to asynchronous request handles, once the operation is completed
  MPI destroys the request object and MPI_Wait* / MPI_Test* set the passed
  handle variable to MPI_REQUEST_NULL on return. Also, a call to
  MPI_Request_free will mark the request for deletion and set the handle to
  MPI_REQUEST_NULL on return. At that point you can reuse the variable and store
  a different request handle in it.

  The same applies to handles to communicators (of type MPI_Comm), handles to
  datatypes (of type MPI_Datatype), handles to reduce operations (of type
  MPI_Op), and so on.
*/

EXTERNC int
MPI_Iprobe(int source, int tag, MPI_Comm comm, int *flag,
            MPI_Status *status)
{
  int retval = 0;
  int wait_type = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  retval = mpi_plugin_is_packet_waiting(source, tag, comm, flag,
                                        status, &wait_type);
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Probe(int source, int tag, MPI_Comm comm, MPI_Status *status)
{
  int retval = 0;
  while (true)
  {
    int flag = 0;
    retval = MPI_Iprobe(source, tag, comm, &flag, status);
    if (flag)
      break;
    // sleep(1);
  }
  return retval;
}

EXTERNC int
MPI_Recv(void *buf, int count, MPI_Datatype datatype, int source, int tag,
          MPI_Comm comm, MPI_Status *status)
{
  int retval = 0xFFFFFFFF;
  int size = 0;
  bool done = false;
  MPI_Status iprobe_mstat;

  // calculate total size of expected message before we do anything
  retval = MPI_Type_size(datatype, &size);
  size = size * count;

  while (true)  // loop until we receive a packet
  {
    int flag = 0;
    int wait_type = MPI_PLUGIN_NO_PACKET_WAITING;

    // during this critical section we must disable checkpointing
    DMTCP_PLUGIN_DISABLE_CKPT();
    retval = mpi_plugin_is_packet_waiting(source, tag, comm, &flag,
                                          &iprobe_mstat, &wait_type);
    if (flag && wait_type == MPI_PLUGIN_BUFFERED_PACKET_WAITING)
    {
      // drain from plugin to rank buffer
      retval = mpi_plugin_return_buffered_packet(buf, count, datatype, source,
                                                tag, comm, status, size);
      done = true;
    }
    else if (flag && wait_type == MPI_PLUGIN_PROXY_PACKET_WAITING)
    {
      // drain from proxy to rank buffer
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Recv);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, source);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
      if (status == MPI_STATUS_IGNORE)
        Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF);
      else
        Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0x0);

      retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
      if (retval == 0)
      {
        Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, buf, size);
        if (status != MPI_STATUS_IGNORE)
          Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  status,
                                  sizeof(*status));
      }
      done = true;
      g_local_recv++;
      g_world_recv++;
    }
    else
    {
      // nothing to do if no packets waiting?
    }
    // no packet waiting, allow a moment to sleep in case we need to checkpoint
    DMTCP_PLUGIN_ENABLE_CKPT();
    if (done)
      break;
  }
  return retval;
}

EXTERNC int
MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source, int tag,
          MPI_Comm comm, MPI_Request *request)
{
  int retval = 0xFFFFFFFF;
  int size = 0;
  bool done = false;
  Async_Message* message = NULL;

  // calculate total size of expected message before we do anything
  retval = MPI_Type_size(datatype, &size);
  size = size * count;

  // during this critical section we must disable checkpointing
  DMTCP_PLUGIN_DISABLE_CKPT();

  // Save all of this info to our replay buffer for after a restart
  // occurs.  We need to do this for any Irecv calls that remain
  // unserviced after
  message = (Async_Message*) malloc(sizeof(Async_Message));
  message->serviced = false;
  message->type = IRECV_REQUEST;
  message->recvbuf = buf;
  message->count = count;
  message->datatype = datatype;
  message->remote_node = source;
  message->tag = tag;
  message->comm = comm;
  message->request = request;
  message->size = size;
  // make sure we set the output parameters to known values
  message->flag = 0;
  memset(&message->status, 0, sizeof(MPI_Status)); // FIXME: is this correct?

  // TODO Check drained message cache for a cached message
  // if it's cached we can simply do the copy and set our message
  // to serviced for quick handling during the next test/wait
  if (matching_buffered_packet(source, tag, comm))
  {
    // copy stuff
    // set to serviced
    message->serviced = true;
    message->flag = 1;
    *message->request = DRAINED_REQUEST_VALUE;
    mpi_plugin_return_buffered_packet(buf,
                                      count,
                                      datatype,
                                      source,
                                      tag,
                                      comm,
                                      &message->status,
                                      size);
    retval = 0;
    g_async_messages[request] = message;
  }
  else // queue it up on the proxy
  {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Irecv);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, source);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, tag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, request, sizeof(MPI_Request));

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    if (retval == 0)
    {
      Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, request,
                              sizeof(MPI_Request));
      // save the address of request
      message->request = request;
      g_async_messages[request] = message;
    }
    else
    {
      // TODO: any error checking related stuff?
      // don't leak memory, clean up and don't store this if it fails
      free(message);
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return retval;
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

EXTERNC int
MPI_Bcast(void *buffer, int count, MPI_Datatype datatype, int root,
          MPI_Comm comm)
{
  int retval = 0;
  int size = 0;
  int status = MPI_Type_size(datatype, &size);

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (status == MPI_SUCCESS) {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Bcast);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count * size);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, buffer, count*size);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, root);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    // Get the status
    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
    JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                    buffer, count * size)
                                    == count * size)
            (count)(size).Text("Received fewer bytes than expected");
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Abort(MPI_Comm comm, int errorcode)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Abort);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, errorcode);

  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status

  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Barrier(MPI_Comm comm)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Barrier);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
              MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)
{
  int retval = 0;
  int size = 0;
  int status = MPI_Type_size(datatype, &size);

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (status == MPI_SUCCESS) {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Allreduce);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count * size);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, sendbuf, count*size);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)op);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
    JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, recvbuf,
                                    count * size)
                                    == count * size)
            (count)(size).Text("Received fewer bytes than expected");
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Reduce(const void *sendbuf, void *recvbuf, int count,
           MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm)
{
  int retval = 0;
  int size = 0;
  int status = MPI_Type_size(datatype, &size);

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (status == MPI_SUCCESS) {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Reduce);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count * size);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, sendbuf, count*size);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)datatype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)op);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, root);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
    JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                    recvbuf, count * size)
                                    == count * size)
            (count)(size).Text("Received fewer bytes than expected");
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Alltoall(const void *sendbuf, int sendcount,
             MPI_Datatype sendtype, void *recvbuf, int recvcount,
             MPI_Datatype recvtype, MPI_Comm comm)
{
  int retval = 0;
  int sendsize = 0;
  int recvsize = 0;
  int commsize = 0;
  int sendstatus = MPI_Type_size(sendtype, &sendsize);
  int recvstatus = MPI_Type_size(recvtype, &recvsize);
  int comsstatus = MPI_Comm_size(comm, &commsize);

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (sendsize && recvsize && commsize) {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Alltoall);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, sendcount * sendsize * commsize);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, recvcount * recvsize * commsize);

    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, sendbuf,
                      sendcount * sendsize * commsize);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, sendcount);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)sendtype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, recvcount);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)recvtype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
    JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                    recvbuf, recvcount * recvsize * commsize)
                                    == recvcount * recvsize * commsize)
             (recvcount)(recvsize).Text("Received fewer bytes than expected");
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Alltoallv(const void *sendbuf, const int sendcounts[],
              const int sdispls[], MPI_Datatype sendtype,
              void *recvbuf, const int recvcounts[],
              const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm)
{
  int retval = 0;
  int sendsize = 0;
  int recvsize = 0;
  int commsize = 0;
  int sendstatus = MPI_Type_size(sendtype, &sendsize);
  int recvstatus = MPI_Type_size(recvtype, &recvsize);
  int comsstatus = MPI_Comm_size(comm, &commsize);

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (sendsize && recvsize && commsize) {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Alltoallv);

    // Buf part
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD,
                      commsize * sizeof (sendcounts[0]));
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD,
                      commsize * sizeof (recvcounts[0]));

    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, sendcounts,
                      commsize * sizeof (sendcounts[0]));

    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, recvcounts,
                      commsize * sizeof (recvcounts[0]));

    int total_send = sum(sendcounts, commsize);
    int total_recv = sum(recvcounts, commsize);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, total_send);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, total_recv);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)sendtype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)recvtype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);

    for (int i = 0; i < commsize; i++) {
      if (sendcounts[i] != 0) {
        Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD,
                      (const void*)((uintptr_t)sendbuf + sendsize * sdispls[i]),
                      sendsize * sendcounts[i]);
      }
    }

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
    for (int i = 0; i < commsize; i++) {
      JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                            (void*)((uintptr_t)recvbuf + recvsize * rdispls[i]),
                            recvsize * recvcounts[i]) ==
                            recvsize * recvcounts[i])
          (recvcounts[i])(recvsize).Text("Received fewer bytes than expected");
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Comm_split(MPI_Comm comm, int color, int key, MPI_Comm *newcomm)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Comm_split);

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, color);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, key);

  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  newcomm,
                                  sizeof(*newcomm)) == sizeof(*newcomm))
          (*newcomm)
          (sizeof(*newcomm)).Text("Received fewer bytes than expected");

  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC double
MPI_Wtime(void)
{
  double retval = 0.0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Wtime);

  JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  &retval,
                                  sizeof(retval)) == sizeof(retval))
          (retval).Text("Received fewer bytes than expected");

  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Comm_dup(MPI_Comm comm, MPI_Comm *newcomm)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Comm_dup);

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  newcomm,
                                  sizeof(*newcomm)) == sizeof(*newcomm))
          (*newcomm)
          (sizeof(*newcomm)).Text("Received fewer bytes than expected");

  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Comm_group(MPI_Comm comm, MPI_Group *group)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Comm_group);

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  group,
                                  sizeof(*group)) == sizeof(*group))
          (*group)
          (sizeof(*group)).Text("Received fewer bytes than expected");
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Comm_create(MPI_Comm comm, MPI_Group group, MPI_Comm *newcomm)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Comm_create);

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
  Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, &group, sizeof(group));
  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  newcomm,
                                  sizeof(*newcomm)) == sizeof(*newcomm))
          (*newcomm)
          (sizeof(*newcomm)).Text("Received fewer bytes than expected");
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Sendrecv(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
             int dest, int sendtag, void *recvbuf, int recvcount,
             MPI_Datatype recvtype, int source, int recvtag, MPI_Comm comm,
             MPI_Status *status)
{
  int retval = 0;
  int sendsize = 0;
  int recvsize = 0;
  int commsize = 0;
  int sendstatus = MPI_Type_size(sendtype, &sendsize);
  int recvstatus = MPI_Type_size(recvtype, &recvsize);
  DMTCP_PLUGIN_DISABLE_CKPT();

  if (sendsize && recvsize) {
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Sendrecv);

    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, sendcount * sendsize);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, recvcount * recvsize);
    // Buf part

    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, sendbuf,
                      sendcount * sendsize);

    // rest of stuff
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, sendcount);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)sendtype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, dest);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)sendtag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, recvcount);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)recvtype);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, source);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)recvtag);
    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
    if (status == MPI_STATUS_IGNORE) {
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF);
    } else {
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0x0);
    }

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
    if (retval == 0) {
      JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  recvbuf, recvcount * recvsize)
                                  == recvcount * recvsize)
           (recvcount)(recvsize).Text("Received fewer bytes than expected");

      if (status != MPI_STATUS_IGNORE)
        Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                status,
                                sizeof(MPI_Status));
    }
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

EXTERNC int
MPI_Waitall(int count, MPI_Request array_of_requests[],
            MPI_Status *array_of_statuses)
{
  int retval = 0;
  DMTCP_PLUGIN_DISABLE_CKPT();
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Waitall);

  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
  Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, array_of_requests,
                    count * sizeof(MPI_Request));

  retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // status
  if (retval == 0)
  {
    JWARNING(Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                array_of_statuses, sizeof(MPI_Status) * count)
                                == sizeof(MPI_Status) * count)
         (count)(sizeof(MPI_Status)).Text("Received fewer bytes than expected");
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
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
  o << baseDir << "/ckpt_rank_" << g_world_rank;
  dmtcp_set_ckpt_dir(o.str().c_str());
}

static bool drain_packet()
{
  void *buf;
  int size = 0;
  int count = 0;
  int flag = 0;
  int source = 0;
  int tag = 0;
  int iprobe_status = 0;
  int get_count_status = 0;
  int receive_status = 0;
  MPI_Datatype datatype;
  MPI_Comm comm = MPI_COMM_WORLD; // FIXME - other comms?
  MPI_Status status;
  Message *message;

  // Probe for waiting packet
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Iprobe);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPI_ANY_SOURCE);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPI_ANY_TAG);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int) MPI_COMM_WORLD);

  // get probe resules
  iprobe_status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  if (iprobe_status == 0)
  {
    flag = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    // FIXME: actually handle a status
    Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                              &status,
                              sizeof(MPI_Status));
  }

  if (!flag)
    return false;

  // There's a packet waiting for us
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Get_count);
  Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, &status, sizeof(MPI_Status));
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPI_BYTE);

  get_count_status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  // Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
  //                          &status,
  //                          sizeof(MPI_Status));
  count = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);

  // Get Type_size info
  // FIXME: get actual type size
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Type_size);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)MPI_BYTE);
  Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
  size = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);

  // allocate our receive buffer
  size = count * size;
  buf = malloc(size); // maximum of 65535 ints

  // drain from proxy to plugin buffer
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Recv);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, count);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPI_BYTE); // FIXME: actual datatype
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, status.MPI_SOURCE);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, status.MPI_TAG);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)comm);
  Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF); // Ignore FIXME

  receive_status = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);

  if (receive_status == 0)
    Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, buf, size);

  // copy all data into local message
  message = (Message *)malloc(sizeof(Message));
  message->buf        = buf;
  message->count      = count;
  message->datatype   = MPI_BYTE;
  message->comm       = comm;
  message->status.count_lo    = status.count_lo;
  message->status.count_hi_and_cancelled = status.count_hi_and_cancelled;
  message->status.MPI_SOURCE  = status.MPI_SOURCE;
  message->status.MPI_TAG     = status.MPI_TAG;
  message->status.MPI_ERROR   = status.MPI_ERROR;
  message->size       = size;

  // queue it
  g_message_queue.push_back(message);
  g_local_recv++;

  return true;
}

struct keyVal {
  int typerank;
  uint32_t value;
} mystruct, mystruct_other;

int HIGHBIT = 1 << ((sizeof(mystruct.typerank)*8)-1);
uint32_t sizeofval = sizeof(mystruct_other.value);

static void
pre_ckpt_register_data()
{
  // publish my keys and values
  mystruct.typerank = g_world_rank;
  mystruct.value = g_local_sent;
  dmtcp_send_key_val_pair_to_coordinator("mpi-proxy",
                                          &(mystruct.typerank),
                                          sizeof(mystruct.typerank),
                                          &(mystruct.value),
                                          sizeof(mystruct.value));
  mystruct.typerank |= HIGHBIT;
  mystruct.value = g_local_recv;
  dmtcp_send_key_val_pair_to_coordinator("mpi-proxy",
                                          &(mystruct.typerank),
                                          sizeof(mystruct.typerank),
                                          &(mystruct.value),
                                          sizeof(mystruct.value));
  return;
}

static void
get_packets_sent()
{
  int i = 0;
  g_world_sent = g_local_sent;
  for (i = 0; i < g_world_size; i++)
  {
    if (i == g_world_rank)
      continue;
    // get number of sent packets by this proxy
    mystruct_other.typerank = i;
    mystruct_other.value = 0;
    dmtcp_send_query_to_coordinator("mpi-proxy",
                                      &(mystruct_other.typerank),
                                      sizeof(mystruct_other.typerank),
                                      &(mystruct_other.value),
                                      &sizeofval);
    g_world_sent += mystruct_other.value;
  }
}

static void
get_packets_recv()
{
  int i = 0;
  g_world_recv = 0;
  // get everyone elses keys and values
  for (i = 0; i < g_world_size; i++)
  {
    if (i == g_world_rank)
    {
      g_world_recv += g_local_recv;
      continue;
    }

    // get number of received packets by this proxy
    mystruct_other.typerank = (i | HIGHBIT);
    mystruct_other.value = 0;
    dmtcp_send_query_to_coordinator("mpi-proxy",
                                      &(mystruct_other.typerank),
                                      sizeof(mystruct_other.typerank),
                                      &(mystruct_other.value),
                                      &sizeofval);
    g_world_recv += mystruct_other.value;
  }
}

static void
publish_packets_recv()
{
  mystruct.typerank = g_world_rank | HIGHBIT;
  mystruct.value = g_local_recv;
  dmtcp_send_key_val_pair_to_coordinator("mpi-proxy",
                                          &(mystruct.typerank),
                                          sizeof(mystruct.typerank),
                                          &(mystruct.value),
                                          sizeof(mystruct.value));
}

static void
complete_blocking_call()
{
  if (g_pending_send)
  {
    g_restart_retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    g_restart_receive = true;
    g_pending_send = false;
  }
  else if (g_pending_wait)
  {
    g_restart_retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
    g_restart_receive = true;
    Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, g_pending_wait_request,
                            sizeof(MPI_Request));
    //  Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, g_pending_wait_status,
    //                        sizeof(MPI_Status));
    g_pending_wait = false;
  }
}

static bool
resolve_async_messages()
{
  int unserviced_isends = 0;
  MPI_Request* request;
  Async_Message* message;
  std::map<MPI_Request*, Async_Message*>::iterator it;

  for (it = g_async_messages.begin(); it != g_async_messages.end(); it++)
  {
    MPI_Status status;
    int retval = 0;
    int flag = 0;
    request = it->first;
    message = it->second;

    if (message->serviced)
      continue;

    Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Test);
    Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, request, sizeof(MPI_Request));
    // Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, 0xFFFFFFFF); // STATUS_IGNORE

    retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD); // return value
    if (retval == MPI_SUCCESS)
    {
      Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                              message->request,
                              sizeof(MPI_Request));
      message->flag = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
      // FIXME: handle actual MPI_Status
      // this will probably end up being MPI_STATUS_IGNORE
      // Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
      //                        &message->status,
      //                        sizeof(MPI_Status));
      if (message->flag)
      {
        // this information also needs to be cached for when the application
        // finally does its own MPI_Test or MPI_Wait on the given MPI_Request
        message->serviced = true;
        if (message->type == IRECV_REQUEST)
        {
          Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD,
                                  message->recvbuf,
                                  message->size);
          g_local_recv++;
          g_world_recv++;
        }
        else
        {
          dpf("serviced ISEND");
        }
      }
      else
      {
        if (message->type == ISEND_REQUEST)
        {
          dpf("unserivced ISEND");
          unserviced_isends++;
        }
        else
        {
          dpf("unserviced IRECV");
        }
      }
    } // TODO: retval = failure?
    else
    {
      printf("MPI call failed? %d message type\n", message->type);
      fflush(stdout);
    }
  }
  return unserviced_isends != 0;
}

static void
pre_ckpt_drain_data_from_proxy()
{
  bool unserviced_isends = true;
  get_packets_sent();
  get_packets_recv();

  complete_blocking_call();

  while (g_world_sent != g_world_recv || unserviced_isends)
  {
    unserviced_isends = resolve_async_messages();
    drain_packet();
    // we have to call this every time to get up to date numbers
    // of all the proxies, since we're not the only one waiting
    publish_packets_recv();
    get_packets_recv();
  }

  // on restart, we want to start totally fresh, since everything
  // is sent and received, these numbers are now totally irrelevant
  g_world_sent = 0;
  g_world_recv = 0;
  g_local_sent = 0;
  g_local_recv = 0;
}

static void
pre_restart_replay()
{
  MPI_Request* request;
  Async_Message* message;
  std::map<MPI_Request*, Async_Message*>::iterator it;

  for (it = g_async_messages.begin(); it != g_async_messages.end(); it++)
  {
    MPI_Status status;
    int retval = 0;
    int flag = 0;
    request = it->first;
    message = it->second;

    if (message->serviced)
      continue;

    if (message->type == IRECV_REQUEST)
    {
      // Queue up the request
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPIProxy_Cmd_Irecv);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, message->count);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)message->datatype);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, message->remote_node);
      Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, message->tag);
      {
        // FIXME: Translate MPI_Comm's
        // Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, (int)message->comm);
        Send_Int_To_Proxy(PROTECTED_MPI_PROXY_FD, MPI_COMM_WORLD);
      }
      Send_Buf_To_Proxy(PROTECTED_MPI_PROXY_FD, request, sizeof(MPI_Request));

      // Get the new MPI_Request handle and overwrite the old one
      retval = Receive_Int_From_Proxy(PROTECTED_MPI_PROXY_FD);
      if (retval == 0)
      {
        Receive_Buf_From_Proxy(PROTECTED_MPI_PROXY_FD, message->request,
                                sizeof(MPI_Request));
      }
      else // MASSIVE ERROR - need to die
      {
        exit(1);
      }
    }
    else // TODO: others?
    {
      continue;
    }
  }
  return;
}

static DmtcpBarrier mpiPluginBarriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt_register_data,
    "Drain-Data-From-Proxy" },
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt_drain_data_from_proxy,
    "Drain-Data-From-Proxy" },
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, pre_ckpt_update_ckpt_dir,
    "update-ckpt-dir-by-rank" },
  { DMTCP_GLOBAL_BARRIER_RESTART, pre_restart_replay,
    "replay-async-receives" },
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
