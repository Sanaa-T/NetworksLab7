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


const char MSG_CONNECT = 1;


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
int sendall(int s, char *buf, int *len)
{
int total = 0; // how many bytes we've sent
int bytesleft= *len; // how many we have left to send
int n;
while(total < *len) {
n = send(s, buf+total, bytesleft, 0);
if (n == -1) { break; }
total += n;
bytesleft-= n;
}
*len = total; // return number actually sent here
return n==-1?-1:0; // return -1 on failure, 0 on success
}


void send_connect_msg(int tcp_fd){
  // msg is gonna be (ip, port) so 4 + 2 bytes
  int len = 9;
  char msg[len];
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

}
