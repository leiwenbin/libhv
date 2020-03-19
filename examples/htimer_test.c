#include "hloop.h"
#include "hbase.h"

void on_timer(htimer_t* timer) {
    printf("time=%lus on_timer\n", hloop_now(hevent_loop(timer)));
}

void on_timer_add(htimer_t* timer) {
    printf("time=%lus on_timer_add\n", hloop_now(hevent_loop(timer)));
    htimer_add(hevent_loop(timer), on_timer_add, 1000, 1);
}

void on_timer_del(htimer_t* timer) {
    printf("time=%lus on_timer_del\n", hloop_now(hevent_loop(timer)));
    htimer_del(timer);
}

void on_timer_reset(htimer_t* timer) {
    printf("time=%lus on_timer_reset\n", hloop_now(hevent_loop(timer)));
    htimer_reset((htimer_t*)hevent_userdata(timer));
}

void on_timer_quit(htimer_t* timer) {
    printf("time=%lus on_timer_quit\n", hloop_now(hevent_loop(timer)));
    hloop_stop(hevent_loop(timer));
}

void cron_hourly(htimer_t* timer) {
    time_t tt;
    time(&tt);
    printf("time=%lus cron_hourly: %s\n", hloop_now(hevent_loop(timer)), ctime(&tt));
}

int main() {
    MEMCHECK;
    hloop_t* loop = hloop_new(0);

    // on_timer_add triggered forever
    htimer_add(loop, on_timer_add, 1000, 1);
    // on_timer_del triggered just once
    htimer_add(loop, on_timer_del, 1000, 10);

    // on_timer triggered after 10s
    htimer_t* reseted = htimer_add(loop, on_timer, 5000, 1);
    htimer_t* reset = htimer_add(loop, on_timer_reset, 1000, 5);
    hevent_set_userdata(reset, reseted);

    // cron_hourly next triggered in one minute
    int minute = time(NULL)%3600/60;
    htimer_add_period(loop, cron_hourly, minute+1, -1, -1, -1, -1, INFINITE);

    // quit application after 1 min
    htimer_add(loop, on_timer_quit, 60000, 1);

    printf("time=%lus begin\n", hloop_now(loop));
    hloop_run(loop);
    printf("time=%lus stop\n", hloop_now(loop));
    hloop_free(&loop);
    return 0;
}
