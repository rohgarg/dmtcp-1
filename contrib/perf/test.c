#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include "dmtcp.h"

int main(){

  long long int a=900000000;

  dmtcp_checkpoint();

  while(a--){
    //sleep(1);
    //printf("%d\n",a);
  }
  dmtcp_checkpoint();
  exit(0);
}
