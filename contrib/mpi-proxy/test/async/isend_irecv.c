// this should test that Isend and Irecv can be used together in the simplest
// way possible (effectively re-implementing MPI_Send and MPI_Recv)
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv) {
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  MPI_Request request = 0;

  // We are assuming at least 2 processes for this task
  if (world_size < 2) {
    fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  int number;
  if (world_rank == 0) {
    // If we are rank 0, set the number to -1 and send it to process 1
    number = 27;
    MPI_Isend(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &request);
    sleep(5);
    MPI_Wait(&request, MPI_STATUS_IGNORE);
    printf("done waiting\n");
    fflush(stdout);
  } else if (world_rank == 1) {
    MPI_Irecv(&number, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &request);
    MPI_Wait(&request, MPI_STATUS_IGNORE);
    sleep(5);
    printf("Process 1 received number %d from process 0\n", number);
    fflush(stdout);
  }
  printf("%d finalized\n", world_rank);
  fflush(stdout);
  MPI_Finalize();
}
