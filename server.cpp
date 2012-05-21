#include "packets.h"
#include "replfs_net.h"
#include "stdio.h"
#include <stdbool.h>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <stdlib.h>
#include <time.h>
#include "log.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <limits.h>

#define DEFAULT_PORT 44018

#define WORD_SIZE_BYTES ((int) sizeof(unsigned int))
#define WORD_SIZE_BITS (WORD_SIZE_BYTES * CHAR_BIT)

static std::string mountPath;
static uint32_t serverId;

static std::set<uint32_t> closedFileIds;
static std::set<uint32_t> openFileIds;
static std::map<uint32_t,std::string> filenames;
static std::map<uint32_t,std::vector<WriteBlockPacket*> > stagedWrites;
static std::map<uint32_t,uint32_t> commitNums;
static std::set<uint32_t> readyToCommit;

extern Sockaddr address;

void listen();

void handlePacket(void* packet, uint8_t type);
void handleRollCall();
void handleOpenFile(OpenFilePacket* packet);
void handleWriteBlock(WriteBlockPacket* packet);
void handleCommitRequest(CommitRequestPacket* packet);
void handleCommit(CommitPacket* packet);
void handleAbort(AbortPacket* packet);

int main(const int argc, char* argv[]){
  unsigned short portNum;
  int dropPercent;
  if(argc == 1){
    portNum = DEFAULT_PORT;
    dropPercent = 10;
    mountPath = "./";
  }else if(argc == 7){
    portNum = atoi(argv[2]);
    dropPercent = atoi(argv[6]);
    mountPath = argv[4];
    if(mountPath[mountPath.length()-1] != '/'){
      mountPath +='/';
    }
    int ret = mkdir(mountPath.c_str(),0777);
    if(ret == -1){
      printf("machine already in use\n");
      return -1;
    }
  }
  LOG("Starting server...\n");
  netInit(portNum,dropPercent);
  LOG("Server started, waiting for roll call\n");
  listen();
}

void listen(){
  ReplfsEvent event;
  ReplfsPacket packet;
  event.packet = &packet;
  while(true){
    nextEvent(&event);
    if(event.type == HEARTBEAT_EVENT){
    }else{
      handlePacket(&(packet.body),packet.type);
    }
  }
}

void handlePacket(void* packet, uint8_t type){
  switch(type){
    case ROLL_CALL:
      handleRollCall();
      break;
    case OPEN_FILE:
      handleOpenFile((OpenFilePacket*)packet);
      break;
    case WRITE_BLOCK:
      handleWriteBlock((WriteBlockPacket*)packet);
      break;
    case COMMIT_REQUEST:
      handleCommitRequest((CommitRequestPacket*)packet);
      break;
    case COMMIT:
      handleCommit((CommitPacket*)packet);
      break;
    case ABORT:
      handleAbort((AbortPacket*)packet);
      break;
  }
}

/*
 * Responds to a RollCall packet sent out by the client by generating 
 * a new server id and sending it out via a RollCallAck packet.
 * ServerId is a 32 bit unsigned int which maxes out at 2^32 -1, 
 * but RAND_MAX is 2^31 -1. This means that rand cannot generate 
 * random numbers large enough to fill up a 32 bit unsigned int. 
 * To compensate for this, we call rand twice and add the results together.
 * Because 2*RAND_MAX = 2^32 -2, which is only 1 away from UINT_MAX,
 * the randomness of rand() isn't affected by much.
 */
void handleRollCall(){
  //re-seed the random number generator
  unsigned int randSeed = (unsigned int) address.sin_addr.s_addr;
  randSeed ^= (unsigned int) getpid();
  struct timeval curtime;
  gettimeofday(&curtime,NULL);
  randSeed ^= (unsigned int) curtime.tv_usec;
  srand(randSeed);
  serverId = rand();
  serverId += rand();
  RollCallAckPacket packet;
  packet.proposedId = serverId;
  sendPacket(&packet,ROLL_CALL_ACK);
  LOG("RollCall packet received\n");
  LOG("New proposed ID generated: %u\n",serverId);
}

void handleOpenFile(OpenFilePacket* packet){
  LOG("OpenFile packet received for filename %s\n",packet->fileName);
  OpenFileAckPacket outgoing;
  outgoing.serverId = serverId;
  outgoing.fileId = packet->fileId;
  if(openFileIds.count(packet->fileId) == 0){
    openFileIds.insert(packet->fileId);
    std::string filename = (char*) packet->fileName;
    filenames[packet->fileId] = filename;
    commitNums[packet->fileId] = 1;
    stagedWrites[packet->fileId] = std::vector<WriteBlockPacket*>();
    LOG("New fileId stored.\n");
  }else{
    LOG("Already had file %u open\n",packet->fileId);
  }
  sendPacket(&outgoing,OPEN_FILE_ACK);
}

void handleWriteBlock(WriteBlockPacket* packet){
  LOG("Received write block packet\n");
  if(packet->commitNum != commitNums[packet->fileId]){
    LOG("Received write block for non-open commit. Discarding...\n");
    return;
  }
  WriteBlockPacket* write =(WriteBlockPacket*) malloc(sizeof(WriteBlockPacket));
  if(write == NULL){
    LOG("Error allocating space for write block packet. crashing\n");
    return;
  }
  memcpy(write,packet,sizeof(WriteBlockPacket));
  std::vector<WriteBlockPacket*>::iterator it;
  for(it = stagedWrites[packet->fileId].begin(); it != stagedWrites[packet->fileId].end(); ++it){
    if((*it)->writeNum == packet->writeNum){
      LOG("Received duplicate write\n");
      free(write);
      return;
    }else if((*it)->writeNum > packet->writeNum) break;
  }
  stagedWrites[packet->fileId].insert(it, write);
  LOG("Staged writes: %zu\n",stagedWrites[packet->fileId].size());
  LOG("Write %u staged for file:%u, commit:%u\n",packet->writeNum,packet->fileId,packet->commitNum);
}

void sendWriteResendRequest(uint32_t fileId, uint32_t commitNum, uint8_t numWrites);

void handleCommitRequest(CommitRequestPacket* packet){
  LOG("Received Commit request for file %u, commit %u with %u expected writes\n",
      packet->fileId,packet->commitNum,packet->finalWriteNum);
  //if the file is open and the commit num is correct
  if(openFileIds.count(packet->fileId) != 0 &&
     commitNums[packet->fileId] == packet->commitNum){
    if(stagedWrites[packet->fileId].size() != packet->finalWriteNum){
      LOG("Commit requested, but %zu of %d writes present. Requesting resends...\n",
          stagedWrites[packet->fileId].size(),packet->finalWriteNum);
      sendWriteResendRequest(packet->fileId,packet->commitNum,packet->finalWriteNum);
    }else{
      LOG("All writes present, ready to commit!\n");
      ReadyToCommitPacket outgoing;
      outgoing.serverId = serverId;
      outgoing.fileId = packet->fileId;
      outgoing.commitNum = packet->commitNum;
      sendPacket(&outgoing,READY_TO_COMMIT);
    }
  }
}

void sendWriteResendRequest(uint32_t fileId, uint32_t commitNum, uint8_t numWrites){
  std::vector<WriteBlockPacket*>::iterator it;
  uint8_t writeArray[16];
  memset(writeArray,0xff,16);
  for(it = stagedWrites[fileId].begin(); it != stagedWrites[fileId].end(); ++it){
    int writeNum = (*it)->writeNum;
    void* address = ((unsigned int*) writeArray) + (writeNum/WORD_SIZE_BITS);
    *(unsigned int*)address &= ~(1 << (writeNum % WORD_SIZE_BITS));
  }
  WriteResendRequestPacket request;
  request.serverId = serverId;
  request.fileId = fileId;
  request.commitNum = commitNum;
  memcpy(request.requestedWrites,writeArray,16);
  sendPacket(&request,WRITE_RESEND_REQUEST);
}

void writeCommitToDisk(uint32_t fileId, uint32_t commitNum){
  std::string filePath = mountPath + filenames[fileId];
  int fd = open(filePath.c_str(),O_WRONLY | O_CREAT, 0777);
  if(fd == -1){
    LOG("Error opening file %s\n",filePath.c_str());
    return;
  }
  std::vector<WriteBlockPacket*>::iterator it;
  for(it = stagedWrites[fileId].begin(); it != stagedWrites[fileId].end(); ++it){
    WriteBlockPacket* packet = *it;
    lseek(fd,packet->byteOffset,SEEK_SET);
    int writeSize = write(fd,packet->data,packet->blockSize);
    if(writeSize == -1 || writeSize != (int) packet->blockSize){
      LOG("Unable to perform write %u \n",packet->writeNum);
    }
  }
  LOG("Commit writing finished. File:%u Commit:%u\n",fileId,commitNum);
  if(close(fd) != 0) LOG("Error closing file %s\n",filePath.c_str());
}

void cleanupAfterCommit(uint32_t fileId, uint32_t commitNum){
  LOG("Cleaning up after commit. File:%u commit:%u\n",fileId,commitNum);
  std::vector<WriteBlockPacket*>::iterator it;
  for(it = stagedWrites[fileId].begin(); it != stagedWrites[fileId].end(); ++it){
    free(*it);
  }
  stagedWrites[fileId].clear();
  readyToCommit.erase(fileId);
  commitNums[fileId]++;
}

void closeFile(int fd){
  LOG("Closing file %u.\n",fd);
  openFileIds.erase(fd);
  filenames.erase(fd);
  stagedWrites.erase(fd);
  commitNums.erase(fd);
  closedFileIds.insert(fd);
}

void handleCommit(CommitPacket* packet){
  LOG("Received final Commit order\n");
  if(packet->commitNum == commitNums[packet->fileId]){
    LOG("Have not already performed commit. Writing to disk...\n");
    writeCommitToDisk(packet->fileId,packet->commitNum);
    cleanupAfterCommit(packet->fileId,packet->commitNum);
    if(packet->closeFlag) closeFile(packet->fileId);
  }
  if(packet->commitNum <= commitNums[packet->fileId] || closedFileIds.count(packet->fileId) != 0){
    CommitAckPacket outgoing;
    outgoing.serverId = serverId;
    outgoing.fileId = packet->fileId;
    outgoing.commitNum = packet->commitNum;
    LOG("Commit already performed. Acknowledging...\n");
    sendPacket(&outgoing,COMMIT_ACK);
  }
}

void handleAbort(AbortPacket* packet){
  LOG("Received abort packet for file %u\n",packet->fileId);
  if(openFileIds.count(packet->fileId) != 0 && commitNums[packet->fileId] == packet->commitNum){
    LOG("Performing abort operation\n");
    cleanupAfterCommit(packet->fileId,packet->commitNum);
    if(packet->closeFlag) closeFile(packet->fileId);
  }
  if(commitNums[packet->fileId] >= packet->commitNum || closedFileIds.count(packet->fileId) != 0){
    LOG("Sending abort confirmation\n");
    AbortAckPacket outgoing;
    outgoing.serverId = serverId;
    outgoing.commitNum = packet->commitNum;
    outgoing.fileId = packet->fileId;
    sendPacket(&outgoing,ABORT_ACK);
  }
}
