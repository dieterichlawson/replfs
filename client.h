#ifndef _replfs_client_h
#define _replfs_client_h
#ifdef __cplusplus
extern "C" {
#endif

int InitReplFs(unsigned short portNum, int packetLoss, int numServers);

int OpenFile(char *name);

int WriteBlock(int fd, char *buffer, int byteOffset, int blockSize);

int Commit(int fd);

int Abort(int fd);

int CloseFile(int fd);

#ifdef __cplusplus
}
#endif
#endif
