#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include "minunit.h"

#define EFD "/proc/self/environ"

static int gfd = -1;

extern int dmtcp_get_restart_env(const char *name,   // IN
                                 char *value,        // OUT
                                 int maxvaluelen);
static void
trim_whitespace(char *str)
{
  char *end = str + strlen(str) - 1;
  while(end > str && isspace(*end)) end--;
  *(end+1) = 0;
}

int
dmtcp_protected_environ_fd()
{
  return gfd;
}

static void
test_setup()
{
  gfd = open(EFD, O_RDONLY);
  assert(gfd > 0);
}

static void
test_teardown()
{
  assert(close(gfd) == 0);
}

MU_TEST(test_empty_file)
{
#undef SIZE
#define SIZE 10
  const char *name = "name";
  char value[SIZE] = {0};
  int temp = gfd;
  gfd = -1;
  int rc = dmtcp_get_restart_env(name, value, SIZE);
  mu_assert(rc == -4,
            "invalid fd should return INTERNAL_ERROR(-4)");
  gfd = temp;
}

MU_TEST(test_nonexistent_env_var)
{
#undef SIZE
#define SIZE 10
  const char *name = "name";
  char value[SIZE] = {0};
  int rc = dmtcp_get_restart_env(name, value, SIZE);
  mu_assert(rc == -1,
            "non-existent env var should return -1");
}

MU_TEST(test_existent_env_var)
{
#undef SIZE
#define SIZE 20
  const char *name = "USER";
  char value[SIZE] = {0};
  int rc = dmtcp_get_restart_env(name, value, SIZE);
  mu_assert(rc == 0,
            "existent env var should return 0");
  trim_whitespace(value);
  mu_assert(strncmp(value,getenv(name),SIZE) == 0,
            "value for existent env var should match"
            " that returned by getenv()");
}

MU_TEST(test_short_buf)
{
#undef SIZE
#define SIZE 10
  const char *name = "PATH";
  char value[SIZE] = {0};
  int rc = dmtcp_get_restart_env(name, value, SIZE);
  mu_assert(rc == -2,
            "short buf should return -2");
}

MU_TEST(test_short_dmtcp_buf)
{
#undef SIZE
#define SIZE 10
  const char *name = "PATH";
  char value[SIZE] = {0};
  int temp = gfd;
  system("cat /proc/self/environ > /tmp/unit-test-101");
  system("cat /proc/self/environ >> /tmp/unit-test-101");
  gfd = open("/tmp/unit-test-101", O_RDONLY);
  assert(gfd > 0);
  int rc = dmtcp_get_restart_env(name, value, SIZE);
  mu_assert(rc == -3,
            "dmtcp short buf should return -3");
  close(gfd);
  gfd = temp;
}

MU_TEST(test_multiple_lines)
{
#undef SIZE
#define SIZE 20
  const char *name = "USER";
  char value[SIZE] = {0};
  int temp = gfd;
  const char *cmd = "cat /proc/self/environ | tr '\\0' '\\n'> /tmp/unit-test-101";
  system(cmd);
  gfd = open("/tmp/unit-test-101", O_RDONLY);
  assert(gfd > 0);
  int rc = dmtcp_get_restart_env(name, value, SIZE);
  mu_assert_int_eq(rc, 0);
  trim_whitespace(value);
  mu_assert(strncmp(value,getenv(name),SIZE) == 0,
            "value for existent env var should match"
            " that returned by getenv()");
  close(gfd);
  gfd = temp;
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);
  MU_RUN_TEST(test_empty_file);
  MU_RUN_TEST(test_nonexistent_env_var);
  MU_RUN_TEST(test_existent_env_var);
  MU_RUN_TEST(test_short_buf);
  MU_RUN_TEST(test_short_dmtcp_buf);
  MU_RUN_TEST(test_multiple_lines);
}

int main(int argc, char *argv[])
{
  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  return 0;
}
