/* CopyLeft Gregory Price (2018) */

#include <mpi.h>
#include <stdint.h>

#define DRAINED_REQUEST_VALUE 0xffffffff

enum MPI_Plugin_Recv_Status
{
  MPI_PLUGIN_PROXY_PACKET_WAITING,
  MPI_PLUGIN_BUFFERED_PACKET_WAITING,
  MPI_PLUGIN_NO_PACKET_WAITING
};

enum MPI_Plugin_Request_Type
{
  ISEND_REQUEST = 0,
  IRECV_REQUEST = 1,
  DRAINED_EARLY = 2,
};

typedef struct Message
{
  void* buf;
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  MPI_Status status;
} Message;

typedef struct Async_Message
{
  // control data
  bool serviced;
  MPI_Plugin_Request_Type type;
  // request parameters
  const void* sendbuf;
  void* recvbuf;
  int count;
  MPI_Datatype datatype;
  int size;
  MPI_Comm comm;
  MPI_Status status;
  int remote_node;  // dest or source
  int tag;
  // async parameters
  MPI_Request * request;
  int flag;
} Async_Message;

static inline int
sum(const int *cnts, int len)
{
  int sum = 0;
  for (int i = 0; i < len; i++) {
    sum += cnts[i];
  }
  return sum;
}
