// This test will be used to ensure MPI_Irecv and MPI_Test work together
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
  int done = 0;

  // We are assuming at least 2 processes for this task
  if (world_size < 2) {
    fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  int number = 0;
  if (world_rank == 0) {
    // If we are rank 0, set the number to -1 and send it to process 1
    number = 25;
    MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
    sleep(5);
  } else if (world_rank == 1) {
    MPI_Irecv(&number, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &request);
    sleep(4);
    // Spin on test until we get something
    while (!done)
    {
      MPI_Test(&request, &done, MPI_STATUS_IGNORE);
      sleep(1);
    }

    printf("Process 1 received number %d from process 0\n", number);
  }
  MPI_Finalize();
}
