#include "client.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#define DEFAULT_PORT 44017

#define MAX_COMMITS 200 

#define MAX_WRITES_PER_COMMIT 127
#define NUM_SERVERS 4

int descriptors[5];

void RandomWrite(int fd);
char* generateRandomString(int size);

int main(const int argc, const char* argv[]){
  InitReplFs(DEFAULT_PORT,10,NUM_SERVERS);
  int fd = OpenFile("hello.txt");
  for(int i = 0; i < 100; i++){
    char blah[3];
    sprintf(blah,"%d\n",i);
    WriteBlock(fd,blah,0,2);
  }
  Abort(fd);
}

void randomTest(){
  srand(time(NULL));
  InitReplFs(DEFAULT_PORT,10,NUM_SERVERS);
  descriptors[0] = OpenFile("1.txt");
  descriptors[1] = OpenFile("2.txt");
  descriptors[2] = OpenFile("3.txt");
  descriptors[3] = OpenFile("4.txt");
  descriptors[4] = OpenFile("5.txt");
  int numCommits = MAX_COMMITS;
  printf("Performing %d commits\n",numCommits);
  for(int i = 0; i < numCommits; i++){
    int numWrites = rand() % MAX_WRITES_PER_COMMIT;
    for(int j = 0; j < numWrites; j++){
      RandomWrite(descriptors[rand()%5]);
    }
    int tossup = rand()%3;
    if(tossup == 0){
      Commit(descriptors[rand()%5]);
    }else if(tossup == 1){
      Abort(descriptors[rand()%5]);
    }else{
      int d = rand()%5;
      CloseFile(descriptors[d]);
      char blah[6];
      sprintf(blah,"%d.txt",d+1);
      descriptors[d] = OpenFile(blah);
    }
  }
}

void RandomWrite(int fd){
  int size = rand() % 512;
  int offset = rand() % ((1024 * 1024) - size - 1);
  char* str = generateRandomString(size);
  WriteBlock(fd,str,offset,size);
  free(str);
}

char* generateRandomString(int size){
  char* str =(char*) malloc(size);
  for(int i = 0; i < size; i++){
    str[i] = (rand() % 94) + ' ';
  }
  return str;
}
