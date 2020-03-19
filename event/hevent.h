#ifndef HV_EVENT_H_
#define HV_EVENT_H_

#include "array.h"
#include "list.h"
#include "heap.h"
#include "queue.h"

#include "hloop.h"
#include "hbuf.h"
#include "hmutex.h"

#define HLOOP_READ_BUFSIZE  8192

typedef enum {
    HLOOP_STATUS_STOP,
    HLOOP_STATUS_RUNNING,
    HLOOP_STATUS_PAUSE
} hloop_status_e;

ARRAY_DECL(hio_t*, io_array);
QUEUE_DECL(hevent_t, event_queue);

struct hloop_s {
    uint32_t    flags;
    hloop_status_e status;
    uint64_t    start_ms;       // ms
    uint64_t    start_hrtime;   // us
    uint64_t    end_hrtime;
    uint64_t    cur_hrtime;
    uint64_t    loop_cnt;
    void*       userdata;
//private:
    // events
    uint64_t                    event_counter;
    uint32_t                    nactives;
    uint32_t                    npendings;
    // pendings: with priority as array.index
    hevent_t*                   pendings[HEVENT_PRIORITY_SIZE];
    // idles
    struct list_head            idles;
    uint32_t                    nidles;
    // timers
    struct heap                 timers;
    uint32_t                    ntimers;
    // ios: with fd as array.index
    struct io_array             ios;
    uint32_t                    nios;
    // one loop per thread, so one readbuf per loop is OK.
    hbuf_t                      readbuf;
    void*                       iowatcher;
    // custom_events
    int                         sockpair[2];
    event_queue                 custom_events;
    hmutex_t                    custom_events_mutex;
};

struct hidle_s {
    HEVENT_FIELDS
    uint32_t    repeat;
//private:
    struct list_node node;
};

#define HTIMER_FIELDS                   \
    HEVENT_FIELDS                       \
    uint32_t    repeat;                 \
    uint64_t    next_timeout;           \
    struct heap_node node;

struct htimer_s {
    HTIMER_FIELDS
};

struct htimeout_s {
    HTIMER_FIELDS
    uint32_t    timeout;                \
};

struct hperiod_s {
    HTIMER_FIELDS
    int8_t      minute;
    int8_t      hour;
    int8_t      day;
    int8_t      week;
    int8_t      month;
};

QUEUE_DECL(offset_buf_t, write_queue);
struct hio_s {
    HEVENT_FIELDS
    unsigned    ready       :1;
    unsigned    closed      :1;
    unsigned    accept      :1;
    unsigned    connect     :1;
    unsigned    connectex   :1; // for ConnectEx/DisconnectEx
    unsigned    recv        :1;
    unsigned    send        :1;
    unsigned    recvfrom    :1;
    unsigned    sendto      :1;
    int         fd;
    hio_type_e  io_type;
    int         error;
    int         events;
    int         revents;
    struct sockaddr*    localaddr;
    struct sockaddr*    peeraddr;
    hbuf_t              readbuf;        // for hread
    struct write_queue  write_queue;    // for hwrite
    // callbacks
    hread_cb    read_cb;
    hwrite_cb   write_cb;
    hclose_cb   close_cb;
    haccept_cb  accept_cb;
    hconnect_cb connect_cb;
//private:
    int         event_index[2]; // for poll,kqueue
    void*       hovlp;          // for iocp/overlapio
    void*       ssl;            // for SSL
    htimer_t*   timer;          // for io timeout
};

#define EVENT_ENTRY(p)          container_of(p, hevent_t, pending_node)
#define IDLE_ENTRY(p)           container_of(p, hidle_t,  node)
#define TIMER_ENTRY(p)          container_of(p, htimer_t, node)

#define EVENT_ACTIVE(ev) \
    if (!ev->active) {\
        ev->active = 1;\
        ev->loop->nactives++;\
    }\

#define EVENT_INACTIVE(ev) \
    if (ev->active) {\
        ev->active = 0;\
        ev->loop->nactives--;\
    }\

#define EVENT_PENDING(ev) \
    do {\
        if (!ev->pending) {\
            ev->pending = 1;\
            ev->loop->npendings++;\
            hevent_t** phead = &ev->loop->pendings[HEVENT_PRIORITY_INDEX(ev->priority)];\
            ev->pending_next = *phead;\
            *phead = (hevent_t*)ev;\
        }\
    } while(0)

#define EVENT_ADD(loop, ev, cb) \
    do {\
        ev->loop = loop;\
        ev->event_id = ++loop->event_counter;\
        ev->cb = (hevent_cb)cb;\
        EVENT_ACTIVE(ev);\
    } while(0)

#define EVENT_DEL(ev) \
    do {\
        EVENT_INACTIVE(ev);\
        if (!ev->pending) {\
            SAFE_FREE(ev);\
        }\
    } while(0)

#define EVENT_RESET(ev) \
    do {\
        ev->destroy = 0;\
        ev->active  = 1;\
        ev->pending = 0;\
    } while(0)

#endif // HV_EVENT_H_
