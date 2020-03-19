#include "nlog.h"

#include "list.h"
#include "hdef.h"
#include "hbase.h"
#include "hsocket.h"

typedef struct network_logger_s {
    hloop_t*            loop;
    hio_t*              listenio;
    struct list_head    clients;
} network_logger_t;

typedef struct nlog_client {
    hio_t*              io;
    struct list_node    node;
} nlog_client;

static network_logger_t s_logger = {0};

static void on_close(hio_t* io) {
    printd("on_close fd=%d error=%d\n", hio_fd(io), hio_error(io));
    struct list_node* next = s_logger.clients.next;
    nlog_client* client;
    while (next != &s_logger.clients) {
        client = list_entry(next, nlog_client, node);
        next = next->next;
        if (client->io == io) {
            list_del(next->prev);
            SAFE_FREE(client);
            break;
        }
    }
}

static void on_read(hio_t* io, void* buf, int readbytes) {
    printd("on_read fd=%d readbytes=%d\n", hio_fd(io), readbytes);
    printd("< %s\n", buf);
    // nothing to do
}

static void on_accept(hio_t* io) {
    /*
    printd("on_accept connfd=%d\n", hio_fd(io));
    char localaddrstr[SOCKADDR_STRLEN] = {0};
    char peeraddrstr[SOCKADDR_STRLEN] = {0};
    printd("accept connfd=%d [%s] <= [%s]\n", hio_fd(io),
            SOCKADDR_STR(hio_localaddr(io), localaddrstr),
            SOCKADDR_STR(hio_peeraddr(io), peeraddrstr));
    */

    hio_setcb_read(io, on_read);
    hio_setcb_close(io, on_close);
    hio_read(io);

    // free on_close
    nlog_client* client;
    SAFE_ALLOC_SIZEOF(client);
    client->io = io;
    list_add(&client->node, &s_logger.clients);
}

void network_logger(int loglevel, const char* buf, int len) {
    struct list_node* node;
    nlog_client* client;
    list_for_each (node, &s_logger.clients) {
        client = list_entry(node, nlog_client, node);
        hio_write(client->io, buf, len);
    }
}

hio_t* nlog_listen(hloop_t* loop, int port) {
    list_init(&s_logger.clients);
    s_logger.loop = loop;
    s_logger.listenio = create_tcp_server(loop, "0.0.0.0", port, on_accept);
    return s_logger.listenio;
}
