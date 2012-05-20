#ifndef _replfs_net_h
#define _replfs_het_h

#include "packets.h"
#include <netdb.h>

#define PACKET_EVENT 0x01
#define HEARTBEAT_EVENT 0x02

#define HEARTBEAT_MSEC 200

#define GROUP 0xe0010101

/* Give a network address a shorter name */
typedef struct sockaddr_in Sockaddr;

/* Holds information about an event in the system */
struct ReplfsEvent {
  short type;
  Sockaddr source;
  ReplfsPacket* packet;
};
typedef struct ReplfsEvent ReplfsEvent;

/*
 * Connects to the network and performs housekeeping
 * to get the system ready to send and receive packets.
 * Must be called before packets are sent or received.
 */
void netInit(unsigned short replfsPort, int dropPercent);

/*
 * Sends the supplied packet out via UDP Multicast.
 * packet should be a pointer to one of the types in
 * packets.h, *not* a ReplfsPacket
 */
int sendPacket(void* packet, uint8_t type);

/*
 * Returns the next event in the system.
 * Will block until that event occurs.
 */
void nextEvent(ReplfsEvent* event);

#endif
