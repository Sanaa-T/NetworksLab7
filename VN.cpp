#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
using namespace std;

char VN_id;

const char MSG_CONNECT = 1;
const char MSG_LINK_STATE = 2;
const int BUF_SZ = 4096;


unsigned char buf[BUF_SZ];
int buf_len = 0;

struct vicino {
  char id;
  sockaddr_in sa;
  int cost;
};

vicino vicini[26];
int n_vicini = 0;

struct sockaddr_in ON_sa;
struct sockaddr_in VN_sa;

in_addr get_local_ip() {
  struct in_addr ip;
  memset(&ip, 0, sizeof(ip));
  char host[256];
  gethostname(host, sizeof(host));

  addrinfo hints, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  if (getaddrinfo(host, NULL, &hints, &res) != 0) {
    fprintf(stderr, "can't resolve %s\n", host);
    exit(1);
  }

  for (addrinfo *p = res; p != NULL; p = p->ai_next) {
    in_addr a = ((sockaddr_in *)p->ai_addr)->sin_addr;
    if ((ntohl(a.s_addr) >> 24) == 127) continue; // dont want no loopback
    ip = a;
    break;
  }
  freeaddrinfo(res);
  printf("VN using ip addr %s\n", inet_ntoa(ip));
  return ip;
}


// copied from beej's guide - as it is
int sendall(int s, unsigned char *buf, int *len)
{
  int total = 0; // how many bytes we've sent
  int bytesleft= *len; // how many we have left to send
  int n;
  while(total < *len) {
    n = send(s, buf+total, bytesleft, 0);
    if (n == -1) { break; }
    total += n;
    bytesleft -= n;
  }
  *len = total; // return number actually sent here
  return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}


void send_connect_msg(int tcp_fd){
  // msg is gonna be (ip, port) so 4 + 2 bytes
  int len = 9;
  unsigned char msg[len];
  msg[0] = MSG_CONNECT; // msg type
  msg[1] = 0; // body len 2 bytes and len is 4 + 2
  msg[2] = 6;
  memcpy(msg + 3, &VN_sa.sin_addr, 4); // 4 byte ip
  memcpy(msg + 7, &VN_sa.sin_port, 2); // 2 byte port

  if (sendall(tcp_fd, msg, &len) < 0) {
    printf("kya VN banega re tu\n");
    exit(1);
  }
}


void process_link_state(unsigned char * body, int blen){
  n_vicini = 0;
  // 1 for letter, 4 fpr ip addr, 2 for udp port, 4 for cost
  for (int i = 0; i + 11 <= blen; i += 11){
    char id = body[i];
    int cost_, cost;
    memcpy(&cost_, body + i + 7, 4);
    cost = ntohl(cost_);

    if (cost == 0) {
      VN_id = id;
      continue;
    }
    vicino &node = vicini[n_vicini++];
    node.id = id;
    node.cost = cost;
    node.sa.sin_family = AF_INET;
    memcpy(&node.sa.sin_addr, body + i + 1, 4);
    memcpy(&node.sa.sin_port, body + i + 5, 2);
  }
  printf("%c = 0", VN_id);
  for (int i = 0; i < n_vicini; i++) printf(", %c = %d", vicini[i].id, vicini[i].cost);
  printf("\n");
}


int main(int argc, char *argv[]){
  if (argc != 4) {
    printf("usage : ./VN <ip_addr of ON> <port of ON> <port of VN>\n");
    exit(1);
  }

  string ON_ip = argv[1];
  int ON_port = atoi(argv[2]);
  int VN_port = atoi(argv[3]);


  memset(&ON_sa, 0, sizeof(ON_sa));
  memset(&VN_sa, 0, sizeof(VN_sa));

  ON_sa.sin_family =AF_INET;
  ON_sa.sin_port = htons(ON_port);
  inet_pton(AF_INET, argv[1], &(ON_sa.sin_addr));

  VN_sa.sin_family =AF_INET;
  VN_sa.sin_port = htons(VN_port);
  VN_sa.sin_addr = get_local_ip();


  int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  bind(udp_fd, (sockaddr *)&VN_sa, sizeof(VN_sa));
  // ! handle exit stuff

  int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
  connect(tcp_fd, (sockaddr *)&ON_sa, sizeof(ON_sa));
  send_connect_msg(tcp_fd);


  while (true) {
    int r = recv(tcp_fd, buf + buf_len, BUF_SZ - buf_len, 0);

    if (r <= 0) {
      printf("ON closed!!!!!!!!!!!!!\n");
      break;
    }

    buf_len += r;

    while (buf_len >= 3) {
      int mtype = buf[0];
      int blen = (buf[1] << 8) + buf[2];

      if (buf_len < 3 + blen) break;

      if (mtype == MSG_LINK_STATE) process_link_state(buf + 3, blen);
      memmove(buf, buf + 3 + blen, buf_len - 3 - blen);
      buf_len -= 3 + blen;
    }
  }

}
