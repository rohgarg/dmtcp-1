/* CopyLeft Gregory Price (2017) */

enum MPI_Proxy_Commands
{
  MPIProxy_ERROR = 0,
  MPIProxy_Cmd_Init,
  MPIProxy_Cmd_Finalize,
  MPIProxy_Cmd_Get_CommSize,
  MPIProxy_Cmd_Get_CommRank,
  MPIProxy_Cmd_Get_ProcessorName,
  MPIProxy_Cmd_Set_ProcessorName,
  MPIProxy_Cmd_Shutdown_Proxy = 0xFFFFFFFF,
};

void mpi_proxy_wait_for_instructions();