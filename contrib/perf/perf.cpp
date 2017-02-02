/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <limits.h>

#ifdef MYPLUG_DEBUG
# define DEBUG
#endif

#include "jassert.h"
#include "dmtcp.h"
#include "protectedfds.h"

#define MAX_LINE_LEN 2000
//#define NUM_OF_PERF_EVENTS 8
#define NUM_OF_PERF_EVENTS 2

//int fd[NUM_OF_PERF_EVENTS] = {-1, -1, -1, -1, -1, -1, -1, -1};
int fd[NUM_OF_PERF_EVENTS] = {-1, -1};

static const char* getStatsFilename(char *fname);

/* Reads from fd until count bytes are read, or
 * newline encountered.
 *
 * Side effects: Copies the characters, including
 * the newline, read from the fd into the buf.
 *
 * Returns num of chars read on success;
 *         -1 on read failure or invalid args; and
 *         -2 if the buffer is too small
 */
static int
readLine(int fd, char *buf, int count)
{
  int i = 0;
  char c;
  JASSERT(fd >= 0 && buf != NULL);
#define NEWLINE '\n' // Linux, OSX
  while (i < count) {
    ssize_t rc = read(fd, &c, 1);
    if (rc == 0) {
      break;
    } else if (rc < 0) {
      buf[i] = '\0';
      return -1;
    } else {
      buf[i++] = c;
      if (c == NEWLINE) break;
    }
  }
  buf[i] = '\0';
  if (i >= count)
    return -2;
  else
    return i;
}

static void
read_perf_ctr_val(int i, const char *name, FILE *outfp)
{
  JASSERT(fd[i] > 0);
  long long count = 0;
  int rc = 0, tries = 0;
  const short MAX_RETRIES = 5;

  do {
    rc = read(fd[i], &count, sizeof(long long));
    if (rc < 0) {
      JASSERT(false)(JASSERT_ERRNO)(tries)(fd[i])(i)(name).Text("Read failed for perf ctr");
    }
    tries++;
  } while (rc != sizeof(count) && tries < MAX_RETRIES);

  JWARNING(rc == sizeof(count))(tries)(rc)(fd[i])(i)(name)
          .Text("Read returned fewer bytes than expected after MAX retries");

  JTRACE("read ")(count)(dmtcp_virtual_to_real_pid(getpid()));
  fprintf(outfp, "%s: %lld\n", name, count);
  ioctl(fd[i], PERF_EVENT_IOC_RESET, 0);
  //close(fd[i]);
}

static bool started = false;

#define _real_open NEXT_FNC(open)
#define _real_dup2 NEXT_FNC(dup2)

static void
read_ctrs(FILE *outfp)
{
  JASSERT(started == true)(getStatsFilename(NULL));
  char line[MAX_LINE_LEN] = {0};
  int foundBoth = 0;
  int fd = _real_open("/proc/self/status", O_RDONLY);
  JASSERT(fd > 0);

  while ((readLine(fd, line, MAX_LINE_LEN) > 0) && (foundBoth != 2)) {
    if(strstr(line, "Name") || strstr(line, "VmRSS")) {
      fprintf(outfp, "%s", line);
      foundBoth += 1;
    }
    memset(line, 0, MAX_LINE_LEN);
  }
  close(fd);

 // read_perf_ctr_val(0, "PAGE_FAULTS", outfp);
 // read_perf_ctr_val(1, "CONTEXT_SWITCHES", outfp);
 // read_perf_ctr_val(2, "CPU_MIGRATIONS", outfp);
  read_perf_ctr_val(0, "CPU_CYCLES", outfp);
  read_perf_ctr_val(1, "INSTRUCTIONS", outfp);
 // read_perf_ctr_val(5, "CACHE_REFERENCES", outfp);
 // read_perf_ctr_val(6, "CACHE_MISSES", outfp);
 // read_perf_ctr_val(7, "BRANCH_INSTRUCTIONS", outfp);

  char buff[20];
  time_t now = time(NULL);
  strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
  fprintf(outfp, "[TIMESTAMP] %s\n\n", buff);
  started = false;
  JTRACE("read ctrs; turning started to false")(started);
}

static long
perf_event_open1(struct perf_event_attr *hw_event,
                 pid_t pid,
                 int cpu,
                 int group_fd,
                 unsigned long flags)
{
  int ret;
  ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
  return ret;
}

static bool
initialize_and_start_perf_attr(struct perf_event_attr *pes, int i, __u32 type, __u64 config)
{
  JASSERT(pes);
  pes->type = type;
  pes->size = sizeof(struct perf_event_attr);
  pes->config = config;
  pes->disabled = 1;
  pes->exclude_kernel = 1;
  pes->exclude_hv = 1;
  pes->inherit = 1;
  fd[i] = perf_event_open1(pes, 0, -1, -1, 0);
  if (fd[i] < 0) {
    JWARNING(false)("Error opening leader\n")(pes->config);
    ioctl(fd[i], PERF_EVENT_IOC_DISABLE, 0);
    return false;
  }
  ioctl(fd[i], PERF_EVENT_IOC_RESET, 0);
  ioctl(fd[i], PERF_EVENT_IOC_ENABLE, 0);
  return true;
}

static bool
invoke_ctr()
{
  struct perf_event_attr pe[NUM_OF_PERF_EVENTS];

  memset(pe, 0, sizeof(struct perf_event_attr) * NUM_OF_PERF_EVENTS);
  JTRACE("invoking counters")(getStatsFilename(NULL));
  started = true;

  bool ret = true;

 // ret &= initialize_and_start_perf_attr(&pe[0], 0, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS);
 // ret &= initialize_and_start_perf_attr(&pe[1], 1, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES);
 // ret &= initialize_and_start_perf_attr(&pe[2], 2, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS);
  ret &= initialize_and_start_perf_attr(&pe[0], 0, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
  if (ret) {
    JASSERT(_real_dup2(fd[0], PROTECTED_PERF_CTR1_FD) == PROTECTED_PERF_CTR1_FD)(JASSERT_ERRNO);
    close(fd[0]);
    fd[0] = PROTECTED_PERF_CTR1_FD;
  }
  ret &= initialize_and_start_perf_attr(&pe[1], 1, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS);
  if (ret) {
    JASSERT(_real_dup2(fd[1], PROTECTED_PERF_CTR2_FD) == PROTECTED_PERF_CTR2_FD)(JASSERT_ERRNO);
    close(fd[1]);
    fd[1] = PROTECTED_PERF_CTR2_FD;
  }
// ret &= initialize_and_start_perf_attr(&pe[5], 5, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES);
// ret &= initialize_and_start_perf_attr(&pe[6], 6, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES);
// ret &= initialize_and_start_perf_attr(&pe[7], 7, PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS);

  JTRACE("started all ctrs")(ret)(started)(getStatsFilename(NULL));
  return ret;
}

EXTERNC int
setup_perf_ctr()
{
  return invoke_ctr();
}

#define START_SIGNAL  SIGRTMIN+2
#define STOP_SIGNAL   SIGRTMIN+3
#define handleError(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

static const char*
getStatsFilename(char *fname)
{
  static dmtcp::string filename = "";
  if (fname) {
    JTRACE("Setting filename")(filename)(fname);
    filename = fname;
  }
  return filename.c_str();
}

static void dumpCtrs()
{
  const char *fname = getStatsFilename(NULL);
  if (fname) {
    JTRACE("Dumping perf counters to")(fname);
    FILE *outfp = fopen(fname, "w+");
    if (!outfp) {
      perror("Error opening stats file in w+ mode");
      JASSERT(false);
    }
    read_ctrs(outfp);
    fclose(outfp);

    char arr[PATH_MAX]={0};
    strcpy(arr, fname);
    FILE *temp = fopen(strcat(arr, ".done"), "w+");
    fclose(temp);
  }
}

void startCtrsSignalHandler(int sig, siginfo_t *si, void *uc)
{
  JTRACE("Starting counters")(getStatsFilename(NULL));
  JWARNING(setup_perf_ctr())(getStatsFilename(NULL)).Text("Error setting up perf ctrs.");
}

void resetCtrsSignalHandler(int sig, siginfo_t *si, void *uc)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  JTRACE("In signal handler; Dumping perf counters");
  dumpCtrs();

  /* We set started to true here because we have already reset the
   * counters in dumpCtrs() above.
   */
  started = true;
  DMTCP_PLUGIN_ENABLE_CKPT();
}

static void
setup_handlers()
{
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = startCtrsSignalHandler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(START_SIGNAL, &sa, NULL) == -1) {
      handleError("sigaction");
  }

  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = resetCtrsSignalHandler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(STOP_SIGNAL, &sa, NULL) == -1) {
      handleError("sigaction");
  }
}

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  static char *filename = NULL;
  static bool restartingFromCkpt = false;
  static FILE *outfp = NULL;
  static char statFile[PATH_MAX] = {0};
  static char ovhdFilename[PATH_MAX] = {0};

  switch (event) {
    case DMTCP_EVENT_INIT:
      {
        JTRACE("setting up signal handlers");
        setup_handlers();

        filename = getenv("STATFILE");
        strncpy(statFile, getStatsFilename(filename), PATH_MAX);
        //ovhdFilename = strcpy(ovhdFilename, strcat(statFile, ".ovhd"));
        strcpy(ovhdFilename, strcat(statFile, ".ovhd"));
      }
      break;

    case DMTCP_EVENT_WRITE_CKPT:
      {
        JTRACE("CHKP");
        if (restartingFromCkpt) {
          JTRACE("Will dump perf counters soon; stat filename: ")(getStatsFilename(NULL));
          dumpCtrs();
          restartingFromCkpt = false;
        }

        FILE *tmp = fopen(ovhdFilename, "a+");
        JASSERT(tmp)(JASSERT_ERRNO)(ovhdFilename);
        char buff[20] = {0};
        time_t now = time(NULL);
        strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(tmp, "[CKPT TIMESTAMP] %s\n\n", buff);
        fclose(tmp);
      }
      break;

    case DMTCP_EVENT_RESUME:
      {
        FILE *tmp = fopen(ovhdFilename, "a+");
        JASSERT(tmp)(JASSERT_ERRNO)(ovhdFilename);
        char buff[20] = {0};
        time_t now = time(NULL);
        strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(tmp, "[RESUME TIMESTAMP] %s\n\n", buff);
        fclose(tmp);
      }
      break;

    case DMTCP_EVENT_RESTART:
      {
        JTRACE("DMTCP_EVENT_RESTART");
        restartingFromCkpt = true;
        JASSERT(dmtcp_get_restart_env("STATFILE", statFile, PATH_MAX) == 0);
        filename = statFile;
        getStatsFilename(filename);
        JWARNING(filename != NULL).Text("Could not get the stats filename in the restart event.");
        JTRACE("Filename: ")(filename);

//        ovhdFilename = strcat(statFile, ".ovhd");
        FILE *tmp = fopen(ovhdFilename, "a+");
        JASSERT(tmp)(JASSERT_ERRNO)(ovhdFilename);
        char buff[20] = {0};
        time_t now = time(NULL);
        strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(tmp, "[RESTART TIMESTAMP] %s\n\n", buff);
        fclose(tmp);
      }
      break;

    case DMTCP_EVENT_RESUME_USER_THREAD:
      {
        JTRACE("DMTCP_EVENT_RESUME_USER_THREAD");
      }
      break;
    case DMTCP_EVENT_EXIT:
      {
      }
      break;

    default:
      break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}

#ifdef MYPLUG_DEBUG
# undef DEBUG
#endif
