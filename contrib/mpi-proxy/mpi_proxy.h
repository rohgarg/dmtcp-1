/* CopyLeft Gregory Price (2017) */

enum MPI_Proxy_Commands
{
  MPIProxy_ERROR = 0,
  // MPI Calls
  MPIProxy_Cmd_Init = 1,
  MPIProxy_Cmd_Finalize = 2,
  MPIProxy_Cmd_Get_CommSize = 3,
  MPIProxy_Cmd_Get_CommRank = 4,
  MPIProxy_Cmd_Get_ProcessorName = 5,
  MPIProxy_Cmd_Send = 6,
  MPIProxy_Cmd_Recv = 7,
  MPIProxy_Cmd_Type_size = 8,
  MPIProxy_Cmd_Iprobe = 9,
  MPIProxy_Cmd_Get_count = 10,
  MPIProxy_Cmd_Isend = 11,
  MPIProxy_Cmd_Irecv = 12,
  MPIProxy_Cmd_Wait = 13,
  MPIProxy_Cmd_Test = 14,

  // Draining Commands
  MPIProxy_Drain_Irecv = 0x80000000,

  // Proxy Commands
  MPIProxy_Cmd_Shutdown_Proxy = 0xFFFFFFFF,
};

void mpi_proxy_wait_for_instructions();
