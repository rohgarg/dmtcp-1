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

struct linux_dirent {
  unsigned long d_ino;        /* Inode number */
  unsigned long d_off;        /* Offset to next linux_dirent */
  unsigned short d_reclen;    /* Length of this linux_dirent */
  char d_name[];              /* Filename (null-terminated) */

  /* length is actually (d_reclen - 2 -
     offsetof(struct linux_dirent, d_name) */

  /*
     char           pad;       // Zero padding byte
     char           d_type;    // File type (only since Linux 2.6.4;
                               // offset is (d_reclen - 1))
  */
};

void mpi_proxy_wait_for_instructions();
