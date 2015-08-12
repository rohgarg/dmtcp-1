/* FILE: pthread-mutex.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2015 Rohan Garg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <syscall.h>
#include <sys/types.h>
#include <vector>

#include "dmtcp.h"
#include "jassert.h"

#ifdef PTHREAD_MUTEX_PLUGIN_DEBUG
# undef JTRACE
# define JTRACE JNOTE
#endif

#define handleError(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

using namespace std;

/* Globals */
bool present = false;
bool patched = false;
bool isRestart = false;

typedef struct {
  pthread_mutex_t* mutex;
  bool present;
  bool patched;
  bool locked;
} mymutex;

vector<mymutex*> mutexList;

/* File local functions  */
static void
patch_mutex_owner(pthread_mutex_t *mutex, pid_t newOwner)
{
  mutex->__data.__owner = newOwner;
}

#ifdef EXPERIMENTAL
/* XXX: we can possibly patch all the locked mutexes at restart
 * time, w/o waiting for an unlock.
 */
static void
process_mutexes()
{
}
#endif

#define __real_pthread_mutex_lock  NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_unlock  NEXT_FNC(pthread_mutex_unlock)

extern "C" int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
  for (unsigned int i = 0; i < mutexList.size(); i++) {
    if (mutexList[i]->mutex == mutex) {
      mutexList[i]->locked = true;
      return __real_pthread_mutex_lock(mutex);
    }
  }
  mymutex *m = new mymutex;
  m->mutex = mutex;
  m->present = true;
  m->patched = false;
  m->locked = true;
  mutexList.push_back(m);
  return __real_pthread_mutex_lock(mutex);
}

extern "C" int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  for (unsigned int i = 0; i < mutexList.size(); i++) {
    if (mutexList[i]->mutex == mutex)  {
      if (isRestart && mutexList[i]->locked && !mutexList[i]->patched) {
        int ptid = syscall(SYS_gettid); // FIXME: need a direct call here
#if 0
        int vtid = syscall(SYS_gettid);
        ptid = dmtcp_virtual_to_real_pid(vtid);
        JNOTE("something ") (vtid) (ptid);
#endif
        patch_mutex_owner(mutex, ptid);
        mutexList[i]->patched = true;
      }
      mutexList[i]->locked = false;
      return __real_pthread_mutex_unlock(mutex);
    }
  }
  return __real_pthread_mutex_unlock(mutex);
}

extern "C" void
dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  static timer_t timerid = 0;
  static int doneInitialization = 0;
  sigset_t mask;

  switch (event) {
    case DMTCP_EVENT_INIT:
      {
        JTRACE("The plugin has been initialized.");
        break;
      }
    case DMTCP_EVENT_WRITE_CKPT:
      {
        JTRACE("*** The plugin is being called before checkpointing. ***");
        break;
      }
    case DMTCP_EVENT_THREADS_RESUME:
      {
        if (data->resumeInfo.isRestart) {
          JTRACE("The plugin is now restarting from checkpointing.");
          isRestart = true;
        } else {
          JTRACE("The process is now resuming after checkpoint.");
        }
        break;
      }
    default:
      break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
