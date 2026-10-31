#include "red_sock.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define SOCK_BAD INVALID_SOCKET
  static void cerrar(sock_t s) { closesocket(s); }
  static bool quedaBloqueo() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINTR; }
  static void noBloq(sock_t s) { u_long m = 1; ioctlsocket(s, FIONBIO, &m); }
  static void iniciarWS() { static bool ok = false; if (!ok) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); ok = true; } }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <signal.h>
  typedef int sock_t;
  #define SOCK_BAD (-1)
  static void cerrar(sock_t s) { close(s); }
  static bool quedaBloqueo() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
  static void noBloq(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
  static void iniciarWS() { static bool ok = false; if (!ok) { signal(SIGPIPE, SIG_IGN); ok = true; } }
#endif

static void sinDemora(sock_t s) { int on = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof on); }

int rs_listen(int puerto) {
    iniciarWS();
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_BAD) return -1;
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof on);
    sockaddr_in a; std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)puerto);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 8) != 0) { cerrar(s); return -1; }
    noBloq(s);
    return (int)s;
}
int rs_accept(int srv) {
    sock_t c = accept((sock_t)srv, nullptr, nullptr);
    if (c == SOCK_BAD) return -1;
    noBloq(c); sinDemora(c);
    return (int)c;
}
int rs_connect(const char* host, int puerto, int ms) {
    iniciarWS();
    addrinfo h; std::memset(&h, 0, sizeof h); h.ai_family = AF_INET; h.ai_socktype = SOCK_STREAM;
    addrinfo* r = nullptr; char ps[16]; std::snprintf(ps, sizeof ps, "%d", puerto);
    if (getaddrinfo(host, ps, &h, &r) != 0 || !r) return -1;
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_BAD) { freeaddrinfo(r); return -1; }
    noBloq(s);
    int rc = connect(s, r->ai_addr, (int)r->ai_addrlen);
    freeaddrinfo(r);
    if (rc != 0 && !quedaBloqueo()) {
#ifndef _WIN32
        if (errno != EINPROGRESS) { cerrar(s); return -1; }
#else
        cerrar(s); return -1;
#endif
    }
    if (rc != 0) {
        fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e);
        timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
        if (select((int)s + 1, nullptr, &w, &e, &tv) <= 0 || FD_ISSET(s, &e)) { cerrar(s); return -1; }
        int err = 0; socklen_t l = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &l);
        if (err != 0) { cerrar(s); return -1; }
    }
    sinDemora(s);
    return (int)s;
}
int rs_send(int s, const void* d, int n) {
    int r = (int)send((sock_t)s, (const char*)d, n, 0);
    if (r >= 0) return r;
    return quedaBloqueo() ? 0 : -1;
}
int rs_recv(int s, void* d, int n) {
    int r = (int)recv((sock_t)s, (char*)d, n, 0);
    if (r > 0) return r;
    if (r == 0) return -1;
    return quedaBloqueo() ? 0 : -1;
}
int rs_esperar(int s, int ms) {
    fd_set r; FD_ZERO(&r); FD_SET((sock_t)s, &r);
    timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    return select(s + 1, &r, nullptr, nullptr, &tv) > 0 ? 1 : 0;
}
void rs_close(int s) { cerrar((sock_t)s); }
const char* rs_ip_local() {
    static char buf[64];
    std::snprintf(buf, sizeof buf, "?");
    iniciarWS();
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == SOCK_BAD) return buf;
    sockaddr_in a; std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(53); inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);   // no envia nada: solo elige la interfaz
    if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
        sockaddr_in b; socklen_t l = sizeof b;
        if (getsockname(s, (sockaddr*)&b, &l) == 0) inet_ntop(AF_INET, &b.sin_addr, buf, sizeof buf);
    }
    cerrar(s);
    return buf;
}

int rs_udp_abrir(int puerto) {
    iniciarWS();
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == SOCK_BAD) return -1;
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof on);
    sockaddr_in a; std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)puerto);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) { cerrar(s); return -1; }
    noBloq(s);
    return (int)s;
}
int rs_udp_recibir(int s, char* d, int n, char* ip, int ipn) {
    sockaddr_in a; socklen_t l = sizeof a;
    int r = (int)recvfrom((sock_t)s, d, n, 0, (sockaddr*)&a, &l);
    if (r <= 0) return 0;
    if (ip && ipn > 0) inet_ntop(AF_INET, &a.sin_addr, ip, ipn);
    return r;
}
int rs_udp_difundir(int puerto, const char* d, int n) {
    iniciarWS();
    static sock_t s = SOCK_BAD;
    if (s == SOCK_BAD) {
        s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s == SOCK_BAD) return -1;
        int on = 1;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof on);
    }
    sockaddr_in a; std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((unsigned short)puerto); a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    int r = (int)sendto(s, d, n, 0, (sockaddr*)&a, sizeof a);
    // tambien a la subred /24 de esta PC (algunos routers no reenvian el 255.255.255.255)
    char ipl[64]; std::snprintf(ipl, sizeof ipl, "%s", rs_ip_local());
    unsigned b1, b2, b3, b4;
    if (std::sscanf(ipl, "%u.%u.%u.%u", &b1, &b2, &b3, &b4) == 4) {
        char dir[64]; std::snprintf(dir, sizeof dir, "%u.%u.%u.255", b1, b2, b3);
        inet_pton(AF_INET, dir, &a.sin_addr);
        sendto(s, d, n, 0, (sockaddr*)&a, sizeof a);
    }
    return r;
}
