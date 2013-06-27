#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#define MAXVAL 1000

int a;

pthread_mutex_t lock;
//pthread_barrier_t bar;


void *accessorThread(void *arg){

  int i;
  //pthread_barrier_wait(&bar);
  for(i = 0; i < MAXVAL; i++){ 
    pthread_mutex_lock(&lock);
    a++;     
    pthread_mutex_unlock(&lock);
  }

  pthread_exit(NULL); 
}

int main(int argc, char *argv[]){
  int res = 0;
  a = 0;

  pthread_mutex_init(&lock,NULL);
  //pthread_barrier_init(&bar,NULL,2);

  pthread_t acc1,acc2;
  pthread_create(&acc1,NULL,accessorThread,NULL);
  pthread_create(&acc2,NULL,accessorThread,NULL);

  pthread_join(acc1,NULL);
  pthread_join(acc2,NULL);
  fprintf(stderr,"Final value of res was %d\n",a); 
}
