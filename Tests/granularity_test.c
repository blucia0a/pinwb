#include <stdio.h>
#include <stdlib.h>

char *f;

int main(int argc, char *argv[]){
 
  f = (char*)malloc(24 * sizeof(char));
   
  int i;
  for(i = 0; i < 24; i++){
    f[i] = 'a' + i;
  }

  unsigned long a = *((unsigned long *)f);
  fprintf(stderr,"%lu\n",a);
 
  return 0; 

}
