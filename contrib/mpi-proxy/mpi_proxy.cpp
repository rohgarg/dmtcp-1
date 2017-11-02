/* CopyLeft Gregory Price (2017) */

#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <mpi.h>
#include <map>
#include <vector>

#include "mpi_proxy.h"
#include "protectedfds.h"
#include "dmtcp.h"
#include "util.h"

int listfd = 0;

int serial_printf(const char * msg)
{
  printf("%s\n", msg);
  fflush(stdout);
}

int MPIProxy_Receive_Arg_Int(int connfd)
{
  int retval;
  int status;
  status = read(connfd, &retval, sizeof(int));
  // TODO: error check
  return retval;
}

int MPIProxy_Send_Arg_Int(int connfd, int arg)
{
  int status = write(connfd, &arg, sizeof(int));
  // TODO: error check
  return status;
}

void MPIProxy_Return_Answer(int connfd, int answer)
{
  printf("Returned %08x\n", answer);
  fflush(stdout);
  write(connfd, &answer, 4);
  return;
}

void MPIProxy_Init(int connfd)
{
  // TODO: Get argc and argv
  serial_printf("PROXY: MPI_Init - ");
  MPIProxy_Return_Answer(connfd, MPI_Init(NULL, NULL));
}

void MPIProxy_Get_CommSize(int connfd)
{
  int group = 0;
  int commsize = 0;
  int retval = 0;
  group = MPIProxy_Receive_Arg_Int(connfd);
  retval = MPI_Comm_size(group, &commsize);
  MPIProxy_Return_Answer(connfd, retval);
  if (!retval)
    MPIProxy_Send_Arg_Int(connfd, commsize);
}

void MPIProxy_Get_CommRank(int connfd)
{
  int group = 0;
  int commrank = 0;
  int retval = 0;

  group = MPIProxy_Receive_Arg_Int(connfd);
  retval = MPI_Comm_rank(group, &commrank);
  MPIProxy_Return_Answer(connfd, retval);
  if (!retval)
    MPIProxy_Send_Arg_Int(connfd, commrank);
}

void MPIProxy_Finalize(int connfd)
{
  serial_printf("PROXY: MPI_Finalize - ");
  MPIProxy_Return_Answer(connfd, MPI_Finalize());
}


void proxy(int connfd)
{
  int init = 0;
  int cmd = 0;
  while (1) {
    cmd = 0;
    int rc = read(connfd, &cmd, sizeof(cmd));
    if (rc < 0) {
      perror("PROXY: read");
      continue;
    }
    switch (cmd) {
    case MPIProxy_Cmd_Init:
      serial_printf("PROXY(INIT) ");
      MPIProxy_Init(connfd);
      break;
    case MPIProxy_Cmd_Get_CommSize:
      serial_printf("PROXY(Get_CommSize) ");
      MPIProxy_Get_CommSize(connfd);
      break;
    case MPIProxy_Cmd_Get_CommRank:
      serial_printf("PROXY(Get_CommRank)");
      MPIProxy_Get_CommRank(connfd);
      break;
    case MPIProxy_Cmd_Finalize:
      serial_printf("PROXY(Finalize)");
      MPIProxy_Finalize(connfd);
      break;
    case MPIProxy_Cmd_Shutdown_Proxy:
      serial_printf("PROXY: Shutdown - ");
      MPIProxy_Return_Answer(connfd, 0);
      goto DONE;
    default:
      printf("PROXY: Unknown Command: %d. Exiting.\n", cmd);
      goto DONE;
      break;
    }
  }
DONE:
  return;
}

void launch_or_restart(pid_t pid, int rank, int argc, char *argv[])
{
  int i = 0;
  if (pid == 0) {
    // child:
    if (strstr(argv[1], "dmtcp_launch")) {
      // go ahead and exec into provided arglist
      serial_printf("Starting");
      std::vector<char*> s;
#ifdef DEBUGGING
      s.push_back(const_cast<char*>("xterm"));
      s.push_back(const_cast<char*>("-e"));
      s.push_back(const_cast<char*>("gdb"));
      s.push_back(const_cast<char*>("--args"));
#endif
      for (int i = 1; i < argc; i++) {
        s.push_back(argv[i]);
      }
      execvp(s[0], &s[0]);
    } else if (strstr(argv[1], "dmtcp_restart")) {
      serial_printf("Restarting");
      // TODO: Select correct image from arglist, re-form arglist
      char * newargv[5] = {NULL, NULL, NULL, NULL, NULL};
      newargv[0] = argv[1];
      newargv[1] = argv[2];
      newargv[2] = argv[3 + rank];
      // newargv[2] = argv[3];
      // newargv[3] = argv[4+rank];
      execvp(newargv[0], (char* const*) &newargv);
    } else {
      printf("ERROR - NOT A LAUNCH OR RESUME\n");
    }
    exit(1);
  }
  return;
}

int main(int argc, char *argv[])
{
  // 0 is read
  // 1 is write
  int debugPipe[2];
  int rank = 0;
  int restart_rank = -1;

  socketpair(AF_UNIX, SOCK_STREAM, 0, debugPipe);

  pid_t pid = fork();
  if (pid > 0) {
    int status;
    if (strstr(argv[1], "dmtcp_restart")) {
      MPI_Init(NULL, NULL);
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      write(debugPipe[0], &rank, sizeof(int));
      // FIXME: wait a second to let child get the rank
      sleep(1);
    }
    proxy(debugPipe[0]);
    waitpid(pid, &status, 0);
  } else if (pid == 0) {
    assert(dup2(debugPipe[1], PROTECTED_MPI_PROXY_FD) ==
           PROTECTED_MPI_PROXY_FD);
    close(debugPipe[1]);
    if (strstr(argv[1], "dmtcp_restart")) {
      read(PROTECTED_MPI_PROXY_FD, &restart_rank, sizeof(int));
      assert(restart_rank != -1);
    }
    launch_or_restart(pid, restart_rank, argc, argv);
  } else {
    assert(0);
  }

  return 0;
}
