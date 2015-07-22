#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Reads from fd until count bytes are read, or
 * newline encountered.
 *
 * Side effects: Copies the characters, including
 * the newline, read from the fd into the buf.
 *
 * Returns num of chars read on success;
 *         -1 on read failure; and
 *         -2 if the buffer is too small
 */
int
readLine(int fd, char *buf, int count)
{
  int i = 0;
  char c;
  if (fd < 0 || buf == NULL) return -1;
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
  return (count - i) == 0 ? -2 : i;
}
