#ifndef _packets_h
#define _packets_h

//for uint32_t etc...
#include <netdb.h>

//The packet types
#define ROLL_CALL 0x01
#define ROLL_CALL_ACK 0x02
#define OPEN_FILE 0x03
#define OPEN_FILE_ACK 0x04
#define WRITE_BLOCK 0x05
#define COMMIT_REQUEST 0x06
#define READY_TO_COMMIT 0x07
#define COMMIT 0x08
#define COMMIT_ACK 0x09
#define WRITE_RESEND_REQUEST 0x0A
#define ABORT 0x0B
#define ABORT_ACK 0x0C

#define MAX_FILENAME_SIZE 128
#define MAX_WRITE_SIZE 512
#define MAX_WRITES_PER_COMMIT 128

struct RollCallAckPacket {
  uint32_t proposedId;
} __attribute__((packed));
typedef struct RollCallAckPacket RollCallAckPacket;

struct OpenFilePacket {
  uint32_t fileId;
  uint8_t fileName[MAX_FILENAME_SIZE];
} __attribute__((packed));
typedef struct OpenFilePacket OpenFilePacket;

struct OpenFileAckPacket {
  uint32_t serverId;
  uint32_t fileId;
} __attribute__((packed));
typedef struct OpenFileAckPacket OpenFileAckPacket;

struct WriteBlockPacket {
  uint32_t fileId;
  uint32_t commitNum;
  uint8_t writeNum;
  uint32_t byteOffset;
  uint32_t blockSize;
  uint8_t data[MAX_WRITE_SIZE];
} __attribute__((packed));
typedef struct WriteBlockPacket WriteBlockPacket;

struct CommitRequestPacket {
  uint32_t fileId;
  uint32_t commitNum;
  uint8_t finalWriteNum;
} __attribute__((packed));
typedef struct CommitRequestPacket CommitRequestPacket;

struct ReadyToCommitPacket {
  uint32_t serverId;
  uint32_t fileId;
  uint32_t commitNum;
} __attribute__((packed));
typedef struct ReadyToCommitPacket ReadyToCommitPacket;

struct CommitPacket {
  uint32_t fileId;
  uint32_t commitNum;
  uint8_t closeFlag;
} __attribute__((packed));
typedef struct CommitPacket CommitPacket;

struct CommitAckPacket {
  uint32_t serverId;
  uint32_t fileId;
  uint32_t commitNum;
} __attribute__((packed));
typedef struct CommitAckPacket CommitAckPacket;

struct WriteResendRequestPacket {
  uint32_t serverId;
  uint32_t fileId;
  uint32_t commitNum;
  uint8_t requestedWrites[16];
} __attribute__((packed));
typedef struct WriteResendRequestPacket WriteResendRequestPacket;

struct AbortPacket {
  uint32_t fileId;
  uint32_t commitNum;
  uint8_t closeFlag;
} __attribute__((packed));
typedef struct AbortPacket AbortPacket;

struct AbortAckPacket {
  uint32_t serverId;
  uint32_t fileId;
  uint32_t commitNum;
} __attribute__((packed));
typedef struct AbortAckPacket AbortAckPacket;

struct ReplfsPacket {
  uint8_t type;
  uint8_t body[sizeof(WriteBlockPacket)];
} __attribute__((packed));
typedef struct ReplfsPacket ReplfsPacket;

#endif
