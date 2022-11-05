#include <net/net.h>
#include <assert.h>
#include <stdio.h>

#include <common/common.h>

struct Client {
  bool connected;
  Address address;
  double timeout_timer;
  Packet to_send;
 
  uint32_t seq_local;
  uint32_t seq_remote;
  Queue ack_queue;
};

#define TIMEOUT_TIMER 5.0
#define MAX_CLIENTS_CONNECTED 4
struct Server {
  Socket socket;
  Client clients[MAX_CLIENTS_CONNECTED];
  int clients_connected;
  Queue packets_to_process;
  double last_time;
};

Client *ServerGetClient(Server *server, Address addr) {
  for(int i = 0 ; i < MAX_CLIENTS_CONNECTED; ++i) {
    Client *client = server->clients + i;
    if(client->connected && AddressEquals(client->address, addr)) {
      return client;
    }
  }
  return 0;
}

Client *ServerFindFreeClient(Server *server) {
  for(int i = 0 ; i < MAX_CLIENTS_CONNECTED; ++i) {
    Client *client = server->clients + i;
    if(!client->connected) {
      return client;
    }
  }
  return 0;
}

Server *ServerCreate(uint16_t port) {
  Server *result = (Server *)malloc(sizeof(Server));
  memset(result, 0, sizeof(Server));
  result->clients_connected = 0;
  result->socket = SocketCreate();
  SocketOpen(result->socket, port);
  SocketSetNotBloking(result->socket);
  result->last_time = GetTimeInSeconds();
  result->packets_to_process = QueueCreate(256);
  for(int i = 0 ; i < MAX_CLIENTS_CONNECTED; ++i) {
    Client *client = result->clients + i;
    client->connected = false;
    PacketInit(&client->to_send);
    client->ack_queue = QueueCreate(33);
  }

  return result;
}

void ServerDestroy(Server *server) {
  SocketClose(server->socket);
  free(server);
}

void ServerHandleClientsTimeOut(Server *server) {
  double current_time = GetTimeInSeconds();
  for(int i = 0 ; i < MAX_CLIENTS_CONNECTED; ++i) {
    Client *client = server->clients + i;
    if(client->connected) {
      double delta_time = current_time - server->last_time;
      client->timeout_timer += delta_time;
      if(client->timeout_timer >= TIMEOUT_TIMER) {
        client->connected = false;
        client->timeout_timer = 0;
        client->seq_local = 0;
        client->seq_remote = 0;
        QueueClear(&client->ack_queue);
        printf("client time out\n");
      }
    }
  }
  server->last_time = current_time;
}

void ServerHandleConnection(Server *server, Address addr) {
  Client *client = ServerFindFreeClient(server);
  if(client) {
    client->address = addr;
    client->connected = true;
    ++server->clients_connected;
    printf("new client connected\n");
  }
}

void ServerRecievePackets(Server *server) {
  Address addr;
  Packet pkt;
  SocketRecieve(server->socket, &addr, &pkt);
  if(PacketIsValid(&pkt)) {
    Client *client = ServerGetClient(server, addr);
    if(client) {
      uint32_t seq = PacketGetHeader(&pkt)->seq;
      if(seq > client->seq_remote) { 
        client->seq_remote = seq;
        QueuePush(&server->packets_to_process, &pkt);
        if(QueueIsFull(&client->ack_queue)) {
          QueuePop(&client->ack_queue);
        }
        QueuePush(&client->ack_queue, &pkt);
      }
      client->timeout_timer = 0;

    } else {
      ServerHandleConnection(server, addr);
    }
  }
}

void ServerUpdate(Server *server, float dt){
  printf("process queue size:%d\n", server->packets_to_process.size);
  while(!QueueIsEmpty(&server->packets_to_process)) {
    Packet *packet = QueuePop(&server->packets_to_process);
    PacketHeader *header = PacketGetHeader(packet);
    printf("packet seq:%d, ack:%d, bf:%x\n", header->seq, header->ack, header->ack_bitfield);
  }
}

uint32_t ServerCalculateAckBitField(Server *server, Client *client) {
  (void)server;
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

void ServerSendPackets(Server *server) {
  for(int i = 0 ; i < MAX_CLIENTS_CONNECTED; ++i) {
    Client *client = server->clients + i;
    if(client->connected) {
      PacketHeader *header = PacketGetHeader(&client->to_send);
      header->id = NetMagicNumber();
      header->seq = ++client->seq_local;
      header->ack = client->seq_remote;
      header->ack_bitfield = ServerCalculateAckBitField(server, client);
      SocketSend(server->socket, client->address, &client->to_send);
      PacketInit(&client->to_send);
    }
  }
}

int main() {
  NetInit();

  double dt = 0.033;
  double accumulator = 0.0;
  double last_time = GetTimeInSeconds();

  Server *server = ServerCreate(3000);

  while(true) {
    double current_time = GetTimeInSeconds();
    double delta_time = current_time - last_time;
    last_time = current_time;
    accumulator += delta_time;

    ServerHandleClientsTimeOut(server);
    ServerRecievePackets(server);

    while(accumulator >= dt) {
      accumulator -= dt;
      ServerUpdate(server, (float)dt);
      ServerSendPackets(server);
    }
  }

  ServerDestroy(server);
  NetShutDown();
}