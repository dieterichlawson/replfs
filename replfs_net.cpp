#include "replfs_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "packets.h"
#include "log.h"
#include <string>

#define USEC_PER_SEC 1000000
#define USEC_PER_MSEC 1000

#define HEARTBEAT_USEC (HEARTBEAT_MSEC * (USEC_PER_MSEC))

static int theSocket;
Sockaddr address;
static Sockaddr groupAddr;
static int dropPercent;

static void convertIncoming(ReplfsPacket* packet);
static void convertOutgoing(ReplfsPacket* packet);
static size_t packetSize(uint8_t type);

static void receivePacket(ReplfsPacket* packet, struct sockaddr* source);
static void incrementTimeout(struct timeval* timeout);
static void subtractTimevals(const struct timeval* one, const struct timeval* two, struct timeval* result);
static inline uint32_t ntohl_wrap(uint32_t in){ return ntohl(in);}
static inline uint32_t htonl_wrap(uint32_t in){ return htonl(in);}

/* Returns the next event*/
void nextEvent(ReplfsEvent* event){
  static bool nextTimeoutInitialized = false;
  static struct timeval nextTimeout;
  if(!nextTimeoutInitialized){
    gettimeofday(&nextTimeout,NULL);
    incrementTimeout(&nextTimeout);
    nextTimeoutInitialized = true;
  }
  struct timeval currTime;
  gettimeofday(&currTime,NULL);
  struct timeval timeTillTimeout;
  subtractTimevals(&nextTimeout,&currTime,&timeTillTimeout);
  fd_set fdmask;
  FD_ZERO(&fdmask);
  FD_SET(theSocket,&fdmask);
  if(select(theSocket+1,&fdmask,NULL,NULL,&timeTillTimeout) > 0){
    receivePacket(event->packet,(struct sockaddr*)&(event->source));
    convertIncoming(event->packet);
    event->type = PACKET_EVENT;
  }else{
    incrementTimeout(&nextTimeout);
    event->type = HEARTBEAT_EVENT;
    memset(&(event->source),0, sizeof(event->source));
    memset(event->packet,0, sizeof(event->packet));
  }
  return;
}

static void receivePacket(ReplfsPacket* packet, struct sockaddr* source){
  socklen_t fromLen = sizeof(struct sockaddr);
  recvfrom(theSocket,packet,sizeof(ReplfsPacket),0,source,&fromLen);
}

static void incrementTimeout(struct timeval* timeout){
  timeout->tv_usec += HEARTBEAT_USEC;
  if(timeout->tv_usec >= USEC_PER_SEC){
    timeout->tv_sec += timeout->tv_usec / USEC_PER_SEC;
    timeout->tv_usec %= USEC_PER_SEC;
  }
}

static void subtractTimevals(const struct timeval* one, const struct timeval* two, struct timeval* result){
  long usecs = (one->tv_sec - two->tv_sec) * USEC_PER_SEC;
  usecs += (one->tv_usec - two->tv_usec);
  result->tv_usec = usecs % USEC_PER_SEC;
  result->tv_sec = usecs / USEC_PER_SEC;
}

int sendPacket(void* packet, uint8_t type){
  if((rand() % 100) < dropPercent){
    LOG("Dropping packet of type 0x%x\n",type);
    return-1;
  }
  ReplfsPacket outerPacket;
  outerPacket.type = type;
  if(packet){
    memcpy(&(outerPacket.body),packet,packetSize(type));
    convertOutgoing(&outerPacket);
  }
  return sendto(theSocket,&outerPacket,packetSize(type),0,(const struct sockaddr*) &groupAddr,sizeof(Sockaddr));
}

static void Error(std::string errorString){
  fprintf(stderr, "ReplFS: %s\n",errorString.c_str());
  perror("ReplFS");
  exit(-1);
}

//Network initialization code
static Sockaddr* resolveHost(register char* name);

/* Gets the network machinery up and running */
void netInit(unsigned short replfsPort, int packetLoss){
  dropPercent = packetLoss;
	Sockaddr* thisHost;
	char buf[128];
	//get the hostname and resolve it
  gethostname(buf, sizeof(buf));
	if ((thisHost = resolveHost(buf)) == (Sockaddr *) NULL){
	  Error("Could not resolve host");
  }
  memcpy(&address,thisHost,sizeof(Sockaddr));
  //get a socket	
  theSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (theSocket < 0) Error("Can't get socket");
  //allow more than one binding to the same socket
	int reuse = 1;
	if (setsockopt(theSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		Error("setsockopt failed (SO_REUSEADDR)");
	}
#ifdef __APPLE__
  if (setsockopt(theSocket, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
		Error("setsockopt failed (SO_REUSEPORT)");
	}
#endif
  //bind the socket to address nullAddr
	Sockaddr nullAddr;
	nullAddr.sin_family = AF_INET;
	nullAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	nullAddr.sin_port = replfsPort;
	if (bind(theSocket, (struct sockaddr *)&nullAddr, sizeof(nullAddr)) < 0){
	  Error("Unable to bind socket");
  }
	//TTL: DO NOT use a value > 32
  u_char ttl = 32;
	if (setsockopt(theSocket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,sizeof(ttl)) < 0) {
		Error("setsockopt failed (IP_MULTICAST_TTL)");
	}
	//join the multicast group
	struct ip_mreq mreq;
	mreq.imr_multiaddr.s_addr = htonl(GROUP);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if(setsockopt(theSocket,IPPROTO_IP,IP_ADD_MEMBERSHIP,(char*)&mreq,sizeof(mreq)) <0){
    Error("setsockopt failed (IP_ADD_MEMBERSHIP)");
  }
  //Get the multi-cast address ready to use
  memcpy(&groupAddr, &nullAddr, sizeof(Sockaddr));
  groupAddr.sin_addr.s_addr = htonl(GROUP);
}

static Sockaddr* resolveHost(register char* name){
  register struct hostent* fhost;
  struct in_addr fadd;
  static Sockaddr socketAddress;
  if((fhost = gethostbyname(name)) != NULL){
    socketAddress.sin_family = fhost->h_addrtype;
    socketAddress.sin_port = 0;
    memcpy(&socketAddress.sin_addr, fhost->h_addr, fhost->h_length);
  } else if(inet_aton(name,(struct in_addr*) &(fadd.s_addr))){
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = 0;
    socketAddress.sin_addr.s_addr = fadd.s_addr;
  } else return NULL;
  return &socketAddress;
}

//Packet conversion routines

static uint32_t convertLong(uint32_t toConvert,bool incoming){
  uint32_t (*convert)(uint32_t) = incoming ? ntohl_wrap : htonl_wrap;
  return convert(toConvert);
}

static void convertRollCallAck(RollCallAckPacket* packet, bool incoming){
  packet->proposedId = convertLong(packet->proposedId,incoming);
}

static void convertOpenFile(OpenFilePacket* packet,bool incoming){
  packet->fileId = convertLong(packet->fileId,incoming);
}

static void convertOpenFileAck(OpenFileAckPacket* packet, bool incoming){
  packet->serverId = convertLong(packet->serverId,incoming);
  packet->fileId = convertLong(packet->fileId,incoming);
}

static void convertWriteBlock(WriteBlockPacket* packet, bool incoming){
  packet->fileId = convertLong(packet->fileId,incoming);
  packet->commitNum = convertLong(packet->commitNum,incoming);
  packet->byteOffset = convertLong(packet->byteOffset,incoming);
  packet->blockSize = convertLong(packet->blockSize,incoming);
}

static void convertCommitRequest(CommitRequestPacket* packet, bool incoming){
  packet->commitNum = convertLong(packet->commitNum,incoming);
  packet->fileId = convertLong(packet->fileId,incoming);
}

static void convertReadyToCommit(ReadyToCommitPacket* packet,bool incoming){
  packet->serverId = convertLong(packet->serverId,incoming);
  packet->fileId = convertLong(packet->fileId,incoming);
  packet->commitNum = convertLong(packet->commitNum,incoming);
}

static void convertCommit(CommitPacket* packet, bool incoming){
  packet->fileId = convertLong(packet->fileId,incoming);
  packet->commitNum = convertLong(packet->commitNum,incoming);
}

static void convertPacket(ReplfsPacket* packet, bool incoming){
  switch(packet->type){
    case ROLL_CALL: break;
    case ROLL_CALL_ACK:
      convertRollCallAck((RollCallAckPacket*)packet->body,incoming);
      break;
    case OPEN_FILE:
      convertOpenFile((OpenFilePacket*)packet->body,incoming);
      break;
    case OPEN_FILE_ACK:
      convertOpenFileAck((OpenFileAckPacket*)packet->body,incoming);
      break;
    case WRITE_BLOCK:
      convertWriteBlock((WriteBlockPacket*)packet->body,incoming);
      break;
    case COMMIT_REQUEST:
      convertCommitRequest((CommitRequestPacket*)packet->body,incoming);
      break;
    case READY_TO_COMMIT:
    case COMMIT_ACK:
    case WRITE_RESEND_REQUEST:
    case ABORT_ACK:
      convertReadyToCommit((ReadyToCommitPacket*)packet->body,incoming);
      break;
    case COMMIT:
    case ABORT:
      convertCommit((CommitPacket*)packet->body,incoming);
      break;
  }
}

static void convertIncoming(ReplfsPacket* packet){
  convertPacket(packet,true);
}

static void convertOutgoing(ReplfsPacket* packet){
  convertPacket(packet,false);
}

static size_t packetSize(uint8_t type){
  size_t result = sizeof(type);
  switch(type){
    case ROLL_CALL: break;
    case ROLL_CALL_ACK: result += sizeof(RollCallAckPacket); break;
    case OPEN_FILE: result += sizeof(OpenFilePacket); break;
    case OPEN_FILE_ACK: result += sizeof(OpenFileAckPacket); break;
    case WRITE_BLOCK: result += sizeof(WriteBlockPacket); break;
    case COMMIT_REQUEST: result += sizeof(CommitRequestPacket); break;
    case READY_TO_COMMIT: result += sizeof(ReadyToCommitPacket); break;
    case COMMIT_ACK: result += sizeof(CommitAckPacket); break;
    case WRITE_RESEND_REQUEST: result += sizeof(WriteResendRequestPacket); break;
    case ABORT: result += sizeof(AbortPacket);
    case COMMIT: result += sizeof(CommitPacket);
    case ABORT_ACK: result += sizeof(AbortAckPacket);
  }
  return result;
}


