#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

extern int dmtcp_protected_environ_fd();
extern int readLine(int fd, char *buf, int count);

#define SUCCESS 0
#define NOTFOUND -1
#define TOOLONG -2
#define DMTCP_BUF_TOO_SMALL -3
#define INTERNAL_ERROR -4
#define NULL_PTR -5
#define MAXSIZE 3000

int
dmtcp_get_restart_env(const char *name,   // IN
                      char *value,        // OUT
                      int maxvaluelen)
{
  int env_fd = dup(dmtcp_protected_environ_fd());
  if(env_fd == -1) { return INTERNAL_ERROR; }
  lseek(env_fd, 0, SEEK_SET);
  int namelen = strlen(name);
  *value = '\0'; // Default is null string

  int rc = NOTFOUND; // Default is -1: name not found

  char env_buf[MAXSIZE] = {0}; // All "name=val" strings must be shorter than this.

  if (name == NULL || value == NULL) {
    close(env_fd);
    return NULL_PTR;
  }

  char *pos = NULL;

  while (rc == NOTFOUND) {
   memset(env_buf, 0, MAXSIZE);
   // read a flattened name-value pairs list
   int count = readLine(env_fd, env_buf, MAXSIZE);
   if (count == 0) {
     break;
   } else if (count == -1) {
     rc = INTERNAL_ERROR;
   } else if (count == -2) {
     rc = DMTCP_BUF_TOO_SMALL;
   } else {
     char *start_ptr = env_buf;
     // iterate over the flattened list of name-value pairs
     while (start_ptr - env_buf < sizeof(env_buf)) {
       pos = NULL;
       if (strncmp(start_ptr, name, namelen) == 0) {
         if ((pos = strchr(start_ptr, '='))) {
           strncpy(value, pos + 1, maxvaluelen);
           if (strlen(pos+1) >= maxvaluelen) {
             rc = TOOLONG; // value does not fit in the user-provided value buffer
             break;
           }
         }
         rc = SUCCESS;
         break;
       }
       // skip over a name-value pair
       start_ptr += strlen(start_ptr) + 1;
     }
   }
  }

  close(env_fd);
  return rc;
}
