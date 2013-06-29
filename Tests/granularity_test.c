#include <stdio.h>
#include <stdlib.h>

char *f;

int main(int argc, char *argv[]){
 
  f = (char*)malloc(24 * sizeof(char));
   
  fprintf(stderr,"*========================================\n");
  int i;
  for(i = 0; i < 24; i++){
    f[i] = 'a' + i;
    fprintf(stderr,"*===================%x=====================\n",f[i]);
    fprintf(stderr,"*========================================\n");
  }

  unsigned long a = *((unsigned long *)f);
  fprintf(stderr,"===================%lu=====================\n",a);
 
  return 0; 

}
