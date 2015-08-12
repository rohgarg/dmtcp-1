#include <pthread.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "dmtcp.h"

class ThreadBarrier
{
    ThreadBarrier(const ThreadBarrier &);
    ThreadBarrier &operator = (const ThreadBarrier &);

    int mLastLockStatus;

    static pthread_mutex_t mLock;
public :
    static pthread_t mOwner;
    ThreadBarrier();
    ~ThreadBarrier();

    void lock();
    void release();
};

pthread_t ThreadBarrier::mOwner = 0;
pthread_mutex_t ThreadBarrier::mLock = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

ThreadBarrier::ThreadBarrier() :  mLastLockStatus(0)
{
    lock();
}

void ThreadBarrier::lock()
{
    mLastLockStatus = pthread_mutex_lock(&mLock);
    mOwner = pthread_self();
}

void ThreadBarrier::release()
{
    if (mLastLockStatus != EDEADLK)
    {
        mOwner = 0;
        int ret = pthread_mutex_unlock(&mLock);
        if (ret != 0) {
          printf("pthread_mutex_unlock failed with return value: %d\n", ret);
        }
    }
}

ThreadBarrier::~ThreadBarrier()
{
    release();
}
  
void synchronize(int count)
{
  ThreadBarrier barrier;
  printf("Synchronize loop called %d\n", count);

  if(count == 1)
  {
      int result = dmtcp_checkpoint();
      if(result == DMTCP_AFTER_RESTART) {
          printf("Restart via DMTCP successful\n");
      } else if (result == DMTCP_AFTER_CHECKPOINT) {
          printf("Checkpoint via DMTCP successful\n");
      } else {
          printf("Checkpoint/restart via DMTCP failed\n");
      }
  } 

}

int main(int argc, char **argv)
{
   long long int count = 1;
   while(count)
   {
       synchronize(count);
       sleep(1);
       count++;
   }
  return 0;
}
