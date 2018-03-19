// Author: Wes Kendall
// Copyright 2011 www.mpitutorial.com
// This code is provided freely with the tutorials on mpitutorial.com. Feel
// free to modify it for your own use. Any distribution of the code must
// either provide a link to www.mpitutorial.com or keep this header in tact.
//
// Ping pong example with MPI_Send and MPI_Recv. Two processes ping pong a
// number back and forth, incrementing it until it reaches a given value.
//
#include <mpi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXREPS 10000000

int main(int argc, char** argv) {
  int world_rank;
  int world_size;
  int buf1 = 1;
  int buf2 = 2;
  int buf3 = 3;
  int i = 0;
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
    for (i = 0; i < MAXREPS; i++)
    {
      MPI_Isend(&buf1, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &req1);
      MPI_Send(&buf2, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
      MPI_Isend(&buf3, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &req2);
      MPI_Wait(&req1, MPI_STATUS_IGNORE);
      MPI_Wait(&req2, MPI_STATUS_IGNORE);
    }
  }
  else
  {
    for (i = 0; i < MAXREPS; i++)
    {
      MPI_Irecv(&buf1, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &req1);
      MPI_Irecv(&buf2, 1, MPI_INT, 1, 0, MPI_COMM_WORLD, &req2);
      MPI_Recv(&buf3, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Wait(&req1, MPI_STATUS_IGNORE);
      MPI_Wait(&req2, MPI_STATUS_IGNORE);
      if (buf1 != 1 || buf2 != 2 || buf3 != 3)
        printf("DERP\n");
    }
  }
  printf("%d - %d\n", world_rank, i);
  MPI_Finalize();
}
