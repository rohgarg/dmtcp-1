/*
int MPI_Comm_size(MPI_Comm comm, int *size)
{
	printf("DMTCP: MPI_Comm_size\n");
	return NEXT_FNC(MPI_Comm_size)(comm, size);
}

int MPI_Comm_rank(MPI_Comm comm, int *rank)
{
	printf("DMTCP: MPI_Comm_rank\n");
	return NEXT_FNC(MPI_Comm_rank)(comm, rank);
}

int MPI_Get_processor_name(char *name, int *resultlen)
{
	printf("DMTCP: MPI_Get_processor_name\n");
	return NEXT_FNC(MPI_Get_processor_name)(name, resultlen);
}
*/

/* mpi-send-and-receive: send_recv.c, ping_pong.c, ring.c */
// MPI_Abort
// MPI_Send
// MPI_Recv

/* dynamic-receiving-with-mpi-prob-and-mpi-status: probe.c, check_status.c */
/* point-to-point-communication-application-random-walk */
// Structures:
//		MPI_Status
// MPI_Probe
// MPI_Get_count
// MPI_Barrier		(check_status.c only)

/* mpi-broadcast-and-collective-communication: my_bcast, compare_bcast.c */
// nothing new needed for my_bcast.c
// MPI_Bcast

/* mpi-scatter-gather-and-allgather: avg.c, */
// MPI_Scatter
// MPI_Gather
// MPI_Allgather

/* performing-parallel-rank-with-mpi: random_rank.c, tmpi_rank.c */
// MPI_Type_size

/* mpi-reduce-and-allreduce: reduce_avg.c, reduce_stddev.c */
// MPI_Reduce
// MPI_Allreduce

/* introduction-to-groups-and-communicators: split.c, groups.c  */
// Structures:
// 		MPI_Group
// MPI_Comm_group
// MPI_Group_incl
// MPI_Comm_create_group
// MPI_Group_free
// MPI_Comm_free
