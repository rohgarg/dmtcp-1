/* CopyLeft Gregory Price (2018) */

#include <mpi.h>
#include <stdint.h>

enum MPI_Plugin_Recv_Status
{
  MPI_PLUGIN_PROXY_PACKET_WAITING,
  MPI_PLUGIN_BUFFERED_PACKET_WAITING,
  MPI_PLUGIN_NO_PACKET_WAITING
};

typedef struct Message
{
  void* buf;
  int count;
  MPI_Datatype datatype;
  MPI_Comm comm;
  MPI_Status status;
  int size;
} Message;

typedef struct Irecv_Params
{
  // request parameters
  void* buf;
  int count;
  MPI_Datatype datatype;
  int source;
  int tag;
  MPI_Comm comm;
  MPI_Request * request;
  // response parameters
  int flag;
  MPI_Status status;
} Irecv_Params;

enum MPI_Plugin_Request_Type
{
  ISEND_REQUEST,
  IRECV_REQUEST,
};
