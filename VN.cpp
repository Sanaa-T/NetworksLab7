#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <time.h>
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

const char MSG_LSP=3;
struct lsp_entry {char id; int cost;};
struct lsp_rec { int valid; int seq; int n; lsp_entry entries[26];}; 

lsp_rec databs[26];
int myseqn0=0;
int udp_fd;

const int INF=100000;
struct sockaddr_in ON_sa;
struct sockaddr_in VN_sa;


in_addr get_local_ip() {
  struct in_addr ip;
  memset(&ip, 0, sizeof(ip));

  int probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (probe_fd < 0) {
    perror("probe socket");
    exit(1);
  }

  struct sockaddr_in probe_sa;
  memset(&probe_sa, 0, sizeof(probe_sa));
  probe_sa.sin_family = AF_INET;
  probe_sa.sin_port = htons(53);
  inet_pton(AF_INET, "8.8.8.8", &(probe_sa.sin_addr));

  // udp has no connections so nothing is actually sent
  // kernel just picks which interface it would use
  if (connect(probe_fd, (sockaddr *)&probe_sa, sizeof(probe_sa)) < 0) {
    perror("cant find my own ip");
    exit(1);
  }

  struct sockaddr_in mine_sa;
  socklen_t len = sizeof(mine_sa);
  getsockname(probe_fd, (sockaddr *)&mine_sa, &len);
  close(probe_fd);
  ip = mine_sa.sin_addr;

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
void sendlsp();

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
  myseqn0++;
  sendlsp();
}

int build_lsp(unsigned char *out){
  out[0] = MSG_LSP;
  out[1]=VN_id;
  int seqn =htonl(myseqn0);
  memcpy(out + 2, &seqn, 4);
  out[6] = n_vicini;
  for (int i = 0; i < n_vicini; i++){
    out[7 + i * 5] = vicini[i].id;
    int c = htonl(vicini[i].cost);
    memcpy(out + 7 + i * 5 + 1, &c, 4);
  }
  return 7 + n_vicini * 5;
}

void flood(unsigned char *pkt, int len, sockaddr_in *skip) {
  for (int i = 0; i < n_vicini; i++) {
    if (skip != NULL &&
        vicini[i].sa.sin_addr.s_addr==skip->sin_addr.s_addr &&
        vicini[i].sa.sin_port==skip->sin_port) continue;

    for (int r = 0; r < 2; r++)
      sendto(udp_fd, pkt, len, 0,
             (sockaddr *)&vicini[i].sa, sizeof(vicini[i].sa));
  }
}

void sendlsp(){
  if (VN_id == 0) return; //since we dk our id yet ; ON hasnt sent it

  unsigned char pkt[BUF_SZ];
  int len = build_lsp(pkt);
  flood(pkt, len, NULL);

  int mine=VN_id - 'A';
  databs[mine].valid = 1;
  databs[mine].seq = myseqn0;
  databs[mine].n = n_vicini;
  for (int i = 0; i < n_vicini; i++){
    databs[mine].entries[i].id = vicini[i].id;
    databs[mine].entries[i].cost = vicini[i].cost;
  }
}

void handletheudp(){
  unsigned char pkt[BUF_SZ];
  sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  int r = recvfrom(udp_fd, pkt, BUF_SZ, 0, (sockaddr *)&from, &fromlen);
  if (r < 7) { return;
      }
  if (pkt[0] != MSG_LSP) return; // not an lsp

  char id = pkt[1];
  if (id==VN_id) return; // dont process my own lsp
  
  int seqn;
  memcpy(&seqn, pkt + 2, 4);
  seqn = ntohl(seqn);

  int n = pkt[6];
  if (n < 0 || n > 26) return; // invalid n
  if (r < 7 + n * 5) return; // invalid lsp

  int my = id - 'A';
  if (my < 0 || my >= 26) return; // invalid iddd
  if (databs[my].valid && databs[my].seq >= seqn) return;

  databs[my].valid = 1;
  databs[my].seq = seqn;
  databs[my].n = n;
  for (int i = 0; i < n; i++){
    databs[my].entries[i].id = pkt[7 + i * 5];
    int c;
    memcpy(&c, pkt + 7 + i * 5 + 1, 4);
    databs[my].entries[i].cost = ntohl(c);
    
  }
  printf("got a lsp from %c, seqn %d\n", id, seqn);
  flood(pkt, r, &from);
}

void print_db(){
  printf("DB:\n");
  for (int i = 0; i < 26; i++){
    if (!databs[i].valid) continue;
    printf("%c, seqn %d, n %d: ", 'A' + i, databs[i].seq, databs[i].n);
    for (int j = 0; j < databs[i].n; j++){
      printf("%c=%d ", databs[i].entries[j].id, databs[i].entries[j].cost);
    }
    printf("\n");
  }
}

void dijikstra(){
  if (VN_id==0) return;
  int dist[26],prev[26];
  bool visited[26];
  for (int i=0;i<26;i++){
    dist[i]=INF;
    prev[i]=-1;
    visited[i]=false;
  }
  int mine=VN_id-'A';
  dist[mine]=0;

  for (int i=0;i<26;i++){
    int u=-1;
    for (int j=0;j<26;j++){
      if (!databs[j].valid||visited[j]) continue;
      if (u==-1||dist[j]<dist[u]) u=j;
    }
    if (u==-1||dist[u]==INF) break;
    visited[u]=true;

    for (int j=0;j<databs[u].n;j++){
      int v=databs[u].entries[j].id-'A';
      if (v<0||v>=26) continue;
      int v_cost=databs[u].entries[j].cost;
      if (dist[u]+v_cost<dist[v]){
        dist[v]=dist[u]+v_cost;
        prev[v]=u;
      }
    }
  }

  printf("OUR Routing tABle IS:\n");
  for (int i=0;i<26;i++){
    if (!databs[i].valid||i==mine||dist[i]==INF) continue;
    int next_hop=i;
    while (prev[next_hop]!=mine&&prev[next_hop]!=-1) next_hop=prev[next_hop];
    printf("%c: cost %d, next hop %c\n",'A'+i,dist[i],'A'+next_hop);
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
  if(inet_pton(AF_INET, argv[1], &(ON_sa.sin_addr)) !=1) {
    printf("invalid ip addresss %s\n", argv[1]);
    exit(1);
  }
 

  VN_sa.sin_family =AF_INET;
  VN_sa.sin_port = htons(VN_port);
  VN_sa.sin_addr = get_local_ip();


  udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_fd < 0) {
    perror("udp socket");
    exit(1);
  }
  if (bind(udp_fd, (sockaddr *)&VN_sa, sizeof(VN_sa)) < 0) {
    perror("unabke to bind udp port");
    exit(1);
  }
  // ! handle exit stuff

  int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (tcp_fd < 0) {
    perror("tcp socket");
    close(udp_fd);
    exit(1);
  }

  if (connect(tcp_fd, (sockaddr *)&ON_sa, sizeof(ON_sa)) < 0) {
    perror("unable to reach ON");
    close(udp_fd);
    close(tcp_fd);
    exit(1);
  }
  
  send_connect_msg(tcp_fd);

  time_t last_lsp_time = time(NULL);
  while (true) {
    fd_set rfds;;
    FD_ZERO(&rfds);
    FD_SET(tcp_fd, &rfds);
    FD_SET(udp_fd, &rfds);
    int maxfd = max(tcp_fd, udp_fd) + 1;

    timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    int rv = select(maxfd, &rfds, NULL, NULL, &tv);
    if (rv < 0) {
      perror("select");
      exit(1);
    }

    if (rv>0 ) {
      if (FD_ISSET(udp_fd, &rfds)) {
        handletheudp();
      }
      if (FD_ISSET(tcp_fd, &rfds)) {
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
    
    time_t rightnow=time(NULL);
    
    if (rightnow-last_lsp_time >= 10) {
      last_lsp_time = rightnow;
      myseqn0++;
      sendlsp();
      dijikstra();
      print_db();
      
    }

  }

}
