#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#define MAXVAL 1000

struct wonk{
  int a;
} *shrdPtr;

pthread_mutex_t lock;
pthread_barrier_t bar;

struct wonk *getNewVal(struct wonk**old){
  free(*old);
  *old = NULL;
  struct wonk *newval = (struct wonk*)malloc(sizeof(struct wonk));
  newval->a = 1;
  return newval;
}

void *updaterThread(void *arg){

  pthread_barrier_wait(&bar);

  int i;
  for(i = 0; i < 10; i++){    
    pthread_mutex_lock(&lock);
    struct wonk*newval = getNewVal(&shrdPtr);
    pthread_mutex_unlock(&lock);
  
  
    pthread_mutex_lock(&lock);
    shrdPtr = newval;
    pthread_mutex_unlock(&lock);
  }

}

void *accessorThread(void *arg){

  int *result = (int*)malloc(sizeof(int));; 
  *result = 0;
  pthread_barrier_wait(&bar);
  while(*result < MAXVAL){ 
    pthread_mutex_lock(&lock);//400a4e
    if(shrdPtr != NULL){
      pthread_mutex_unlock(&lock);
     

      pthread_mutex_lock(&lock);//400a6e
      //assert(shrdPtr != NULL);
      *result += shrdPtr->a;     
      pthread_mutex_unlock(&lock);
  
    }
    pthread_mutex_unlock(&lock);
    usleep(10 + (rand() % 100) );


  }
  
  pthread_exit(result); 
}

int main(int argc, char *argv[]){
  int res = 0;
  shrdPtr= (struct wonk*)malloc(sizeof(struct wonk));
  shrdPtr->a = 1;

  pthread_mutex_init(&lock,NULL);
  pthread_barrier_init(&bar,NULL,2);

  pthread_t acc,upd;
  pthread_create(&acc,NULL,accessorThread,(void*)shrdPtr);
  pthread_create(&upd,NULL,updaterThread,(void*)shrdPtr);

  pthread_join(upd,NULL);
  pthread_join(acc,(void*)&res);
  fprintf(stderr,"Final value of res was %d\n",res); 
}
