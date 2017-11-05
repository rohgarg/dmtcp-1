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
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
  int size = 0;
  int status = 0;
  int count = 0;
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  while (count < 20)
  {
    status = MPI_Type_size(MPI_INT, &size);
    if (status != MPI_SUCCESS)
      printf("%08x - %d\n", status, size);
    count++;
  }
  MPI_Finalize();
}
