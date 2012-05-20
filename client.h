#ifndef _replfs_client_h
#define _replfs_client_h
#ifdef __cplusplus
extern "C" {
#endif

extern int InitReplFs(unsigned short portNum, int packetLoss, int numServers);

extern int OpenFile(char *name);

extern int WriteBlock(int fd, char *buffer, int byteOffset, int blockSize);

extern int Commit(int fd);

extern int Abort(int fd);

extern int CloseFile(int fd);

#ifdef __cplusplus
}
#endif
#endif
