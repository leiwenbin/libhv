#include "hthread.h"
#include "hmutex.h"

#include <stdio.h>
#include <time.h>

void once_print() {
    printf("exec once\n");
}

HTHREAD_ROUTINE(test_once) {
    honce_t once = HONCE_INIT;
    for (int i = 0; i < 10; ++i) {
        honce(&once, once_print);
    }
    printf("honce test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_mutex) {
    hmutex_t mutex;
    hmutex_init(&mutex);
    hmutex_lock(&mutex);
    hmutex_unlock(&mutex);
    hmutex_destroy(&mutex);
    printf("hmutex test OK!\n");
    return 0;
}

#if HAVE_PTHREAD_SPIN_LOCK
HTHREAD_ROUTINE(test_spinlock) {
    hspinlock_t spin;
    hspinlock_init(&spin);
    hspinlock_lock(&spin);
    hspinlock_unlock(&spin);
    hspinlock_destroy(&spin);
    printf("hspinlock test OK!\n");
    return 0;
}
#endif

HTHREAD_ROUTINE(test_rwlock) {
    hrwlock_t rwlock;
    hrwlock_init(&rwlock);
    hrwlock_rdlock(&rwlock);
    hrwlock_rdunlock(&rwlock);
    hrwlock_wrlock(&rwlock);
    hrwlock_wrunlock(&rwlock);
    hrwlock_destroy(&rwlock);
    printf("hrwlock test OK!\n");
    return 0;
}

#if HAVE_PTHREAD_MUTEX_TIMEDLOCK
HTHREAD_ROUTINE(test_timed_mutex) {
    htimed_mutex_t mutex;
    htimed_mutex_init(&mutex);
    htimed_mutex_lock(&mutex);
    time_t start_time = time(NULL);
    htimed_mutex_lock_for(&mutex, 3000);
    time_t end_time = time(NULL);
    htimed_mutex_unlock(&mutex);
    htimed_mutex_destroy(&mutex);
    printf("htimed_mutex_lock_for %zds\n", end_time - start_time);
    printf("htimed_mutex test OK!\n");
    return 0;
}
#endif

HTHREAD_ROUTINE(test_condvar) {
    hmutex_t mutex;
    hmutex_init(&mutex);
    hcondvar_t cv;
    hcondvar_init(&cv);

    hmutex_lock(&mutex);
    hcondvar_signal(&cv);
    hcondvar_broadcast(&cv);
    time_t start_time = time(NULL);
    hcondvar_wait_for(&cv, &mutex, 3000);
    time_t end_time = time(NULL);
    printf("hcondvar_wait_for %zds\n", end_time - start_time);
    hmutex_unlock(&mutex);

    hmutex_destroy(&mutex);
    hcondvar_destroy(&cv);
    printf("hcondvar test OK!\n");
    return 0;
}

int main(int argc, char* argv[]) {
    hthread_t thread_once = hthread_create(test_once, NULL);
    hthread_t thread_mutex = hthread_create(test_mutex, NULL);
#if HAVE_PTHREAD_SPIN_LOCK
    hthread_t thread_spinlock = hthread_create(test_spinlock, NULL);
#endif
    hthread_t thread_rwlock = hthread_create(test_rwlock, NULL);
#if HAVE_PTHREAD_MUTEX_TIMEDLOCK
    hthread_t thread_timed_mutex = hthread_create(test_timed_mutex, NULL);
#endif
    hthread_t thread_condvar = hthread_create(test_condvar, NULL);

    hthread_join(thread_once);
    hthread_join(thread_mutex);
#if HAVE_PTHREAD_SPIN_LOCK
    hthread_join(thread_spinlock);
#endif
    hthread_join(thread_rwlock);
#if HAVE_PTHREAD_MUTEX_TIMEDLOCK
    hthread_join(thread_timed_mutex);
#endif
    hthread_join(thread_condvar);
    printf("hthread test OK!\n");
    return 0;
}
