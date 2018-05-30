/* CopyLeft Gregory Price (2017) */

#include <dirent.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <mpi.h>
#include <errno.h>
#include <map>
#include <vector>
#include <algorithm>

#include "mpi_proxy.h"
#include "protectedfds.h"

// #define DEBUG_PRINTS

int g_commrank = 0;
int listfd = 0;

std::vector<MPI_Request *>g_request_queue;
std::map<MPI_Request, void *> g_hanging_isend;
std::map<MPI_Request, void *> g_hanging_irecv;
std::map<MPI_Request, int> g_hanging_irecv_size;

int serial_printf(const char * msg)
{
#ifdef DEBUG_PRINTS
  printf("proxy %d - %s\n", g_commrank, msg);
  fflush(stdout);
#endif
}

// Fails or does entire write (returns count)
static ssize_t
writeAll(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *)buf;
  size_t num_written = 0;

  do {
    ssize_t rc = write(fd, ptr + num_written, count - num_written);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } else {
        return rc;
      }
    } else if (rc == 0) {
      break;
    } else { // else rc > 0
      num_written += rc;
    }
  } while (num_written < count);
  return num_written;
}

// Fails, succeeds, or partial read due to EOF (returns num read)
// return value:
// -1: unrecoverable error
// <n>: number of bytes read
static ssize_t
readAll(int fd, void *buf, size_t count)
{
  ssize_t rc;
  char *ptr = (char *)buf;
  size_t num_read = 0;

  for (num_read = 0; num_read < count;) {
    rc = read(fd, ptr + num_read, count - num_read);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } else {
        return -1;
      }
    } else if (rc == 0) {
      break;
    } else { // else rc > 0
      num_read += rc;
    }
  }
  return num_read;
}

int MPIProxy_Receive_Arg_Int(int connfd)
{
  int retval;
  int status;
  status = readAll(connfd, &retval, sizeof(int));
  // TODO: error check
  return retval;
}

int MPIProxy_Send_Arg_Int(int connfd, int arg)
{
  int status = writeAll(connfd, &arg, sizeof(int));
  // TODO: error check
  return status;
}

int MPIProxy_Receive_Arg_Buf(int connfd, void *buf, int size)
{
  int received = 0;
  while (received < size)
    received += readAll(connfd, ((char *)buf)+received, size-received);
  return size == received;
}

int MPIProxy_Send_Arg_Buf(int connfd, void *buf, int size)
{
  int status = writeAll(connfd, buf, size);
  // TODO: error check
  return status;
}

void MPIProxy_Return_Answer(int connfd, int answer)
{
#ifdef DEBUG_PRINTS
  printf("Returned %08x\n", answer);
  fflush(stdout);
#endif
  writeAll(connfd, &answer, 4);
  return;
}

void MPIProxy_Init(int connfd)
{
  // TODO: Get argc and argv
  serial_printf("PROXY: MPI_Init - ");
  MPIProxy_Return_Answer(connfd, MPI_Init(NULL, NULL));
}

void MPIProxy_Get_CommSize(int connfd)
{
  int group = 0;
  int commsize = 0;
  int retval = 0;
  group = MPIProxy_Receive_Arg_Int(connfd);
  retval = MPI_Comm_size(group, &commsize);
  MPIProxy_Return_Answer(connfd, retval);
  if (retval == MPI_SUCCESS)
    MPIProxy_Send_Arg_Int(connfd, commsize);
}

void MPIProxy_Get_CommRank(int connfd)
{
  int group = 0;
  int commrank = 0;
  int retval = 0;

  group = MPIProxy_Receive_Arg_Int(connfd);
  retval = MPI_Comm_rank(group, &commrank);
  g_commrank = commrank;
  MPIProxy_Return_Answer(connfd, retval);
  if (retval == MPI_SUCCESS)
    MPIProxy_Send_Arg_Int(connfd, commrank);
}

void MPIProxy_Type_size(int connfd)
{
  int retval = 0;
  int size = 0;
  MPI_Datatype datatype;
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);

  // Do the actual Type_size call
  retval = MPI_Type_size(datatype, &size);

  MPIProxy_Return_Answer(connfd, retval);
  if (retval == MPI_SUCCESS)
    MPIProxy_Send_Arg_Int(connfd, size);
}


void MPIProxy_Send(int connfd)
{
  int retval = 0;
  // void * buf = NULL;
  int size = 0;
  int count, dest, tag;
  int msgid;  // used to verify it was sent
  MPI_Datatype datatype;
  MPI_Comm comm;
  void * send_buf = NULL;

  // Collect the arguments
  size = MPIProxy_Receive_Arg_Int(connfd);

  // Buffer read
  send_buf = malloc(size);
  MPIProxy_Receive_Arg_Buf(connfd, send_buf, size);

  // rest of the arguments
  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);
  dest = MPIProxy_Receive_Arg_Int(connfd);
  tag = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  // Do the send
  retval = MPI_Send(send_buf, count, datatype, dest, tag, comm);

  if (retval != MPI_SUCCESS)
  {
    printf("Proxy - SEND FAILED\n");
    fflush(stdout);
  }

  free(send_buf);
  MPIProxy_Return_Answer(connfd, retval);
}

void MPIProxy_Isend(int connfd)
{
  int retval = 0;
  void * buf = NULL;
  int size = 0;
  int count, dest, tag;
  int msgid;  // used to verify it was sent
  int numread = 0;
  MPI_Datatype datatype;
  MPI_Comm comm;
  MPI_Request request;
  memset(&request, 0x0, sizeof(MPI_Request));

  // Collect the arguments
  size = MPIProxy_Receive_Arg_Int(connfd);

  // Buffer read
  buf = malloc(size);
  while (numread < size)
    numread += readAll(connfd, ((char *)buf)+numread, size-numread);

  // rest of the arguments
  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);
  dest = MPIProxy_Receive_Arg_Int(connfd);
  tag = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  MPIProxy_Receive_Arg_Buf(connfd, &request, sizeof(MPI_Request));

  // Do the send
  retval = MPI_Isend(buf, count, datatype, dest, tag, comm, &request);

  if (retval != MPI_SUCCESS)
  {
    printf("Proxy - SEND FAILED\n");
    fflush(stdout);
  }

  g_hanging_isend[request] = buf;

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &request, sizeof(MPI_Request));
}
// int MPI_Wait(MPI_Request *request, MPI_Status *status)
void MPIProxy_Wait(int connfd)
{
  int retval = 0;
  int isend_size = 0;
  void* isend_buf = NULL;
  std::map<MPI_Request, void*>::iterator it;
  MPI_Request request;
  MPI_Status status;

  // Wait is ONLY called for Send's in our design because
  // doing a blocking call on a Recv is going to introduce deadlocks
  MPIProxy_Receive_Arg_Buf(connfd, &request, sizeof(MPI_Request));

  // FIXME: handle actual MPI_Status values
  // MPIProxy_Receive_Arg_Buf(connfd, &status, sizeof(MPI_Status));

  // FIXME: handle actual MPI_Status values
  retval = MPI_Wait(&request, MPI_STATUS_IGNORE);

  it = g_hanging_isend.find(request);
  if (it != g_hanging_isend.end())
  {
    isend_buf = g_hanging_isend[request];
    g_hanging_isend.erase(request);
    free(isend_buf);
  } // FIXME: what if this is a wait for a request that doesn't exit?

  MPIProxy_Send_Arg_Int(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &request, sizeof(MPI_Request));

  // FIXME: handle actual MPI_Status values
  // MPIProxy_Send_Arg_Buf(connfd, &status, sizeof(MPI_Status));
}

void MPIProxy_Test(int connfd)
{
  int retval;
  MPI_Request request;
  MPI_Request request_copy;
  int flag = 0;
  void * irecv_buf;
  int irecv_size;
  std::map<MPI_Request, void*>::iterator it;

  MPIProxy_Receive_Arg_Buf(connfd, &request, sizeof(MPI_Request));
  request_copy = request; // request gets destroyed if a Test is completed

  // TODO: handle real status
  // temp_status = MPIProxy_Receive_Arg_Int(connfd);
  // if (temp_status == 0xFFFFFFFF)

  retval = MPI_Test(&request, &flag, MPI_STATUS_IGNORE);

  MPIProxy_Send_Arg_Int(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &request, sizeof(MPI_Request));
  MPIProxy_Send_Arg_Int(connfd, flag);
  // MPIProxy_Send_Arg_Buf(connfd, &status, sizeof(MPI_Status));

  // if this was an irecv
  it = g_hanging_irecv.find(request_copy);
  if (flag && it != g_hanging_irecv.end())
  {
    // we just completed an irecv, drain the data to the plugin
    irecv_buf = g_hanging_irecv[request_copy];
    irecv_size = g_hanging_irecv_size[request_copy];
    MPIProxy_Send_Arg_Buf(connfd, irecv_buf, irecv_size);
    g_hanging_irecv.erase(request_copy);
    g_hanging_irecv_size.erase(request_copy);
  }
  else
  {
    // it was an isend
    g_hanging_isend.erase(request_copy);
  }
}


//  MPIProxy_Cmd_Bcast = 15,
//  MPIProxy_Cmd_Abort = 16,
//  MPIProxy_Cmd_Barrier = 17,
//  MPIProxy_Cmd_Reduce = 18,
//  MPIProxy_Cmd_AllReduce = 19,
//  MPIProxy_Cmd_AlltoAll = 20,
//  MPIProxy_Cmd_AlltoAllv = 21,
//  MPIProxy_Cmd_Comm_Split = 22,

// int
// MPI_Bcast(void *buffer, int count, MPI_Datatype datatype,
//          int root, MPI_Comm comm)
void MPIProxy_Bcast(int connfd)
{
  int retval = 0;
  // void * buf = NULL;
  int size = 0;
  int count;
  int root;
  MPI_Datatype datatype;
  MPI_Comm comm;
  void * send_buf = NULL;

  // Collect the arguments
  size = MPIProxy_Receive_Arg_Int(connfd);

  // Buffer read
  send_buf = malloc(size);
  MPIProxy_Receive_Arg_Buf(connfd, send_buf, size);

  // rest of the arguments
  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);
  root = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  // Do the send
  retval = MPI_Bcast(send_buf, count, datatype, root, comm);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - BCAST FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, send_buf, size);
  free(send_buf);
}

// int
// MPI_Abort(MPI_Comm comm, int errorcode)
void MPIProxy_Abort(int connfd)
{
  int retval = 0;
  int errcode;
  MPI_Comm comm;
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  errcode = MPIProxy_Receive_Arg_Int(connfd);
  // Do the send
  retval = MPI_Abort(comm, errcode);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - ABORT FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
}

// int
// MPI_Barrier(MPI_Comm comm)
void MPIProxy_Barrier(int connfd)
{
  int retval = 0;
  MPI_Comm comm;

  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  // Do the send
  retval = MPI_Barrier(comm);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - BARRIER FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
}

// int
// MPI_Reduce(const void *sendbuf, void *recvbuf, int count,
//           MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm)
void MPIProxy_Reduce(int connfd)
{
  int retval = 0;
  int size;
  int count;
  MPI_Datatype datatype;
  MPI_Op op;
  int root;
  MPI_Comm comm;
  char *send_buf = NULL;
  char *recv_buf = NULL;

  size = MPIProxy_Receive_Arg_Int(connfd);
  recv_buf = (char*)malloc(size);

  // Buffer read
  send_buf = (char*)malloc(size);
  MPIProxy_Receive_Arg_Buf(connfd, send_buf, size);

  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype)MPIProxy_Receive_Arg_Int(connfd);
  op = (MPI_Op)MPIProxy_Receive_Arg_Int(connfd);
  root = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  retval = MPI_Reduce(send_buf, recv_buf, count, datatype, op, root, comm);
  if (retval != MPI_SUCCESS) {
    printf("Proxy - REDUCE FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, recv_buf, size);
}

// int
// MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,
//           MPI_Datatype datatype, MPI_Op op, MPI_Comm comm)
void MPIProxy_Allreduce(int connfd)
{
  int retval = 0;
  int size;
  int count;
  MPI_Datatype datatype;
  MPI_Op op;
  MPI_Comm comm;
  char *send_buf = NULL;
  char *recv_buf = NULL;

  size = MPIProxy_Receive_Arg_Int(connfd);
  recv_buf = (char*)malloc(size);

  // Buffer read
  send_buf = (char*)malloc(size);
  MPIProxy_Receive_Arg_Buf(connfd, send_buf, size);

  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype)MPIProxy_Receive_Arg_Int(connfd);
  op = (MPI_Op)MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  retval = MPI_Allreduce(send_buf, recv_buf, count, datatype, op, comm);
  if (retval != MPI_SUCCESS) {
    printf("Proxy - REDUCE FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  // Send the result to all the nodes
  // TODO: Correct it
  MPIProxy_Send_Arg_Buf(connfd, recv_buf, size);
  free(recv_buf);
  free(send_buf);
}

// int
// MPI_Alltoall(const void *sendbuf, int sendcount,
//    MPI_Datatype sendtype, void *recvbuf, int recvcount,
//    MPI_Datatype recvtype, MPI_Comm comm)
void MPIProxy_Alltoall(int connfd)
{
  int retval = 0;
  int sendsize = 0;
  int recvsize = 0;
  int sendcount, recvcount;
  MPI_Datatype sendtype;
  MPI_Datatype recvtype;
  MPI_Op op;
  MPI_Comm comm;
  char *send_buf = NULL;
  char *recv_buf = NULL;

  sendsize = MPIProxy_Receive_Arg_Int(connfd);
  recvsize = MPIProxy_Receive_Arg_Int(connfd);

  recv_buf = (char*)malloc(recvsize);

  // Buffer read
  send_buf = (char*)malloc(sendsize);
  MPIProxy_Receive_Arg_Buf(connfd, send_buf, sendsize);

  sendcount = MPIProxy_Receive_Arg_Int(connfd);
  sendtype = (MPI_Datatype)MPIProxy_Receive_Arg_Int(connfd);
  recvcount = MPIProxy_Receive_Arg_Int(connfd);
  recvtype = (MPI_Datatype)MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  retval = MPI_Alltoall(send_buf, sendcount, sendtype,
                        recv_buf, recvcount, recvtype, comm);
  if (retval != MPI_SUCCESS) {
    printf("Proxy - REDUCE FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, recv_buf, recvsize);
}

// int
// MPI_Alltoallv(const void *sendbuf, const int sendcounts[],
//              const int sdispls[], MPI_Datatype sendtype,
//              void *recvbuf, const int recvcounts[],
//              const int rdispls[], MPI_Datatype recvtype, MPI_Comm comm)
void MPIProxy_Alltoallv(int connfd)
{
  int retval = 0;
  // TODO: Implement it
}

// int
// MPI_Comm_split(MPI_Comm comm, int color, int key, MPI_Comm *newcomm)
void MPIProxy_Comm_split(int connfd)
{
  int retval = 0;
  MPI_Comm comm;
  int color;
  int key;
  MPI_Comm newcomm;

  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  color = MPIProxy_Receive_Arg_Int(connfd);
  key = MPIProxy_Receive_Arg_Int(connfd);

  // Do the send
  retval = MPI_Comm_split(comm, color, key, &newcomm);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - BARRIER FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &newcomm, sizeof(newcomm));
}

// double
// MPI_Wtime(void)
void MPIProxy_Wtime(int connfd)
{
  double retval = 0.0;

  // Do the send
  retval = MPI_Wtime();

  MPIProxy_Send_Arg_Buf(connfd, &retval, sizeof(retval));
}


void MPIProxy_Recv(int connfd)
{
  int status = 0;
  void * buf;
  int size = 0;
  int count = 0;
  int source = 0;
  int tag = 0;
  MPI_Datatype datatype;
  MPI_Comm comm;
  int mpi_status_arg;
  MPI_Status *mpi_status;

  // Collect the arguments
  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);
  source = MPIProxy_Receive_Arg_Int(connfd);
  tag = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  mpi_status_arg = MPIProxy_Receive_Arg_Int(connfd);
  if (mpi_status_arg == 0xFFFFFFFF)
  {
    mpi_status = MPI_STATUS_IGNORE;
  }
  else
  {
    // TODO: ????
    // FIXME
    printf("NOT IGNORE STATUS!!!\n");
    fflush(stdout);
    mpi_status = NULL;
  }

  // Do the receive
  MPI_Type_size(datatype, &size);
  size = count * size;
  buf = malloc(size);
  memset(buf, 0, size);

  // TODO: Check that mpi_status is correctly used here
  serial_printf("PROXY: RECIEVING - ");
  status = MPI_Recv(buf, count, datatype, source, tag, comm, mpi_status);
  serial_printf("\tDONE");

  // Return receive's status
  MPIProxy_Return_Answer(connfd, status);
  if (status == MPI_SUCCESS)
  {
    MPIProxy_Send_Arg_Buf(connfd, buf, size);
    // TODO: Check that mpi_status is correctly used here
    if (mpi_status != MPI_STATUS_IGNORE)
    {
      printf("proxy returning not status ignore?!\n");
      MPIProxy_Send_Arg_Buf(connfd, &mpi_status, sizeof(mpi_status));
    }
  }

  free(buf);
}

void MPIProxy_Irecv(int connfd)
{
  int status = 0;
  void * buf;
  int size = 0;
  int count, source, tag;
  MPI_Datatype datatype;
  MPI_Comm comm;
  int mpi_status_arg;
  MPI_Request mpi_request;

  // Collect the arguments
  count = MPIProxy_Receive_Arg_Int(connfd);
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);
  source = MPIProxy_Receive_Arg_Int(connfd);
  tag = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  MPIProxy_Receive_Arg_Buf(connfd, &mpi_request, sizeof(MPI_Request));

  // Do the receive
  MPI_Type_size(datatype, &size);
  size = count * size;
  buf = malloc(size);


  // TODO: Check that mpi_status is correctly used here
  serial_printf("PROXY: RECIEVING - ");
  status = MPI_Irecv(buf, count, datatype, source, tag, comm, &mpi_request);
  serial_printf("\tDONE");

  g_hanging_irecv[mpi_request] = buf;
  g_hanging_irecv_size[mpi_request] = size;

  // Return receive's status
  MPIProxy_Return_Answer(connfd, status);
  if (status == MPI_SUCCESS)
  {
    MPIProxy_Send_Arg_Buf(connfd, &mpi_request, sizeof(MPI_Request));
  }
}

void MPIProxy_Iprobe(int connfd)
{
  int status = 0;
  int source = 0;
  int tag = 0;
  MPI_Comm comm;
  int flag = 0;
  MPI_Status mpi_status;

  // collect the arguments
  source = MPIProxy_Receive_Arg_Int(connfd);
  tag = MPIProxy_Receive_Arg_Int(connfd);
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);

  // do the Iprobe
  status = MPI_Iprobe(source, tag, comm, &flag, &mpi_status);

  // return the info
  // Return receive's status
  MPIProxy_Return_Answer(connfd, status);
  if (status == MPI_SUCCESS)
  {
    MPIProxy_Send_Arg_Int(connfd, flag);
    MPIProxy_Send_Arg_Buf(connfd, &mpi_status, sizeof(mpi_status));
    // FIXME: handle actual status
  }
}

void MPIProxy_Get_count(int connfd)
{
  int retval = 0;
  int count = 0;
  MPI_Status status;
  MPI_Datatype datatype;

  // Get the MPI_Status and Datatype
  MPIProxy_Receive_Arg_Buf(connfd, &status, sizeof(MPI_Status));
  datatype = (MPI_Datatype) MPIProxy_Receive_Arg_Int(connfd);

  // Do the Get_count
  retval = MPI_Get_count(&status, datatype, &count);
  MPIProxy_Return_Answer(connfd, retval);
  // MPIProxy_Send_Arg_Buf(connfd, &status, sizeof(MPI_Status));
  MPIProxy_Send_Arg_Int(connfd, count);
}

// int MPI_Comm_dup(MPI_Comm comm, MPI_Comm *newcomm)
void MPIProxy_Comm_dup(int connfd)
{
  int retval = 0;
  MPI_Comm comm;
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  MPI_Comm newcomm;
  // Do the send
  retval = MPI_Comm_dup(comm, &newcomm);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - Comm_dup FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &newcomm, sizeof(newcomm));
}

// int MPI_Comm_group(MPI_Comm comm, MPI_Group *group)
void MPIProxy_Comm_group(int connfd)
{
  int retval;
  MPI_Comm comm;
  MPI_Group group;
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  retval = MPI_Comm_group(comm, &group);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - Comm_dup FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &group, sizeof(group));
}

// int MPI_Comm_create(MPI_Comm comm, MPI_Group group, MPI_Comm *newcomm)
void MPIProxy_Comm_create(int connfd)
{
  int retval;
  MPI_Comm comm;
  MPI_Group group;
  MPI_Comm newcomm;
  comm = (MPI_Comm) MPIProxy_Receive_Arg_Int(connfd);
  MPIProxy_Receive_Arg_Buf(connfd, &group, sizeof(group));
  retval = MPI_Comm_create(comm, group, &newcomm);

  if (retval != MPI_SUCCESS) {
    printf("Proxy - Comm_dup FAILED\n");
    fflush(stdout);
  }

  MPIProxy_Return_Answer(connfd, retval);
  MPIProxy_Send_Arg_Buf(connfd, &newcomm, sizeof(newcomm));
}

void MPIProxy_Finalize(int connfd)
{
  serial_printf("PROXY: MPI_Finalize - ");
  MPIProxy_Return_Answer(connfd, MPI_Finalize());
}


void proxy(int connfd)
{
  int init = 0;
  int cmd = 0;
  while (1) {
    cmd = 0;
    int rc = readAll(connfd, &cmd, sizeof(cmd));
    if (rc < 0) {
      perror("PROXY: read");
      break; // perhaps the child died
    }
    switch (cmd) {
    case MPIProxy_Cmd_Init:
      serial_printf("PROXY(INIT) ");
      MPIProxy_Init(connfd);
      break;
    case MPIProxy_Cmd_Get_CommSize:
      serial_printf("PROXY(Get_CommSize) - ");
      MPIProxy_Get_CommSize(connfd);
      break;
    case MPIProxy_Cmd_Get_CommRank:
      serial_printf("PROXY(Get_CommRank) - ");
      MPIProxy_Get_CommRank(connfd);
      break;
    case MPIProxy_Cmd_Send:
      serial_printf("PROXY(Send) - ");
      MPIProxy_Send(connfd);
      break;
    case MPIProxy_Cmd_Isend:
      serial_printf("PROXY(Isend) - ");
      MPIProxy_Isend(connfd);
      break;
    case MPIProxy_Cmd_Recv:
      serial_printf("PROXY(Recv) - ");
      MPIProxy_Recv(connfd);
      break;
    case MPIProxy_Cmd_Irecv:
      serial_printf("PROXY(Irecv) - ");
      MPIProxy_Irecv(connfd);
      break;
    case MPIProxy_Cmd_Wait:
      serial_printf("PROXY(Wait) - ");
      MPIProxy_Wait(connfd);
      break;
    case MPIProxy_Cmd_Test:
      serial_printf("PROXY(Test) - ");
      MPIProxy_Test(connfd);
      break;
    case MPIProxy_Cmd_Type_size:
      serial_printf("PROXY(Type_size) - ");
      MPIProxy_Type_size(connfd);
      break;
    case MPIProxy_Cmd_Iprobe:
      serial_printf("PROXY(Iprobe) - ");
      MPIProxy_Iprobe(connfd);
      break;
    case MPIProxy_Cmd_Get_count:
      serial_printf("PROXY(Get_count)");
      MPIProxy_Get_count(connfd);
      break;
    case MPIProxy_Cmd_Finalize:
      serial_printf("PROXY(Finalize)");
      MPIProxy_Finalize(connfd);
      break;
    case MPIProxy_Cmd_Bcast:
      serial_printf("PROXY(Bcast)");
      MPIProxy_Bcast(connfd);
      break;
    case MPIProxy_Cmd_Abort:
      serial_printf("PROXY(Abort)");
      MPIProxy_Abort(connfd);
      break;
    case MPIProxy_Cmd_Barrier:
      serial_printf("PROXY(Barrier)");
      MPIProxy_Barrier(connfd);
      break;
    case MPIProxy_Cmd_Reduce:
      serial_printf("PROXY(Reduce)");
      MPIProxy_Reduce(connfd);
      break;
    case MPIProxy_Cmd_Allreduce:
      serial_printf("PROXY(Allreduce)");
      MPIProxy_Allreduce(connfd);
      break;
    case MPIProxy_Cmd_Alltoall:
      serial_printf("PROXY(Alltoall)");
      MPIProxy_Alltoall(connfd);
      break;
    case MPIProxy_Cmd_Alltoallv:
      serial_printf("PROXY(Alltoallv)");
      MPIProxy_Alltoallv(connfd);
      break;
    case MPIProxy_Cmd_Comm_split:
      serial_printf("PROXY(Comm_split)");
      MPIProxy_Comm_split(connfd);
      break;
    case MPIProxy_Cmd_Wtime:
      serial_printf("PROXY(Wtime)");
      MPIProxy_Wtime(connfd);
      break;
    case MPIProxy_Cmd_Comm_dup:
      serial_printf("PROXY(Comm_dup)");
      MPIProxy_Comm_dup(connfd);
      break;
    case MPIProxy_Cmd_Comm_group:
      serial_printf("PROXY(Comm_group)");
      MPIProxy_Comm_group(connfd);
      break;
    case MPIProxy_Cmd_Comm_create:
      serial_printf("PROXY(Comm_create)");
      MPIProxy_Comm_create(connfd);
      break;
    case MPIProxy_Cmd_Shutdown_Proxy:
      serial_printf("PROXY: Shutdown - ");
      MPIProxy_Return_Answer(connfd, 0);
      goto DONE;

    // Unimplemented Commands
    case MPIProxy_Cmd_Accumulate:
    case MPIProxy_Cmd_Add_error_class:
    case MPIProxy_Cmd_Add_error_code:
    case MPIProxy_Cmd_Add_error_string:
    case MPIProxy_Cmd_Address:
    case MPIProxy_Cmd_Allgather:
    case MPIProxy_Cmd_Iallgather:
    case MPIProxy_Cmd_Allgatherv:
    case MPIProxy_Cmd_Iallgatherv:
    case MPIProxy_Cmd_Alloc_mem:
    case MPIProxy_Cmd_Iallreduce:
    case MPIProxy_Cmd_Ialltoall:
    case MPIProxy_Cmd_Ialltoallv:
    case MPIProxy_Cmd_Alltoallw:
    case MPIProxy_Cmd_Ialltoallw:
    case MPIProxy_Cmd_Attr_delete:
    case MPIProxy_Cmd_Attr_get:
    case MPIProxy_Cmd_Attr_put:
    case MPIProxy_Cmd_Ibarrier:
    case MPIProxy_Cmd_Bsend:
    case MPIProxy_Cmd_Ibcast:
    case MPIProxy_Cmd_Bsend_init:
    case MPIProxy_Cmd_Buffer_attach:
    case MPIProxy_Cmd_Buffer_detach:
    case MPIProxy_Cmd_Cancel:
    case MPIProxy_Cmd_Cart_coords:
    case MPIProxy_Cmd_Cart_create:
    case MPIProxy_Cmd_Cart_get:
    case MPIProxy_Cmd_Cart_map:
    case MPIProxy_Cmd_Cart_rank:
    case MPIProxy_Cmd_Cart_shift:
    case MPIProxy_Cmd_Cart_sub:
    case MPIProxy_Cmd_Cartdim_get:
    case MPIProxy_Cmd_Close_port:
    case MPIProxy_Cmd_Comm_accept:
    case MPIProxy_Cmd_FMPI_Comm_c2f:
    case MPIProxy_Cmd_Comm_call_errhandler:
    case MPIProxy_Cmd_Comm_compare:
    case MPIProxy_Cmd_Comm_connect:
    case MPIProxy_Cmd_Comm_create_errhandler:
    case MPIProxy_Cmd_Comm_create_keyval:
    case MPIProxy_Cmd_Comm_create_group:
    case MPIProxy_Cmd_Comm_delete_attr:
    case MPIProxy_Cmd_Comm_disconnect:
    case MPIProxy_Cmd_Comm_idup:
    case MPIProxy_Cmd_Comm_dup_with_info:
    case MPIProxy_Cmd_Comm_f2c:
    case MPIProxy_Cmd_Comm_free_keyval:
    case MPIProxy_Cmd_Comm_free:
    case MPIProxy_Cmd_Comm_get_attr:
    case MPIProxy_Cmd_Dist_graph_create:
    case MPIProxy_Cmd_Dist_graph_create_adjacent:
    case MPIProxy_Cmd_Dist_graph_neighbors:
    case MPIProxy_Cmd_Dist_graph_neighbors_count:
    case MPIProxy_Cmd_Comm_get_errhandler:
    case MPIProxy_Cmd_Comm_get_info:
    case MPIProxy_Cmd_Comm_get_name:
    case MPIProxy_Cmd_Comm_get_parent:
    case MPIProxy_Cmd_Comm_join:
    case MPIProxy_Cmd_Comm_rank:
    case MPIProxy_Cmd_Comm_remote_group:
    case MPIProxy_Cmd_Comm_remote_size:
    case MPIProxy_Cmd_Comm_set_attr:
    case MPIProxy_Cmd_Comm_set_errhandler:
    case MPIProxy_Cmd_Comm_set_info:
    case MPIProxy_Cmd_Comm_set_name:
    case MPIProxy_Cmd_Comm_size:
    case MPIProxy_Cmd_Comm_spawn:
    case MPIProxy_Cmd_Comm_spawn_multiple:
    case MPIProxy_Cmd_Comm_split_type:
    case MPIProxy_Cmd_Comm_test_inter:
    case MPIProxy_Cmd_Compare_and_swap:
    case MPIProxy_Cmd_Dims_create:
    case MPIProxy_Cmd_FMPI_Errhandler_c2f:
    case MPIProxy_Cmd_Errhandler_create:
    case MPIProxy_Cmd_Errhandler_f2c:
    case MPIProxy_Cmd_Errhandler_free:
    case MPIProxy_Cmd_Errhandler_get:
    case MPIProxy_Cmd_Errhandler_set:
    case MPIProxy_Cmd_Error_class:
    case MPIProxy_Cmd_Error_string:
    case MPIProxy_Cmd_Exscan:
    case MPIProxy_Cmd_Fetch_and_op:
    case MPIProxy_Cmd_Iexscan:
    case MPIProxy_Cmd_FMPI_File_c2f:
    case MPIProxy_Cmd_File_f2c:
    case MPIProxy_Cmd_File_call_errhandler:
    case MPIProxy_Cmd_File_create_errhandler:
    case MPIProxy_Cmd_File_set_errhandler:
    case MPIProxy_Cmd_File_get_errhandler:
    case MPIProxy_Cmd_File_open:
    case MPIProxy_Cmd_File_close:
    case MPIProxy_Cmd_File_delete:
    case MPIProxy_Cmd_File_set_size:
    case MPIProxy_Cmd_File_preallocate:
    case MPIProxy_Cmd_File_get_size:
    case MPIProxy_Cmd_File_get_group:
    case MPIProxy_Cmd_File_get_amode:
    case MPIProxy_Cmd_File_set_info:
    case MPIProxy_Cmd_File_get_info:
    case MPIProxy_Cmd_File_set_view:
    case MPIProxy_Cmd_File_get_view:
    case MPIProxy_Cmd_File_read_at:
    case MPIProxy_Cmd_File_read_at_all:
    case MPIProxy_Cmd_File_write_at:
    case MPIProxy_Cmd_File_write_at_all:
    case MPIProxy_Cmd_File_iread_at:
    case MPIProxy_Cmd_File_iwrite_at:
    case MPIProxy_Cmd_File_iread_at_all:
    case MPIProxy_Cmd_File_iwrite_at_all:
    case MPIProxy_Cmd_File_read:
    case MPIProxy_Cmd_File_read_all:
    case MPIProxy_Cmd_File_write:
    case MPIProxy_Cmd_File_write_all:
    case MPIProxy_Cmd_File_iread:
    case MPIProxy_Cmd_File_iwrite:
    case MPIProxy_Cmd_File_iread_all:
    case MPIProxy_Cmd_File_iwrite_all:
    case MPIProxy_Cmd_File_seek:
    case MPIProxy_Cmd_File_get_position:
    case MPIProxy_Cmd_File_get_byte_offset:
    case MPIProxy_Cmd_File_read_shared:
    case MPIProxy_Cmd_File_write_shared:
    case MPIProxy_Cmd_File_iread_shared:
    case MPIProxy_Cmd_File_iwrite_shared:
    case MPIProxy_Cmd_File_read_ordered:
    case MPIProxy_Cmd_File_write_ordered:
    case MPIProxy_Cmd_File_seek_shared:
    case MPIProxy_Cmd_File_get_position_shared:
    case MPIProxy_Cmd_File_read_at_all_begin:
    case MPIProxy_Cmd_File_read_at_all_end:
    case MPIProxy_Cmd_File_write_at_all_begin:
    case MPIProxy_Cmd_File_write_at_all_end:
    case MPIProxy_Cmd_File_read_all_begin:
    case MPIProxy_Cmd_File_read_all_end:
    case MPIProxy_Cmd_File_write_all_begin:
    case MPIProxy_Cmd_File_write_all_end:
    case MPIProxy_Cmd_File_read_ordered_begin:
    case MPIProxy_Cmd_File_read_ordered_end:
    case MPIProxy_Cmd_File_write_ordered_begin:
    case MPIProxy_Cmd_File_write_ordered_end:
    case MPIProxy_Cmd_File_get_type_extent:
    case MPIProxy_Cmd_File_set_atomicity:
    case MPIProxy_Cmd_File_get_atomicity:
    case MPIProxy_Cmd_File_sync:
    case MPIProxy_Cmd_Finalized:
    case MPIProxy_Cmd_Free_mem:
    case MPIProxy_Cmd_Gather:
    case MPIProxy_Cmd_Igather:
    case MPIProxy_Cmd_Gatherv:
    case MPIProxy_Cmd_Igatherv:
    case MPIProxy_Cmd_Get_address:
    case MPIProxy_Cmd_Get_elements:
    case MPIProxy_Cmd_Get_elements_x:
    case MPIProxy_Cmd_Get:
    case MPIProxy_Cmd_Get_accumulate:
    case MPIProxy_Cmd_Get_library_version:
    case MPIProxy_Cmd_Get_processor_name:
    case MPIProxy_Cmd_Get_version:
    case MPIProxy_Cmd_Graph_create:
    case MPIProxy_Cmd_Graph_get:
    case MPIProxy_Cmd_Graph_map:
    case MPIProxy_Cmd_Graph_neighbors_count:
    case MPIProxy_Cmd_Graph_neighbors:
    case MPIProxy_Cmd_Graphdims_get:
    case MPIProxy_Cmd_Grequest_complete:
    case MPIProxy_Cmd_Grequest_start:
    case MPIProxy_Cmd_FMPI_Group_c2f:
    case MPIProxy_Cmd_Group_compare:
    case MPIProxy_Cmd_Group_difference:
    case MPIProxy_Cmd_Group_excl:
    case MPIProxy_Cmd_Group_f2c:
    case MPIProxy_Cmd_Group_free:
    case MPIProxy_Cmd_Group_incl:
    case MPIProxy_Cmd_Group_intersection:
    case MPIProxy_Cmd_Group_range_excl:
    case MPIProxy_Cmd_Group_range_incl:
    case MPIProxy_Cmd_Group_rank:
    case MPIProxy_Cmd_Group_size:
    case MPIProxy_Cmd_Group_translate_ranks:
    case MPIProxy_Cmd_Group_union:
    case MPIProxy_Cmd_Ibsend:
    case MPIProxy_Cmd_Improbe:
    case MPIProxy_Cmd_Imrecv:
    case MPIProxy_Cmd_FMPI_Info_c2f:
    case MPIProxy_Cmd_Info_create:
    case MPIProxy_Cmd_Info_delete:
    case MPIProxy_Cmd_Info_dup:
    case MPIProxy_Cmd_Info_f2c:
    case MPIProxy_Cmd_Info_free:
    case MPIProxy_Cmd_Info_get:
    case MPIProxy_Cmd_Info_get_nkeys:
    case MPIProxy_Cmd_Info_get_nthkey:
    case MPIProxy_Cmd_Info_get_valuelen:
    case MPIProxy_Cmd_Info_set:
    case MPIProxy_Cmd_Initialized:
    case MPIProxy_Cmd_Init_thread:
    case MPIProxy_Cmd_Intercomm_create:
    case MPIProxy_Cmd_Intercomm_merge:
    case MPIProxy_Cmd_Irsend:
    case MPIProxy_Cmd_Issend:
    case MPIProxy_Cmd_Is_thread_main:
    case MPIProxy_Cmd_Keyval_create:
    case MPIProxy_Cmd_Keyval_free:
    case MPIProxy_Cmd_Lookup_name:
    case MPIProxy_Cmd_FMPI_Message_c2f:
    case MPIProxy_Cmd_Message_f2c:
    case MPIProxy_Cmd_Mprobe:
    case MPIProxy_Cmd_Mrecv:
    case MPIProxy_Cmd_Neighbor_allgather:
    case MPIProxy_Cmd_Ineighbor_allgather:
    case MPIProxy_Cmd_Neighbor_allgatherv:
    case MPIProxy_Cmd_Ineighbor_allgatherv:
    case MPIProxy_Cmd_Neighbor_alltoall:
    case MPIProxy_Cmd_Ineighbor_alltoall:
    case MPIProxy_Cmd_Neighbor_alltoallv:
    case MPIProxy_Cmd_Ineighbor_alltoallv:
    case MPIProxy_Cmd_Neighbor_alltoallw:
    case MPIProxy_Cmd_Ineighbor_alltoallw:
    case MPIProxy_Cmd_FMPI_Op_c2f:
    case MPIProxy_Cmd_Op_commutative:
    case MPIProxy_Cmd_Op_create:
    case MPIProxy_Cmd_Open_port:
    case MPIProxy_Cmd_Op_f2c:
    case MPIProxy_Cmd_Op_free:
    case MPIProxy_Cmd_Pack_external:
    case MPIProxy_Cmd_Pack_external_size:
    case MPIProxy_Cmd_Pack:
    case MPIProxy_Cmd_Pack_size:
    case MPIProxy_Cmd_Pcontrol:
    case MPIProxy_Cmd_Probe:
    case MPIProxy_Cmd_Publish_name:
    case MPIProxy_Cmd_Put:
    case MPIProxy_Cmd_Query_thread:
    case MPIProxy_Cmd_Raccumulate:
    case MPIProxy_Cmd_Recv_init:
    case MPIProxy_Cmd_Ireduce:
    case MPIProxy_Cmd_Reduce_local:
    case MPIProxy_Cmd_Reduce_scatter:
    case MPIProxy_Cmd_Ireduce_scatter:
    case MPIProxy_Cmd_Reduce_scatter_block:
    case MPIProxy_Cmd_Ireduce_scatter_block:
    case MPIProxy_Cmd_Register_datarep:
    case MPIProxy_Cmd_FMPI_Request_c2f:
    case MPIProxy_Cmd_Request_f2c:
    case MPIProxy_Cmd_Request_free:
    case MPIProxy_Cmd_Request_get_status:
    case MPIProxy_Cmd_Rget:
    case MPIProxy_Cmd_Rget_accumulate:
    case MPIProxy_Cmd_Rput:
    case MPIProxy_Cmd_Rsend:
    case MPIProxy_Cmd_Rsend_init:
    case MPIProxy_Cmd_Scan:
    case MPIProxy_Cmd_Iscan:
    case MPIProxy_Cmd_Scatter:
    case MPIProxy_Cmd_Iscatter:
    case MPIProxy_Cmd_Scatterv:
    case MPIProxy_Cmd_Iscatterv:
    case MPIProxy_Cmd_Send_init:
    case MPIProxy_Cmd_Sendrecv:
    case MPIProxy_Cmd_Sendrecv_replace:
    case MPIProxy_Cmd_Ssend_init:
    case MPIProxy_Cmd_Ssend:
    case MPIProxy_Cmd_Start:
    case MPIProxy_Cmd_Startall:
    case MPIProxy_Cmd_Status_c2f:
    case MPIProxy_Cmd_Status_f2c:
    case MPIProxy_Cmd_Status_set_cancelled:
    case MPIProxy_Cmd_Status_set_elements:
    case MPIProxy_Cmd_Status_set_elements_x:
    case MPIProxy_Cmd_Testall:
    case MPIProxy_Cmd_Testany:
    case MPIProxy_Cmd_Test_cancelled:
    case MPIProxy_Cmd_Testsome:
    case MPIProxy_Cmd_Topo_test:
    case MPIProxy_Cmd_FMPI_Type_c2f:
    case MPIProxy_Cmd_Type_commit:
    case MPIProxy_Cmd_Type_contiguous:
    case MPIProxy_Cmd_Type_create_darray:
    case MPIProxy_Cmd_Type_create_f90_complex:
    case MPIProxy_Cmd_Type_create_f90_integer:
    case MPIProxy_Cmd_Type_create_f90_real:
    case MPIProxy_Cmd_Type_create_hindexed_block:
    case MPIProxy_Cmd_Type_create_hindexed:
    case MPIProxy_Cmd_Type_create_hvector:
    case MPIProxy_Cmd_Type_create_keyval:
    case MPIProxy_Cmd_Type_create_indexed_block:
    case MPIProxy_Cmd_Type_create_struct:
    case MPIProxy_Cmd_Type_create_subarray:
    case MPIProxy_Cmd_Type_create_resized:
    case MPIProxy_Cmd_Type_delete_attr:
    case MPIProxy_Cmd_Type_dup:
    case MPIProxy_Cmd_Type_extent:
    case MPIProxy_Cmd_Type_free:
    case MPIProxy_Cmd_Type_free_keyval:
    case MPIProxy_Cmd_Type_f2c:
    case MPIProxy_Cmd_Type_get_attr:
    case MPIProxy_Cmd_Type_get_contents:
    case MPIProxy_Cmd_Type_get_envelope:
    case MPIProxy_Cmd_Type_get_extent:
    case MPIProxy_Cmd_Type_get_extent_x:
    case MPIProxy_Cmd_Type_get_name:
    case MPIProxy_Cmd_Type_get_true_extent:
    case MPIProxy_Cmd_Type_get_true_extent_x:
    case MPIProxy_Cmd_Type_hindexed:
    case MPIProxy_Cmd_Type_hvector:
    case MPIProxy_Cmd_Type_indexed:
    case MPIProxy_Cmd_Type_lb:
    case MPIProxy_Cmd_Type_match_size:
    case MPIProxy_Cmd_Type_set_attr:
    case MPIProxy_Cmd_Type_set_name:
    case MPIProxy_Cmd_Type_size_x:
    case MPIProxy_Cmd_Type_struct:
    case MPIProxy_Cmd_Type_ub:
    case MPIProxy_Cmd_Type_vector:
    case MPIProxy_Cmd_Unpack:
    case MPIProxy_Cmd_Unpublish_name:
    case MPIProxy_Cmd_Unpack_external :
    case MPIProxy_Cmd_Waitall:
    case MPIProxy_Cmd_Waitany:
    case MPIProxy_Cmd_Waitsome:
    case MPIProxy_Cmd_Win_allocate:
    case MPIProxy_Cmd_Win_allocate_shared:
    case MPIProxy_Cmd_Win_attach:
    case MPIProxy_Cmd_FMPI_Win_c2f:
    case MPIProxy_Cmd_Win_call_errhandler:
    case MPIProxy_Cmd_Win_complete:
    case MPIProxy_Cmd_Win_create:
    case MPIProxy_Cmd_Win_create_dynamic:
    case MPIProxy_Cmd_Win_create_errhandler:
    case MPIProxy_Cmd_Win_create_keyval:
    case MPIProxy_Cmd_Win_delete_attr:
    case MPIProxy_Cmd_Win_detach:
    case MPIProxy_Cmd_Win_f2c:
    case MPIProxy_Cmd_Win_fence:
    case MPIProxy_Cmd_Win_flush:
    case MPIProxy_Cmd_Win_flush_all:
    case MPIProxy_Cmd_Win_flush_local:
    case MPIProxy_Cmd_Win_flush_local_all:
    case MPIProxy_Cmd_Win_free:
    case MPIProxy_Cmd_Win_free_keyval:
    case MPIProxy_Cmd_Win_get_attr:
    case MPIProxy_Cmd_Win_get_errhandler:
    case MPIProxy_Cmd_Win_get_group:
    case MPIProxy_Cmd_Win_get_info:
    case MPIProxy_Cmd_Win_get_name:
    case MPIProxy_Cmd_Win_lock:
    case MPIProxy_Cmd_Win_lock_all:
    case MPIProxy_Cmd_Win_post:
    case MPIProxy_Cmd_Win_set_attr:
    case MPIProxy_Cmd_Win_set_errhandler:
    case MPIProxy_Cmd_Win_set_info:
    case MPIProxy_Cmd_Win_set_name:
    case MPIProxy_Cmd_Win_shared_query:
    case MPIProxy_Cmd_Win_start:
    case MPIProxy_Cmd_Win_sync:
    case MPIProxy_Cmd_Win_test:
    case MPIProxy_Cmd_Win_unlock:
    case MPIProxy_Cmd_Win_unlock_all:
    case MPIProxy_Cmd_Win_wait:
    case MPIProxy_Cmd_Wtick:

    default:
      NOT_IMPLEMENTED(cmd);
      break;
    }
  }
DONE:
  return;
}

// Copied from DMTCP utils
static bool
strEndsWith(const char *str, const char *pattern)
{
  if (str == NULL || pattern == NULL) {
    return false;
  }
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    size_t idx = len1 - len2;
    return strncmp(str + idx, pattern, len2) == 0;
  }
  return false;
}

static bool
isCkptImage(const char *s, int rank = 0)
{
  if (strEndsWith(s, ".dmtcp")) {
    return true;
  }
  return false;
}

// Copied from DMTCP jalib
static std::vector<int>
listOpenFds()
{
  int fd = open("/proc/self/fd", O_RDONLY | O_NDELAY |
                       O_LARGEFILE | O_DIRECTORY, 0);

  const size_t allocation = (4 * BUFSIZ < sizeof(struct dirent64)
                             ? sizeof(struct dirent64) : 4 * BUFSIZ);
  char *buf = (char *)malloc(allocation);

  std::vector<int> fdVec;

  while (true) {
    int nread = syscall(SYS_getdents, fd, buf, allocation);
    if (nread == 0) {
      break;
    }
    for (int pos = 0; pos < nread;) {
      struct linux_dirent *d = (struct linux_dirent *)(&buf[pos]);
      if (d->d_ino > 0) {
        char *ch;
        int fdnum = strtol(d->d_name, &ch, 10);
        if (*ch == 0 && fdnum >= 0 && fdnum != fd) {
          fdVec.push_back(fdnum);
        }
      }
      pos += d->d_reclen;
    }
  }

  close(fd);

  std::sort(fdVec.begin(), fdVec.end());
  free(buf);
  return fdVec;
}

static void
setCloseOnExec()
{
  std::vector<int> fds = listOpenFds();
  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (DMTCP_IS_PROTECTED_FD(fd) || fd <= 2) {
      continue;
    }
    int ret = fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (ret < 0) {
      perror("fcntl");
    }
  }
}

void launch_or_restart(pid_t pid, int rank, int argc, char *argv[])
{
  int i = 0;
  if (pid == 0) {
    // child:
    if (strstr(argv[1], "dmtcp_launch")) {
      // go ahead and exec into provided arglist
      serial_printf("Starting");
      std::vector<char*> s;
#ifdef DEBUGGING
      s.push_back(const_cast<char*>("xterm"));
      s.push_back(const_cast<char*>("-e"));
      s.push_back(const_cast<char*>("gdb"));
      s.push_back(const_cast<char*>("--args"));
#endif
      for (int i = 1; i < argc; i++) {
        s.push_back(argv[i]);
        serial_printf(argv[i]);
      }
      s.push_back(NULL); // This is necessary for exec
      setCloseOnExec();
      int ret = execvp(s[0], &s[0]);
      if (ret < 0) {
        perror("execvp failed");
        exit(-1);
      }
    } else if (strstr(argv[1], "dmtcp_restart")) {
      serial_printf("Restarting");
      // TODO: Select correct image from arglist, re-form arglist
#if 0
      char * newargv[5] = {NULL, NULL, NULL, NULL, NULL};
      newargv[0] = argv[1];
      newargv[1] = argv[2];
      newargv[2] = argv[3 + rank];
      // newargv[2] = argv[3];
      // newargv[3] = argv[4+rank];
      execvp(newargv[0], (char* const*) &newargv);
#else
      std::vector<char*> s;
      int imgidx = 0;
      for (int i = 1; i < argc; i++) {
        if (isCkptImage(argv[i], rank)) {
          imgidx = i;
          break;
        }
        s.push_back(argv[i]);
      }
      s.push_back(argv[imgidx + rank]);
      s.push_back(NULL); // This is necessary for exec
      setCloseOnExec();
      int ret = execvp(s[0], &s[0]);
      if (ret < 0) {
        perror("execvp failed");
        exit(-1);
      }
#endif
    } else {
      printf("ERROR - NOT A LAUNCH OR RESUME\n");
    }
    exit(1);
  }
  return;
}

int main(int argc, char *argv[])
{
  // 0 is read
  // 1 is write
  int debugPipe[2];
  int rank = 0;
  int restart_rank = -1;

  socketpair(AF_UNIX, SOCK_STREAM, 0, debugPipe);

  pid_t pid = fork();
  if (pid > 0) {
    int status;
    if (strstr(argv[1], "dmtcp_restart")) {
      MPI_Init(NULL, NULL);
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      writeAll(debugPipe[0], &rank, sizeof(int));
      // FIXME: wait a second to let child get the rank
      sleep(1);
    }
    proxy(debugPipe[0]);
    waitpid(pid, &status, 0);
  } else if (pid == 0) {
    assert(dup2(debugPipe[1], PROTECTED_MPI_PROXY_FD) ==
           PROTECTED_MPI_PROXY_FD);
    close(debugPipe[1]);
    if (strstr(argv[1], "dmtcp_restart")) {
      readAll(PROTECTED_MPI_PROXY_FD, &restart_rank, sizeof(int));
      assert(restart_rank != -1);
    }
    int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (r == -1) { perror(0); exit(1); }
    // test in case the original parent exited just
    // before the prctl() call
    if (getppid() == 1)
      exit(1);
    launch_or_restart(pid, restart_rank, argc, argv);
  } else {
    assert(0);
  }

  return 0;
}
