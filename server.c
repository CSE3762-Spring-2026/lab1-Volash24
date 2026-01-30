
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

// key: no spaces, ends at ':'
static int parse_key(const char **p, char *key, size_t key_sz) {
    skip_ws(p);
    if (**p == '\0') return 0; // done

    const char *start = *p;
    while (**p && **p != ':' && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r')
        (*p)++;

    if (**p != ':') return -1; // invalid (must have ':')
    size_t len = (size_t)(*p - start);
    if (len == 0 || len >= key_sz) return -1;

    memcpy(key, start, len);
    key[len] = '\0';
    (*p)++; // skip ':'
    return 1;
}

// value: either "quoted string" (may contain spaces) OR unquoted token (no spaces)
static int parse_value(const char **p, char *val, size_t val_sz) {
    skip_ws(p);
    if (**p == '\0') return -1;

    if (**p == '"') {
        (*p)++; // skip opening quote
        const char *start = *p;
        while (**p && **p != '"') (*p)++;
        if (**p != '"') return -1; // missing closing quote

        size_t len = (size_t)(*p - start);
        if (len >= val_sz) len = val_sz - 1;
        memcpy(val, start, len);
        val[len] = '\0';
        (*p)++; // skip closing quote
        return 1;
    } else {
        const char *start = *p;
        while (**p && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r')
            (*p)++;

        size_t len = (size_t)(*p - start);
        if (len == 0) return -1;

        if (len >= val_sz) len = val_sz - 1;
        memcpy(val, start, len);
        val[len] = '\0';
        return 1;
    }
}

static void print_pair(const char *key, const char *val) {
    // Exactly what spec wants: 20-char left aligned fields
    printf("%-20.20s %-20.20s\n", key, val);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <multicast_ip> <port>\n", argv[0]);
        return 1;
    }

    const char *mcast_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port.\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) die("socket");

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        die("setsockopt SO_REUSEADDR");

#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)); // OK if fails
#endif

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((uint16_t)port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0)
        die("bind");

    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    if (inet_pton(AF_INET, mcast_ip, &mreq.imr_multiaddr) != 1) {
        fprintf(stderr, "Invalid multicast IP.\n");
        return 1;
    }
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        die("setsockopt IP_ADD_MEMBERSHIP");

    printf("Joined multicast group %s:%d\n", mcast_ip, port);
    fflush(stdout);

    for (;;) {
        char buf[4096];
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
        if (n < 0) {
            if (errno == EINTR) continue;
            die("recvfrom");
        }
        buf[n] = '\0';

        const char *p = buf;
        while (1) {
            char key[256], val[2048];
            int k = parse_key(&p, key, sizeof(key));
            if (k == 0) break; // end of message
            if (k < 0) { fprintf(stderr, "Invalid key format.\n"); break; }

            if (parse_value(&p, val, sizeof(val)) < 0) {
                fprintf(stderr, "Invalid value format for key '%s'.\n", key);
                break;
            }

            print_pair(key, val);
        }
        fflush(stdout);
    }

    close(sock);
    return 0;
}
