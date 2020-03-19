#include "hevent.h"
#include "hsocket.h"

int hio_fd(hio_t* io) {
    return io->fd;
}

hio_type_e hio_type(hio_t* io) {
    return io->io_type;
}

int hio_error(hio_t* io) {
    return io->error;
}

struct sockaddr* hio_localaddr(hio_t* io) {
    return io->localaddr;
}

struct sockaddr* hio_peeraddr(hio_t* io) {
    return io->peeraddr;
}

void hio_set_readbuf(hio_t* io, void* buf, size_t len) {
    if (buf == NULL || len == 0) {
        hloop_t* loop = io->loop;
        if (loop && (loop->readbuf.base == NULL || loop->readbuf.len == 0)) {
            loop->readbuf.len = HLOOP_READ_BUFSIZE;
            loop->readbuf.base = (char*)malloc(loop->readbuf.len);
            io->readbuf = loop->readbuf;
        }
    }
    else {
        io->readbuf.base = (char*)buf;
        io->readbuf.len = len;
    }
}

int hio_enable_ssl(hio_t* io) {
    printd("ssl fd=%d\n", io->fd);
    io->io_type = HIO_TYPE_SSL;
    return 0;
}

void hio_setcb_accept   (hio_t* io, haccept_cb  accept_cb) {
    io->accept_cb = accept_cb;
}

void hio_setcb_connect  (hio_t* io, hconnect_cb connect_cb) {
    io->connect_cb = connect_cb;
}

void hio_setcb_read     (hio_t* io, hread_cb    read_cb) {
    io->read_cb = read_cb;
}

void hio_setcb_write    (hio_t* io, hwrite_cb   write_cb) {
    io->write_cb = write_cb;
}

void hio_setcb_close    (hio_t* io, hclose_cb   close_cb) {
    io->close_cb = close_cb;
}

void hio_set_type(hio_t* io, hio_type_e type) {
    io->io_type = type;
}

void hio_set_localaddr(hio_t* io, struct sockaddr* addr, int addrlen) {
    if (io->localaddr == NULL) {
        SAFE_ALLOC(io->localaddr, sizeof(sockaddr_un));
    }
    memcpy(io->localaddr, addr, addrlen);
}

void hio_set_peeraddr (hio_t* io, struct sockaddr* addr, int addrlen) {
    if (io->peeraddr == NULL) {
        SAFE_ALLOC(io->peeraddr, sizeof(sockaddr_un));
    }
    memcpy(io->peeraddr, addr, addrlen);
}
