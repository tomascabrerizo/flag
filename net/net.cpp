#include "net.h"
#include <assert.h>
#include <math.h>

static LARGE_INTEGER frequency;

inline double GetTimeInSeconds() {
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  return ((double)counter.QuadPart / (double)frequency.QuadPart);
}

void PerciseSleep(double seconds) {
  static double estimate = 5e-3;
  static double mean = 5e-3;
  static double m2 = 0;
  static int64_t count = 1;

  while(seconds > estimate) {
    double start = GetTimeInSeconds();
    Sleep(1);
    double end = GetTimeInSeconds();
    double observerd = end - start;
    seconds -= observerd;
    ++count;
    double delta = (observerd - mean);
    mean += (delta / (double)count);
    m2 += delta * (observerd - mean);
    double stddev = sqrt(m2 / (double)(count - 1));
    estimate = mean + stddev;
  }

  double start = GetTimeInSeconds();
  while((GetTimeInSeconds() - start) < seconds);
}

void NetInit() {
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
  timeBeginPeriod(1);
  QueryPerformanceFrequency(&frequency);
}

void NetShutDown() {
  WSACleanup();
}

Arena *ArenaCreate(uint64_t size) {
  Arena *result = (Arena *)malloc(sizeof(Arena) + size);
  result->data = (char *)result + sizeof(Arena);
  result->size = size;
  result->used = 0;
  return result;
}

void ArenaDestroy(Arena *arena) {
  free(arena);
}

void *ArenaPush(Arena *arena, uint64_t size) {
  assert((arena->used + size) <= arena->size);
  void *result = (void *)(arena->data + arena->used);
  arena->used += size;
  return result;
}

void ArenaClear(Arena *arena) {
  arena->used = 0;
}

void PacketInit(Packet *packet) {
  packet->start = sizeof(PacketHeader);
  packet->end = sizeof(PacketHeader);
}

void PacketPushInt(Packet *packet, int a) {
  assert(packet->end + sizeof(int) <= MAX_PACKET_SIZE);
  int *dst = (int *)(packet->data + packet->end);
  *dst = a;
  packet->end += sizeof(int);
}

void PacketPushUInt(Packet *packet, uint32_t a) {
  assert(packet->end + sizeof(uint32_t) <= MAX_PACKET_SIZE);
  uint32_t *dst = (uint32_t *)(packet->data + packet->end);
  *dst = a;
  packet->end += sizeof(uint32_t);
}

void PacketPushFloat(Packet *packet, float a) {
  assert(packet->end + sizeof(float) <= MAX_PACKET_SIZE);
  float *dst = (float *)(packet->data + packet->end);
  *dst = a;
  packet->end += sizeof(float);
}

void PacketPushDouble(Packet *packet, double a) {
  assert(packet->end + sizeof(double) <= MAX_PACKET_SIZE);
  double *dst = (double *)(packet->data + packet->end);
  *dst = a;
  packet->end += sizeof(double);
}

int PacketPopInt(Packet *packet) {
  int a = *((int *)(packet->data + packet->start));
  packet->start += sizeof(int);
  return a;
}

uint32_t PacketPopUInt(Packet *packet) {
  uint32_t a = *((uint32_t *)(packet->data + packet->start));
  packet->start += sizeof(uint32_t);
  return a;
}

float PacketPopFloat(Packet *packet) {
  float a = *((float *)(packet->data + packet->start));
  packet->start += sizeof(float);
  return a;
}

double PacketPopDouble(Packet *packet) {
  double a = *((double *)(packet->data + packet->start));
  packet->start += sizeof(double);
  return a;
}

bool PacketIsValid(Packet *packet) {
  return ((PacketHeader *)packet)->id == *((uint32_t *)id) && (packet->end > 0);
}

PacketHeader *PacketGetHeader(Packet *packet) {
  return (PacketHeader *)packet;
}

PacketNode *PacketNodeAlloc(Queue *queue) {
  if(queue->free) {
    PacketNode *result = queue->free;
    queue->free = queue->free->next;
    return result;
  }
  return (PacketNode *)ArenaPush(queue->arena, sizeof(PacketNode));
}

void PacketNodeFree(Queue *queue, PacketNode *packet_node) {
  packet_node->next = queue->free;
  queue->free = packet_node;
}

Queue QueueCreate(uint32_t max_size) {
  Queue result;
  result.arena = ArenaCreate((max_size+1)*sizeof(PacketNode));
  result.free = 0;
  result.size = 0;
  result.max_size = max_size;
  result.dummy = PacketNodeAlloc(&result);
  result.dummy->next = result.dummy;
  result.dummy->prev = result.dummy;
  return result;
}

void QueueDestroy(Queue queue) {
  ArenaDestroy(queue.arena);
}

void QueuePush(Queue *queue, Packet *packet) {
  ++queue->size;
  PacketNode *packet_node = PacketNodeAlloc(queue);
  memcpy(&packet_node->packet, packet, sizeof(Packet));
  packet_node->next = queue->dummy;
  packet_node->prev = queue->dummy->prev;
  packet_node->next->prev = packet_node;
  packet_node->prev->next = packet_node;
}

Packet *QueuePop(Queue *queue) {
  assert(queue->dummy->next != queue->dummy);
  --queue->size;
  PacketNode *packet_node = queue->dummy->next;
  packet_node->next->prev = packet_node->prev;
  packet_node->prev->next = packet_node->next;
  PacketNodeFree(queue, packet_node);
  return &packet_node->packet;
}

void QueueClear(Queue *queue) {
  ArenaClear(queue->arena);
  queue->free = 0;
  queue->size = 0;
  queue->dummy = PacketNodeAlloc(queue);
  queue->dummy->next = queue->dummy;
  queue->dummy->prev = queue->dummy;

}

bool QueueIsFull(Queue *queue) {
  return (queue->size == queue->max_size);
}

bool QueueIsEmpty(Queue *queue) {
  return queue->size == 0;
}

Address AddressCreate(char a, char b, char c, char d, uint16_t port) {
  Address result;
  result.address = (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | ((uint32_t)d << 0));
  result.port = port;
  return result;
}

bool AddressEquals(Address a, Address b) {
  return (a.address == b.address) && (a.port == b.port);
}

Socket SocketCreate() {
  Socket handle = (Socket)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  assert(handle > 0);
  return handle;
}

void SocketClose(Socket socket) {
  closesocket(socket);
}

bool SocketOpen(Socket socket, uint16_t port) {
  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  return (bind(socket, (const sockaddr *)&address, sizeof(sockaddr_in)) >= 0);
}

bool SocketSetNotBloking(Socket socket) {
  DWORD nonBlocking = 1;
  return (ioctlsocket(socket, FIONBIO, &nonBlocking) != -1);
}

void SocketRecieve(Socket socket, Address *src, Packet *packet) {
  sockaddr_in from;
  int fromLength = sizeof(from);
  int bytes = recvfrom(socket, (char *)packet->data, MAX_PACKET_SIZE, 0, (sockaddr *)&from, &fromLength);

  if(bytes > 0) {
    packet->start = sizeof(PacketHeader);
    packet->end = (uint32_t)bytes;
  } else {
    packet->start = 0;
    packet->end = 0;
  }
  
  src->address = ntohl(from.sin_addr.s_addr);
  src->port = ntohs(from.sin_port);
}

void SocketSend(Socket socket, Address dst, Packet *packet) {
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(dst.address);
  addr.sin_port = htons(dst.port);
  int bytes_send = sendto(socket, (const char *)packet->data, (int)packet->end, 0, (const sockaddr *)&addr, sizeof(sockaddr_in));
  assert((int)packet->end == bytes_send);
}