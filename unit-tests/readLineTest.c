#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "minunit.h"

static char *filename = "/tmp/empty-env.txt";
static int gfd = -1;

extern int readLine(int fd, char *buf, int count);

void
test_setup()
{
  gfd = open(filename, O_CREAT, S_IRWXU);
  assert(close(gfd) == 0);
  gfd = open(filename, O_RDONLY);
}

void
test_teardown()
{
  close(gfd);
}

static int
create_short_file()
{
  system("echo \"ad asdf lkj-; ad;fioj\" > /tmp/env-short");
  return open("/tmp/env-short", O_RDONLY);
}

static void
cleanup_short_file(int fd)
{
  assert(close(fd) == 0);
}

MU_TEST(test_empty_file)
{
  char buf[10] = {0};
  mu_assert(readLine(gfd, buf, 10) == 0, "empty file should return 0");
}

MU_TEST(test_null_buf)
{
  char *buf = NULL;
  mu_assert(readLine(gfd, buf, 10) == -1, "null buffer should return -1");
}

MU_TEST(test_short_buf)
{
#undef SIZE
#define SIZE 10
  char buf[SIZE] = {0};
  int tempfd = create_short_file();
  mu_assert(readLine(tempfd, buf, SIZE) == -2, "short buf should return -2");
  mu_assert(strncmp("ad asdf lk", buf, SIZE) == 0, "short buf should fill up values");
  cleanup_short_file(tempfd);
}

MU_TEST(test_regular_buf)
{
#undef SIZE
#define SIZE 100
  char buf[SIZE] = {0};
  int tempfd = create_short_file();
  mu_assert(readLine(tempfd, buf, SIZE) == 22, "regular buf should return 22");
  mu_check(strncmp("ad asdf lkj-; ad;fioj", buf, SIZE) < 0);
  mu_assert(strncmp("ad asdf lkj-; ad;fioj\n", buf, SIZE) == 0, "regular buf should fill up values including \\n");
  cleanup_short_file(tempfd);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);
  MU_RUN_TEST(test_empty_file);
  MU_RUN_TEST(test_null_buf);
  MU_RUN_TEST(test_short_buf);
  MU_RUN_TEST(test_regular_buf);
}

int main(int argc, char *argv[])
{
  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  return 0;
}
