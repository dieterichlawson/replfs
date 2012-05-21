#include "client.h"
#include "replfs_net.h"
#include "packets.h"
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include "log.h"
#include <set>
#include <map>
#include <vector>
#include <limits.h>

#define WORD_SIZE_BYTES ((int) sizeof(unsigned int))
#define WORD_SIZE_BITS (WORD_SIZE_BYTES * CHAR_BIT)

#define DEFAULT_PORT 44016

#define ERR_RETURN -1
#define OK_RETURN 0

#define USEC_PER_SEC 1000000

#define MAX_ROLLCALL_ROUNDS 3
#define MAX_TIMEOUTS_PER_ROLLCALL 3

#define MAX_TIMEOUTS_PER_OPEN 10
#define MAX_COMMIT_LATENCY_SEC 2
#define MAX_TIMEOUTS_PER_COMMIT 10
#define MAX_TIMEOUTS_PER_ABORT 10

#define MAX_FILESIZE_BYTES (1024 *1024)

struct OpenFile {
  uint32_t fileId;
  uint32_t commitNum;
  uint8_t writeNum;
};

static std::set<uint32_t> serverIds;
static std::set<uint32_t> openFileIds;
static std::map <uint32_t,struct OpenFile*> openFiles;
static std::map<uint32_t,std::vector<WriteBlockPacket*> >stagedWrites;

static int RollCall(size_t expectedNumServers);

int InitReplFs(unsigned short portNum, int packetLoss, int numServers){
  srand(time(NULL));
  LOG("Initializing network connection...\n");
  netInit(portNum,packetLoss);
  LOG("Network initialized.\n");
  return RollCall(numServers);
}

static int RollCall(size_t expectedNumServers){
  LOG("Sending RollCall\n");
  for(int roundNum=0; roundNum < MAX_ROLLCALL_ROUNDS && serverIds.size() != expectedNumServers; roundNum++){
    serverIds.clear();
    if(sendPacket(NULL,ROLL_CALL) < 0){
      LOG("Error sending packet...\n");
    }
    LOG("RollCall sent, round %d.\n",roundNum+1);
    int timeoutNum = 0;
    ReplfsEvent event;
    ReplfsPacket packet;
    event.packet = &packet;
    while(timeoutNum < MAX_TIMEOUTS_PER_ROLLCALL && serverIds.size() != expectedNumServers){
      nextEvent(&event);
      if(event.type == HEARTBEAT_EVENT){
        timeoutNum++;
      }else if(packet.type == ROLL_CALL_ACK){
        RollCallAckPacket* p = (RollCallAckPacket*) &(packet.body);
        serverIds.insert(p->proposedId);
        LOG("Saw new server with ID %u\n",p->proposedId);
      }
    }
  }
  if(serverIds.size() == expectedNumServers){
    LOG("Expected number of servers accounted for. Initialization complete.\n");
    return OK_RETURN;
  }else{
    LOG("Saw %zu servers, expected %zu. Initialization failed.\n",serverIds.size(),expectedNumServers);
    return ERR_RETURN;
  }
}

int OpenFile(char *name){
  static uint32_t nextFileId = 1;
  //create and send the first OpenFile packet
  OpenFilePacket packet;
  packet.fileId = nextFileId;
  nextFileId++;
  strncpy((char*) packet.fileName,name,MAX_FILENAME_SIZE);
  LOG("Created new fileId \'%u\' for file %s\n",packet.fileId,name);
  sendPacket(&packet,OPEN_FILE);
  //wait for acknowledgements
  int timeoutNum = 0;
  ReplfsEvent event;
  ReplfsPacket incoming;
  event.packet = &incoming;
  std::set<uint32_t> remainingServers = serverIds;
  while(timeoutNum < MAX_TIMEOUTS_PER_OPEN && remainingServers.size() >0){
    nextEvent(&event);
    if(event.type == HEARTBEAT_EVENT){
      timeoutNum++;
      LOG("Resending OpenFile packet for file %u\n",packet.fileId);
      sendPacket(&packet,OPEN_FILE);
    }else if(event.type == PACKET_EVENT && incoming.type == OPEN_FILE_ACK){
      OpenFileAckPacket* openFileAck = (OpenFileAckPacket*) &incoming.body;
      if(openFileAck->fileId == packet.fileId){
        remainingServers.erase(openFileAck->serverId);
        LOG("Received OpenFileAck from server %u. %zu servers remaining\n",
            openFileAck->serverId,remainingServers.size());
      }
    }
  }
  //if all the servers acknowledged...
  if(remainingServers.size() == 0){
    LOG("All servers acknowledged OpenFile.\n");
    LOG("Creating housekeeping data...\n");
    //insert the id into the list of ids
    openFileIds.insert(packet.fileId);
    //create the file struct and add it to the map
    struct OpenFile* file = new struct OpenFile;
    file->fileId = packet.fileId;
    file->commitNum = 1;
    file->writeNum = 0;
    openFiles[packet.fileId] = file;
    stagedWrites[packet.fileId] = std::vector<WriteBlockPacket*>();
    return packet.fileId;
  }else{
    LOG("Some servers did not acknowledge OpenFile. File could not be opened.\n");
    return ERR_RETURN;
  }
}

int WriteBlock(int fd, char *buffer, int byteOffset, int blockSize){
  if(openFileIds.count(fd) == 0 ||
     blockSize > MAX_WRITE_SIZE ||
     byteOffset + blockSize > MAX_FILESIZE_BYTES){
    return ERR_RETURN;
  }
  if(buffer == NULL) return OK_RETURN;
  WriteBlockPacket* outgoing = (WriteBlockPacket*) malloc(sizeof(WriteBlockPacket));
  if(outgoing == NULL){
    LOG("Error mallocing enough size for a write block packet. Crashing...\n");
    return ERR_RETURN;
  }
  outgoing->fileId = fd;
  struct OpenFile* file = openFiles[fd];
  outgoing->commitNum = file->commitNum;
  if(file->writeNum >= 127){
    LOG("Exceeded max writes for file %u commit %u\n",fd,file->commitNum);
    return ERR_RETURN;
  }else{
    file->writeNum++;
    LOG("Incremented writenum for file %u commit %u to %u\n",fd,file->commitNum,file->writeNum);
  }
  outgoing->writeNum = file->writeNum;
  outgoing->byteOffset = byteOffset;
  outgoing->blockSize = blockSize;
  memcpy(outgoing->data,buffer,blockSize);
  sendPacket(outgoing,WRITE_BLOCK);
  stagedWrites[fd].push_back(outgoing);
  LOG("Sent WriteBlock packet\n");
  return blockSize;
}

void initializeServerTimes(std::map<uint32_t,struct timeval>& serverTimes);
bool serversAlive(std::map<uint32_t,struct timeval>& serverTimes);
int finishCommit(uint32_t fd, uint32_t commitNum,bool closeFlag);
void resendWrites(uint32_t fileId, uint32_t commitNum, uint8_t reqWrites[16]);
int performCommit(int fd,bool closeFlag);

int Commit(int fd){
  return performCommit(fd,false);
}

int performCommit(int fd,bool closeFlag){
  if(openFileIds.count(fd) == 0) return ERR_RETURN;
  LOG("Sending out a commit request for file %u\n",fd);
  CommitRequestPacket commitRequest;
  commitRequest.fileId = fd;
  commitRequest.commitNum = openFiles[fd]->commitNum;
  commitRequest.finalWriteNum = openFiles[fd]->writeNum;
  //listen for responses
  ReplfsEvent event;
  ReplfsPacket incoming;
  event.packet = &incoming;
  //holds the last time we've seen the servers
  std::map<uint32_t,struct timeval> serverTimes;
  //holds a list of servers that have yet to ack
  std::set<uint32_t> remainingServers = serverIds;
  initializeServerTimes(serverTimes);
  sendPacket(&commitRequest,COMMIT_REQUEST);
  LOG("Waiting for %zu servers to come to readiness...\n",remainingServers.size());
  while(serversAlive(serverTimes) && remainingServers.size() > 0){
    //resend the commit request
    nextEvent(&event);
    if(event.type == HEARTBEAT_EVENT){
      sendPacket(&commitRequest,COMMIT_REQUEST);
    }if(event.type == PACKET_EVENT && incoming.type == READY_TO_COMMIT){
      ReadyToCommitPacket* rtcPacket = (ReadyToCommitPacket*) incoming.body;
      if(rtcPacket->fileId == commitRequest.fileId && rtcPacket->commitNum == commitRequest.commitNum){
        //if the server is ready to commit, we remove them from 
        //the time tracking data structures
        LOG("Server %u ready to commit. %zu remaining...\n",
            rtcPacket->serverId,remainingServers.size());
        remainingServers.erase(rtcPacket->serverId);
        serverTimes.erase(rtcPacket->serverId);
      }
    }else if(event.type == PACKET_EVENT && incoming.type == WRITE_RESEND_REQUEST){
      WriteResendRequestPacket* request = (WriteResendRequestPacket*) incoming.body;
      //update the last seen time
      struct timeval curTime;
      gettimeofday(&curTime,NULL);
      serverTimes[request->serverId] = curTime;
      //resend the requested writes
      resendWrites(request->fileId,request->commitNum,request->requestedWrites);
    }
  }
  if(remainingServers.size() == 0){
    LOG("Commit phase 1 completed. Finishing commit...\n");
    return finishCommit(fd,commitRequest.commitNum,closeFlag);
  }else{
    LOG("Commit failed in phase 1.\n");
    return ERR_RETURN;
  }
}

void initializeServerTimes(std::map<uint32_t,struct timeval>& serverTimes){
  struct timeval curTime;
  gettimeofday(&curTime,NULL);
  std::set<uint32_t>::iterator serverIdIt;
  for(serverIdIt = serverIds.begin();serverIdIt != serverIds.end(); ++ serverIdIt){
    serverTimes[*serverIdIt] = curTime;
  }
}

bool serversAlive(std::map<uint32_t,struct timeval>& serverTimes){
  struct timeval curTime;
  struct timeval serverTime;
  gettimeofday(&curTime,NULL);
  std::map<uint32_t,struct timeval>::iterator serverIt;
  for(serverIt = serverTimes.begin();serverIt != serverTimes.end(); ++serverIt){
    serverTime = (*serverIt).second;
    long usecs = (curTime.tv_sec - serverTime.tv_sec) * USEC_PER_SEC;
    usecs += (curTime.tv_usec - serverTime.tv_usec);
    if(usecs >= MAX_COMMIT_LATENCY_SEC * USEC_PER_SEC){
      LOG("Server %u died during commit phase 1.\n",(*serverIt).first);
      return false;
    }
  }
  return true;
}

void cleanupAfterCommit(uint32_t fileId, uint32_t commitNum);

void closeFile(int fd){
  LOG("Closing file %u\n.",fd);
  openFileIds.erase(fd);
  delete openFiles[fd];
  openFiles.erase(fd);
  stagedWrites.erase(fd);
}

int finishCommit(uint32_t fd, uint32_t commitNum, bool closeFlag){
  LOG("Sending commit packet\n");
  CommitPacket commit;
  commit.fileId = fd;
  commit.commitNum = commitNum;
  commit.closeFlag = closeFlag;
  sendPacket(&commit,COMMIT);
  LOG("Waiting for commit acks\n");
  int timeoutNum = 0;
  ReplfsEvent event;
  ReplfsPacket incoming;
  event.packet = &incoming;
  std::set<uint32_t> remainingServers = serverIds;
  while(timeoutNum < MAX_TIMEOUTS_PER_COMMIT && remainingServers.size() >0){
    nextEvent(&event);
    if(event.type == HEARTBEAT_EVENT){
      timeoutNum++;
      LOG("Resending Commit packet for file %u\n",commit.fileId);
      sendPacket(&commit,COMMIT);
    }else if(event.type == PACKET_EVENT && incoming.type == COMMIT_ACK){
      CommitAckPacket* commitAck = (CommitAckPacket*) &incoming.body;
      if(commitAck->fileId == commit.fileId && commitAck->commitNum == commit.commitNum){
        remainingServers.erase(commitAck->serverId);
        LOG("Received CommitAck from server %u\n",commitAck->serverId);
      }
    }
  }
  if(remainingServers.size() == 0){
    LOG("Commit successful!\n");
    cleanupAfterCommit(fd,commitNum);
    if(closeFlag) closeFile(fd);
    return OK_RETURN;
  }else{
    LOG("Some servers did not ack commit. Commit failed.\n");
    return ERR_RETURN;
  }
}

void cleanupAfterCommit(uint32_t fileId, uint32_t commitNum){
  LOG("Cleaning up after commit. File:%u commit:%u\n",fileId,commitNum);
  std::vector<WriteBlockPacket*>::iterator it;
  for(it = stagedWrites[fileId].begin(); it != stagedWrites[fileId].end(); it++){
    free(*it);
  }
  stagedWrites[fileId].clear();
  openFiles[fileId]->commitNum++;
  openFiles[fileId]->writeNum = 0;
}

void resendWrites(uint32_t fileId, uint32_t commitNum, uint8_t reqWrites[16]){
  std::vector<WriteBlockPacket*>::iterator it;
  for(it = stagedWrites[fileId].begin(); it!= stagedWrites[fileId].end(); ++it){
    WriteBlockPacket* write = (*it);
    void* address = ((unsigned int*) reqWrites) + (write->writeNum / WORD_SIZE_BITS);
    if(*(unsigned int*)address & (1 << (write->writeNum % WORD_SIZE_BITS))){
      LOG("Resending write %u for file %u commit %u\n",
          write->writeNum,fileId,commitNum);
      sendPacket(write,WRITE_BLOCK);
    }
  }
}

int performAbort(int fd, bool closeFlag);

int Abort(int fd){
  return performAbort(fd,false);
}

int performAbort(int fd, bool closeFlag){
  if(openFileIds.count(fd) == 0) return ERR_RETURN;
  cleanupAfterCommit(fd,openFiles[fd]->commitNum);
  AbortPacket abort;
  abort.fileId = fd;
  abort.commitNum = openFiles[fd]->commitNum-1;
  abort.closeFlag = closeFlag;
  sendPacket(&abort,ABORT);
  //wait for acknowledgements
  int timeoutNum = 0;
  ReplfsEvent event;
  ReplfsPacket incoming;
  event.packet = &incoming;
  std::set<uint32_t> remainingServers = serverIds;
  while(timeoutNum < MAX_TIMEOUTS_PER_ABORT && remainingServers.size() >0){
    nextEvent(&event);
    if(event.type == HEARTBEAT_EVENT){
      timeoutNum++;
      LOG("Resending Abort packet for file %u\n",abort.fileId);
      sendPacket(&abort,ABORT);
    }else if(event.type == PACKET_EVENT && incoming.type == ABORT_ACK){
      AbortAckPacket* abortAck = (AbortAckPacket*) incoming.body;
      if(abortAck->fileId == abort.fileId && abortAck->commitNum == abort.commitNum){
        remainingServers.erase(abortAck->serverId);
        LOG("Received Abort Ack from server %u. %zu remaining.\n",
            abortAck->serverId,remainingServers.size());
      }
    }
  }
  return OK_RETURN;
}

int CloseFile(int fd){
  if(stagedWrites[fd].size() != 0){
    return performCommit(fd,true);
  }else{
    return performAbort(fd,true);
  }
}
