#include "net/udp.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#define INVALID_SOCKET (-1)
#endif

#ifdef _WIN32
static bool winsock_ready = false;
static bool start_winsock(void)
{
    if (winsock_ready) return true;
    WSADATA data;
    winsock_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return winsock_ready;
}
#else
static bool start_winsock(void) { return true; }
#endif

bool udp_open(UdpSocket *s, uint16_t port)
{
    memset(s, 0, sizeof *s);
    if (!start_winsock()) return false;
#ifdef _WIN32
    SOCKET h = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
    int h = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (h == INVALID_SOCKET) return false;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(h, (struct sockaddr *)&addr, sizeof addr) != 0) {
        CLOSE_SOCKET(h);
        return false;
    }
#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(h, FIONBIO, &nonblocking);
#else
    fcntl(h, F_SETFL, fcntl(h, F_GETFL, 0) | O_NONBLOCK);
#endif
    s->open = true;
    s->handle = (intptr_t)h;
    return true;
}

void udp_close(UdpSocket *s)
{
    if (s->open) CLOSE_SOCKET((s->handle));
    s->open = false;
}

bool udp_send(UdpSocket *s, NetAddress to, const void *data, size_t size)
{
    if (!s->open) return false;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = to.ip;
    addr.sin_port = to.port;
    int sent = (int)sendto(s->handle, (const char *)data, (int)size, 0, (struct sockaddr *)&addr, sizeof addr);
    return sent == (int)size;
}

size_t udp_recv(UdpSocket *s, NetAddress *from, void *data, size_t capacity)
{
    if (!s->open) return 0;
    struct sockaddr_in addr;
    socklen_t length = sizeof addr;
    int got = (int)recvfrom(s->handle, (char *)data, (int)capacity, 0, (struct sockaddr *)&addr, &length);
    if (got <= 0) return 0;   // nada chegou (ou erro passageiro, como o ICMP de porta fechada no Windows)
    from->ip = addr.sin_addr.s_addr;
    from->port = addr.sin_port;
    return (size_t)got;
}

bool udp_parse(const char *text, NetAddress *out)
{
    unsigned a, b, c, d, port = NET_DEFAULT_PORT;
    char extra;
    int n = sscanf(text, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &port, &extra);
    if (n != 4 && n != 5) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255 || port == 0 || port > 65535) return false;
    uint32_t host = (a << 24) | (b << 16) | (c << 8) | d;
    out->ip = htonl(host);
    out->port = htons((uint16_t)port);
    return true;
}

const char *udp_format(NetAddress a)
{
    static char out[32];
    uint32_t host = ntohl(a.ip);
    snprintf(out, sizeof out, "%u.%u.%u.%u:%u", (host >> 24) & 255u, (host >> 16) & 255u, (host >> 8) & 255u, host & 255u,
             (unsigned)ntohs(a.port));
    return out;
}

bool udp_same(NetAddress a, NetAddress b)
{
    return a.ip == b.ip && a.port == b.port;
}

int udp_local_ips(char out[][24], int max)
{
    int n = 0;
    if (!start_winsock()) return 0;
#ifdef _WIN32
    char name[256];
    if (gethostname(name, sizeof name) != 0) return 0;
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    if (getaddrinfo(name, NULL, &hints, &list) != 0) return 0;
    for (struct addrinfo *it = list; it != NULL && n < max; it = it->ai_next) {
        uint32_t host = ntohl(((struct sockaddr_in *)it->ai_addr)->sin_addr.s_addr);
        if ((host >> 24) == 127u) continue;
        snprintf(out[n++], 24, "%u.%u.%u.%u", (host >> 24) & 255u, (host >> 16) & 255u, (host >> 8) & 255u, host & 255u);
    }
    freeaddrinfo(list);
#else
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) return 0;
    for (struct ifaddrs *it = list; it != NULL && n < max; it = it->ifa_next) {
        if (it->ifa_addr == NULL || it->ifa_addr->sa_family != AF_INET) continue;
        uint32_t host = ntohl(((struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr);
        if ((host >> 24) == 127u) continue;
        snprintf(out[n++], 24, "%u.%u.%u.%u", (host >> 24) & 255u, (host >> 16) & 255u, (host >> 8) & 255u, host & 255u);
    }
    freeifaddrs(list);
#endif
    return n;
}
