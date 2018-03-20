#include <mpi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  int world_rank;
  int world_size;
  int buf1 = 1;
  int buf2 = 2;
  MPI_Request req1;
  MPI_Request req2;
  MPI_Init(NULL, NULL);

  // Initialize the MPI environment
  // Find out rank, size
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  // We are assuming at least 2 processes for this task
  if (world_size != 2) {
    fprintf(stderr, "World size must be two for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  if (world_rank % 2) // sender
  {
    MPI_Isend(&buf1, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &req1);
    MPI_Isend(&buf2, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &req2);
    printf("%d: %08x - %08x\n", world_rank, req1, req2);
    fflush(stdout);
    MPI_Wait(&req1, MPI_STATUS_IGNORE);
    MPI_Wait(&req2, MPI_STATUS_IGNORE);
    printf("%d: %08x - %08x\n", world_rank, req1, req2);
    fflush(stdout);
  }
  else // reciever
  {
    MPI_Irecv(&buf1, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &req1);
    MPI_Irecv(&buf2, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &req2);
    printf("%d: %08x - %08x\n", world_rank, req1, req2);
    fflush(stdout);
    //MPI_Wait(&req1, MPI_STATUS_IGNORE);
    //MPI_Wait(&req2, MPI_STATUS_IGNORE);
    sleep(5); // test if updates are made without a test of wait call
    printf("%d: %08x - %08x\n", world_rank, req1, req2);
    fflush(stdout);
  }
  MPI_Finalize();
}
