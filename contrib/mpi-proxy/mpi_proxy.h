/* CopyLeft Gregory Price (2017) */

enum MPI_Proxy_Commands
{
  MPIProxy_ERROR = 0,
  MPIProxy_Cmd_Init = 1,
  MPIProxy_Cmd_Finalize = 2,
  MPIProxy_Cmd_Get_CommSize = 3,
  MPIProxy_Cmd_Set_CommSize = 4,
  MPIProxy_Cmd_Get_CommRank = 5,
  MPIProxy_Cmd_Set_CommRank = 6,
  MPIProxy_Cmd_Get_ProcessorName = 7,
  MPIProxy_Cmd_Set_ProcessorName = 8,
  MPIProxy_Cmd_Shutdown_Proxy = 0xFFFFFFFF,
};

void mpi_proxy_wait_for_instructions();
