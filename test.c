#include "client.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#define DEFAULT_PORT 44018

#define MAX_COMMITS 500

#define MAX_WRITES_PER_COMMIT 127
#define NUM_SERVERS 1

int descriptors[5];

void RandomWrite(int fd);
char* generateRandomString(int size);
void randomNumberWrite(int fd);

void randomMultiFileTest();
void writeNumbersTest();
void sequentialWriteTest();
void openAbortTest();
void openCommitTest();
void dontTrucateTest();

int main(const int argc, const char* argv[]){
  InitReplFs(DEFAULT_PORT,10,NUM_SERVERS);
  writeNumbersTest();
  randomMultiFileTest();
  sequentialWriteTest();
  openAbortTest();
  openCommitTest();
  dontTrucateTest();
}

void dontTrucateTest(){
  int fd = OpenFile("numbers.txt");
  WriteBlock(fd,"I'm so very happy",17,17);
  Commit(fd);
  WriteBlock(fd,"I'm so very sad",17,15);
  Abort(fd);
  CloseFile(fd);
}

void openAbortTest(){
  int fd = OpenFile("should_not_exist.txt");
  Abort(fd);
}

void openCommitTest(){
  int fd = OpenFile("should_be_empty.txt");
  Commit(fd);
  CloseFile(fd);
}

void writeNumbersTest(){
  int fd = OpenFile("numbers.txt");
  for(int j = 0; j < MAX_COMMITS; j++){
    for(int i = 0; i < MAX_WRITES_PER_COMMIT; i++){
      randomNumberWrite(fd);
    }
    if(rand() % 2 == 0){
      Commit(fd);
    }else {
      Abort(fd);
    }
  }
  CloseFile(fd);
}

void randomNumberWrite(int fd){
  int size = rand() % (512/8);
  int start = rand() % (2000 - size);
  char str[(size*8)+1];
  for(int i = 0; i < size; i++){
    sprintf(str+(i*8), "%8d",i+start);
  }
  str[(size*8)] = '\0';
  WriteBlock(fd,str,start*8,size *8);
}

void sequentialWriteTest(){
  int fd = OpenFile("sequential_write.txt");
  for(int i = 0; i < MAX_WRITES_PER_COMMIT; i++){
    char blah[3];
    sprintf(blah,"%3d\n",i);
    WriteBlock(fd,blah,0,4);
  }
  Commit(fd);
  CloseFile(fd);
}

void randomMultiFileTest(){
  srand(time(NULL));
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
  for(int i = 0; i < 5; i++){
    CloseFile(descriptors[i]);
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
