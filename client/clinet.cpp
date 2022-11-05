#include <net/net.h>
#include <assert.h>
#include <stdio.h>

#include <common/common.h>

#define TIMEOUT_TIMER 5.0
struct Client {
  Socket socket;
  Address server_address;
  Queue packets_to_process;
  Packet to_send;
  bool connected;
  double timeout_timer;
  double last_time;
  Queue ack_queue; // TODO: create a custom queue with only ack not the complete packet
  uint32_t seq_local;
  uint32_t seq_remote;
};

Client *ClientCreate(char a, char b, char c, char d, uint16_t port) {
  Client *result = (Client *)malloc(sizeof(Client));
  memset(result, 0, sizeof(Client));
  result->socket = SocketCreate();
  SocketSetNotBloking(result->socket);
  result->last_time = GetTimeInSeconds();
  result->server_address = AddressCreate(a, b, c, d, port);
  result->packets_to_process = QueueCreate(64);
  result->ack_queue = QueueCreate(33);
  PacketInit(&result->to_send);
  result->connected = true;
  return result;
}

void ClientDestroy(Client *client) {
  SocketClose(client->socket);
  free(client);
}

void ClientHandleTimeOut(Client *client) {
    if(client->connected) {
      double current_time = GetTimeInSeconds();
      double delta_time = current_time - client->last_time;
      client->last_time = current_time;
      client->timeout_timer += delta_time;
      if(client->timeout_timer >= TIMEOUT_TIMER) {
        client->connected = false;
        printf("server time out\n");
      }
    }
}

void ClientRecievePackets(Client *client) {
  Address addr;
  Packet pkt;
  SocketRecieve(client->socket, &addr, &pkt);
  if(PacketIsValid(&pkt)) {
    uint32_t seq = PacketGetHeader(&pkt)->seq;
    if(seq > client->seq_remote) {
      client->seq_remote = seq;
      QueuePush(&client->packets_to_process, &pkt);
      if(QueueIsFull(&client->ack_queue)) {
        QueuePop(&client->ack_queue);
      }
      QueuePush(&client->ack_queue, &pkt);
    }
    client->timeout_timer = 0;
  }
}

void ClientUpdate(Client *client, float dt) {
  printf("process queue size:%d\n", client->packets_to_process.size);
  printf("ack queue size:%d\n", client->ack_queue.size);
  while(!QueueIsEmpty(&client->packets_to_process)) {
    Packet *packet = QueuePop(&client->packets_to_process);
    PacketHeader *header = PacketGetHeader(packet);
    printf("packet seq:%d, ack:%d, bf:%x\n", header->seq, header->ack, header->ack_bitfield);
  }
}

uint32_t ClientCalculateAckBitField(Client *client) {
  uint32_t bitfield = 0;
  PacketNode *node = client->ack_queue.dummy->next;
  // NOTE: loop all the ack packet except the last one
  while(node != client->ack_queue.dummy->prev) {
    Packet *packet = &node->packet;
    uint32_t packet_ack = PacketGetHeader(packet)->seq;
    uint32_t index = client->seq_remote - packet_ack;
    bitfield |= (1 << (32 - index));
    node = node->next;
  }
  return bitfield;
}

void ClientSendPackets(Client *client) {
  PacketHeader *header = PacketGetHeader(&client->to_send);
  header->id = NetMagicNumber();
  header->seq = ++client->seq_local;
  header->ack = client->seq_remote;
  header->ack_bitfield = ClientCalculateAckBitField(client);
  SocketSend(client->socket, client->server_address, &client->to_send);
  PacketInit(&client->to_send);
}

int main() {
  NetInit();

  double dt = 0.033;
  double accumulator = 0.0;
  double last_time = GetTimeInSeconds();

  Client *client = ClientCreate(127, 0, 0, 1, 3000);

  while(true) {
    double current_time = GetTimeInSeconds();
    double delta_time = current_time - last_time;
    last_time = current_time;
    accumulator += delta_time;

    ClientHandleTimeOut(client);
    ClientRecievePackets(client);

    if(client->connected) {
      while(accumulator >= dt) {
        accumulator -= dt;
        ClientUpdate(client, (float)dt);
        ClientSendPackets(client);
      }
    }
  }

  ClientDestroy(client);
  NetShutDown();
}
