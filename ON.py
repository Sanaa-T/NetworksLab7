import select # thank u, beej
import socket
import sys
import os


TCP_PORT=5000
MSG_CONNECT=1
MSG_LINK_STATE=2

HDR_SZ = 3 # 1 byte for mtype, 2 for blen
CONN_SZ = 6 # 4 bytes for ip addr, 2 for udp port
TUPLE_SZ = 11 # 1 for letter, 4 fpr ip addr, 2 for udp port, 4 for cost

def u8(n):
    return n.to_bytes(1, 'big')
def u16(n):
    return n.to_bytes(2, 'big')
def i32(n):
    return n.to_bytes(4, 'big', signed = True)

def pack_hdr(mtype, body_len):
    return u8(mtype) + u16(body_len)

def unpack_hdr(buf):
    mtype = buf[0]
    body_len = int.from_bytes(buf[1:3], 'big')
    return mtype, body_len

def unpack_conn(body):
    ip = socket.inet_ntoa(body[0:4]) # a.a.a.a
    port = int.from_bytes(body[4:6], 'big')
    return ip, port

def pack_tuple(letter, ip_addr, port, cost):
  return u8(ord(letter)) + socket.inet_aton(ip_addr) + u16(port) + i32(cost)

def parse_config(path):
    rows = []

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            rows.append([int(tok) for tok in line.split()])

    n = len(rows) + 1

    cost = [[-1] * n for _ in range(n)]

    for i, row in enumerate(rows):
        for k, c in enumerate(row):
            j = i + 1 + k
            cost[i][j] = c
            cost[j][i] = c

    return n, cost

def print_matrix(n, cost):
    line = "    "
    for j in range(n):
        line = line + "  " + chr(65 + j) + " "
    print(line)

    for i in range(n):
        line = "  " + chr(65 + i) + " "
        for j in range(n):
            line = line + "%3d " % cost[i][j]
        print(line)

def ready(peers, n):
  if len(peers) < n:
    return False
  for i in range(n):
    if peers[i]['ip'] is None or peers[i]['sock'] is None:
      return False
  return True

def send_link_state(peers, cost, n, i):
  p = peers[i]
  body = pack_tuple(chr(65+i), p['ip'], p['udp'], 0)

  for j in range(n):
    if cost[i][j] > 0 :
      vicino = peers[j]
      body += pack_tuple(chr(65+j), vicino['ip'], vicino['udp'], cost[i][j])
  p['sock'].sendall(pack_hdr(MSG_LINK_STATE, len(body)) + body)
  print(f'ON sent link_state to {chr(65+i)} [{len(body) / TUPLE_SZ} tuples]')

def broadcast_link_state(peers, cost, n):
  for i in range(n):
    send_link_state(peers, cost, n, i)
def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 53))
        return s.getsockname()[0]
    finally:
        s.close()
def main():
    if len(sys.argv) != 2 :
        print("wrong format!")
        sys.exit()
   # bind_ip = '8.8.8.8' # need to decide
    n, cost = parse_config(sys.argv[1])
    cfgtime=os.path.getmtime(sys.argv[1])

    print('ON %d nodes (A..%s)' % (n, chr(64 + n)))
    print_matrix(n, cost)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
# HOST = socket.gethostname()
 #   for res in socket.getaddrinfo(HOST, TCP_PORT, socket.AF_INET, socket.SOCK_STREAM, 0, socket.AI_PASSIVE):
   #   _, _, _, _, sa = res
  #    bind_ip = sa[0]
    ##  if (bind_ip[0:3] == '127'):
      #  continue
      #try :
      #  listener.bind(sa)
 #       listener.listen(27)
  ##     listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
     #   continue
 #     break


    bind_ip = local_ip()
    listener.bind((bind_ip, TCP_PORT))
    listener.listen(27)


    print('ON is listening on %s:%d' % (bind_ip, TCP_PORT))

    peers = []

    while True:
        socks = [listener] + [p['sock'] for p in peers if p['sock']]
        readable, _, _ = select.select(socks, [], [], 1.0)

        for s in readable:
            if s is listener:
                conn, addr = listener.accept()  # conn is the new socket for this vn
                # addr is (hostaddr, port)
                idx = len(peers)
                
                if idx < n:
                    peers.append({'sock': conn,
                                    'buf': b'',
                                                  'ip': None,
                                                  'udp': None,
                                                  'idx': idx})
                    print('ON node %s connected from %s:%d'
                          % (chr(65 + idx), addr[0], addr[1]))
                else:
                    print('ON has extra connection from %s:%d -> island, and wil close'
                          % (addr[0], addr[1]))
                    conn.close()
                    
                continue

            p = None
            for q in peers:
                if q['sock'] is s:
                    p = q
                    break

            # [0,9,1,2,4,5,6]

            chunk = s.recv(4096)
            if not chunk:
              print("what to do chunk is 0? means socket closed!")
              print('node %s disconnected' % chr(65 + p['idx']))
              s.close()
              p['sock'] = None
              continue
            p['buf'] += chunk
            while len(p['buf']) >= HDR_SZ:
                mtype, blen = unpack_hdr(p['buf'])
                if len(p['buf']) < HDR_SZ + blen:
                    break
                body = p['buf'][HDR_SZ:HDR_SZ + blen]
                p['buf'] = p['buf'][HDR_SZ + blen:]

                if mtype == MSG_CONNECT and blen == CONN_SZ:
                    ip, udp_port = unpack_conn(body)
                    p['ip'] = ip
                    p['udp'] = udp_port
                    print('ON CONNECT from %s : (%s, %d)'
                          % (chr(65 + p['idx']), p['ip'], p['udp']))
                    # need to broadcast
                    if ready(peers, n):
                      broadcast_link_state(peers, cost, n)
        m=os.path.getmtime(sys.argv[1])
        if m!=cfgtime:
            print("config file changed, reloading...")
            n, cost = parse_config(sys.argv[1])
            cfgtime=m
            print('ON %d nodes (A..%s)' % (n, chr(64 + n)))
            print_matrix(n, cost)
            if ready(peers, n):
              broadcast_link_state(peers, cost, n)

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nbye bye, boy bye, it\'s over')
