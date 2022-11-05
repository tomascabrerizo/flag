#ifndef NET_H
#define NET_H

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#include <Windows.h>
#include <stdint.h>

// TODO: manage this global variable
inline double GetTimeInSeconds();
void PerciseSleep(double seconds);

void NetInit();
void NetShutDown();

struct Arena {
  char *data;
  uint64_t size;
  uint64_t used;
};

Arena *ArenaCreate(uint64_t size);
void ArenaDestroy(Arena *arena);
void *ArenaPush(Arena *arena, uint64_t size);
void ArenaClear(Arena *arena);

static char id[4] = {'f', 'l', 'a', 'g'};
inline uint32_t NetMagicNumber() {
  return *((uint32_t*)id);
}

struct Address {
  uint32_t address;
  uint16_t port;
};

typedef uint64_t Socket;

struct PacketHeader {
  uint32_t id;
  uint32_t seq;
  uint32_t ack;
  uint32_t ack_bitfield;
};

#define MAX_PACKET_SIZE 1024
struct Packet {
  char data[MAX_PACKET_SIZE];
  uint32_t end;
  uint32_t start;
};

struct PacketNode {
  PacketNode *next;
  PacketNode *prev;
  Packet packet;
};

struct Queue {
  Arena *arena;
  PacketNode *dummy;
  PacketNode *free;
  uint32_t size;
  uint32_t max_size;
};

void PacketInit(Packet *packet);

void PacketPushInt(Packet *packet, int a);
void PacketPushUInt(Packet *packet, uint32_t a);
void PacketPushFloat(Packet *packet, float a);
void PacketPushDouble(Packet *packet, double a);

int PacketPopInt(Packet *packet);
uint32_t PacketPopUInt(Packet *packet);
float PacketPopFloat(Packet *packet);
double PacketPopDouble(Packet *packet);

bool PacketIsValid(Packet *packet);
PacketHeader *PacketGetHeader(Packet *packet);

PacketNode *PacketNodeAlloc(Queue *queue);
void PacketNodeFree(Queue *queue, PacketNode *packet_node);

Queue QueueCreate(uint32_t max_size);
void QueueDestroy(Queue queue);
void QueuePush(Queue *queue, Packet *packet);
Packet *QueuePop(Queue *queue);
void QueueClear(Queue *queue);
bool QueueIsFull(Queue *queue);
bool QueueIsEmpty(Queue *queue);

Address AddressCreate(char a, char b, char c, char d, uint16_t port);
bool AddressEquals(Address a, Address b);

Socket SocketCreate();
void SocketClose(Socket socket);
bool SocketOpen(Socket socket, uint16_t port);
bool SocketSetNotBloking(Socket socket);
void SocketRecieve(Socket socket, Address *src, Packet *packet);
void SocketSend(Socket socket, Address dst, Packet *packet);

#endif