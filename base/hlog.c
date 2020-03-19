#include "hlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

//#include "hmutex.h"
#ifdef _WIN32
#include <windows.h>
#define hmutex_t            CRITICAL_SECTION
#define hmutex_init         InitializeCriticalSection
#define hmutex_destroy      DeleteCriticalSection
#define hmutex_lock         EnterCriticalSection
#define hmutex_unlock       LeaveCriticalSection
#else
#include <sys/time.h>       // for gettimeofday
#include <pthread.h>
#define hmutex_t            pthread_mutex_t
#define hmutex_init(mutex)  pthread_mutex_init(mutex, NULL)
#define hmutex_destroy      pthread_mutex_destroy
#define hmutex_lock         pthread_mutex_lock
#define hmutex_unlock       pthread_mutex_unlock
#endif

//#include "htime.h"
#define SECONDS_PER_HOUR    3600
#define SECONDS_PER_DAY     86400   // 24*3600
#define SECONDS_PER_WEEK    604800  // 7*24*3600;

static int s_gmtoff = 28800; // 8*3600

struct logger_s {
    logger_handler  handler;
    unsigned int    bufsize;
    char*           buf;

    int             level;
    int             enable_color;

    // for file logger
    char                filepath[256];
    unsigned long long  max_filesize;
    int                 remain_days;
    int                 enable_fsync;
    FILE*               fp_;
    char                cur_logfile[256];
    time_t              last_logfile_ts;

    hmutex_t            mutex_; // thread-safe
};

static void logger_init(logger_t* logger) {
    logger->handler = NULL;
    logger->bufsize = DEFAULT_LOG_MAX_BUFSIZE;
    logger->buf = (char*)malloc(logger->bufsize);

    logger->level = DEFAULT_LOG_LEVEL;
    logger->enable_color = 0;

    logger->fp_ = NULL;
    logger->max_filesize = DEFAULT_LOG_MAX_FILESIZE;
    logger->remain_days = DEFAULT_LOG_REMAIN_DAYS;
    logger->enable_fsync = 1;
    logger_set_file(logger, DEFAULT_LOG_FILE);
    logger->last_logfile_ts = 0;
    hmutex_init(&logger->mutex_);
}

logger_t* logger_create() {
    // init gmtoff here
    time_t ts = time(NULL);
    struct tm* local_tm = localtime(&ts);
    int local_hour = local_tm->tm_hour;
    struct tm* gmt_tm = gmtime(&ts);
    int gmt_hour = gmt_tm->tm_hour;
    s_gmtoff = (local_hour - gmt_hour) * SECONDS_PER_HOUR;

    logger_t* logger = (logger_t*)malloc(sizeof(logger_t));
    logger_init(logger);
    return logger;
}

void logger_destroy(logger_t* logger) {
    if (logger) {
        if (logger->buf) {
            free(logger->buf);
        }
        hmutex_destroy(&logger->mutex_);
        free(logger);
    }
}

void logger_set_handler(logger_t* logger, logger_handler fn) {
    logger->handler = fn;
}

void logger_set_level(logger_t* logger, int level) {
    logger->level = level;
}

void logger_set_remain_days(logger_t* logger, int days) {
    logger->remain_days = days;
}

void logger_set_max_bufsize(logger_t* logger, unsigned int bufsize) {
    logger->bufsize = bufsize;
    logger->buf = (char*)realloc(logger->buf, bufsize);
}

void logger_enable_color(logger_t* logger, int on) {
    logger->enable_color = on;
}

void logger_set_file(logger_t* logger, const char* filepath) {
    strncpy(logger->filepath, filepath, sizeof(logger->filepath));
    // remove suffix .log
    char* suffix = strrchr(logger->filepath, '.');
    if (suffix && strcmp(suffix, ".log") == 0) {
        *suffix = '\0';
    }
}

void logger_set_max_filesize(logger_t* logger, unsigned long long filesize) {
    logger->max_filesize = filesize;
}

void logger_enable_fsync(logger_t* logger, int on) {
    logger->enable_fsync = on;
}

void logger_fsync(logger_t* logger) {
    hmutex_lock(&logger->mutex_);
    if (logger->fp_) {
        fflush(logger->fp_);
    }
    hmutex_unlock(&logger->mutex_);
}

static void ts_logfile(const char* filepath, time_t ts, char* buf, int len) {
    struct tm* tm = localtime(&ts);
    snprintf(buf, len, "%s-%04d-%02d-%02d.log",
            filepath,
            tm->tm_year+1900,
            tm->tm_mon+1,
            tm->tm_mday);
}

static FILE* shift_logfile(logger_t* logger) {
    time_t ts_now = time(NULL);
    int interval_days = logger->last_logfile_ts == 0 ? 0 : (ts_now+s_gmtoff) / SECONDS_PER_DAY - (logger->last_logfile_ts+s_gmtoff) / SECONDS_PER_DAY;;
    if (logger->fp_ == NULL || interval_days > 0) {
        // close old logfile
        if (logger->fp_) {
            fclose(logger->fp_);
            logger->fp_ = NULL;
        }
        else {
            interval_days = 30;
        }

        if (logger->remain_days >= 0) {
            if (interval_days >= logger->remain_days) {
                // remove [today-interval_days, today-remain_days] logfile
                char rm_logfile[256] = {0};
                for (int i = interval_days; i >= logger->remain_days; --i) {
                    time_t ts_rm  = ts_now - i * SECONDS_PER_DAY;
                    ts_logfile(logger->filepath, ts_rm, rm_logfile, sizeof(rm_logfile));
                    remove(rm_logfile);
                }
            }
            else {
                // remove today-remain_days logfile
                char rm_logfile[256] = {0};
                time_t ts_rm  = ts_now - logger->remain_days * SECONDS_PER_DAY;
                ts_logfile(logger->filepath, ts_rm, rm_logfile, sizeof(rm_logfile));
                remove(rm_logfile);
            }
        }
    }

    // open today logfile
    if (logger->fp_ == NULL) {
        ts_logfile(logger->filepath, ts_now, logger->cur_logfile, sizeof(logger->cur_logfile));
        logger->fp_ = fopen(logger->cur_logfile, "a");
        logger->last_logfile_ts = ts_now;
    }

    // ftruncate
    // NOTE; estimate can_write_cnt to avoid frequent fseek/ftell
    static int s_can_write_cnt = 0;
    if (logger->fp_ && --s_can_write_cnt < 0) {
        fseek(logger->fp_, 0, SEEK_END);
        long filesize = ftell(logger->fp_);
        if (filesize > logger->max_filesize) {
            fclose(logger->fp_);
            logger->fp_ = NULL;
            logger->fp_ = fopen(logger->cur_logfile, "w");
            // reopen with O_APPEND for multi-processes
            if (logger->fp_) {
                fclose(logger->fp_);
                logger->fp_ = fopen(logger->cur_logfile, "a");
            }
        }
        else {
            s_can_write_cnt = (logger->max_filesize - filesize) / logger->bufsize;
        }
    }

    return logger->fp_;
}

int logger_print(logger_t* logger, int level, const char* fmt, ...) {
    if (level < logger->level)
        return -10;

    const char* pcolor = "";
    const char* plevel = "";
#define XXX(id, str, clr) \
    case id: plevel = str; pcolor = clr; break;

    switch (level) {
        LOG_LEVEL_MAP(XXX)
    }
#undef XXX

    if (!logger->enable_color) {
        pcolor = "";
    }

    // lock logger->buf
    int year,month,day,hour,min,sec,ms;
#ifdef _WIN32
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    year     = tm.wYear;
    month    = tm.wMonth;
    day      = tm.wDay;
    hour     = tm.wHour;
    min      = tm.wMinute;
    sec      = tm.wSecond;
    ms       = tm.wMilliseconds;
#else
    struct timeval tv;
    struct tm* tm = NULL;
    gettimeofday(&tv, NULL);
    time_t tt = tv.tv_sec;
    tm = localtime(&tt);
    year     = tm->tm_year + 1900;
    month    = tm->tm_mon  + 1;
    day      = tm->tm_mday;
    hour     = tm->tm_hour;
    min      = tm->tm_min;
    sec      = tm->tm_sec;
    ms       = tv.tv_usec/1000;
#endif
    hmutex_lock(&logger->mutex_);
    char* buf = logger->buf;
    int bufsize = logger->bufsize;
    int len = snprintf(buf, bufsize, "%s[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s] ",
        pcolor,
        year, month, day, hour, min, sec, ms, plevel);

    va_list ap;
    va_start(ap, fmt);
    len += vsnprintf(buf + len, bufsize - len, fmt, ap);
    va_end(ap);

    if (logger->enable_color) {
        len += snprintf(buf + len, bufsize - len, "%s", CLR_CLR);
    }

    if (logger->handler) {
        logger->handler(level, buf, len);
    }
    else {
        FILE* fp = shift_logfile(logger);
        if (fp) {
            fwrite(buf, 1, len, fp);
            if (logger->enable_fsync) {
                fflush(fp);
            }
        }
    }

    hmutex_unlock(&logger->mutex_);
    return len;
}

logger_t* default_logger() {
    static logger_t* s_logger = NULL;
    if (s_logger == NULL) {
        s_logger = logger_create();
    }
    return s_logger;
}

void stdout_logger(int loglevel, const char* buf, int len) {
    fprintf(stdout, "%.*s", len, buf);
}

void stderr_logger(int loglevel, const char* buf, int len) {
    fprintf(stderr, "%.*s", len, buf);
}

void file_logger(int loglevel, const char* buf, int len) {
    logger_t* logger = default_logger();
    FILE* fp = shift_logfile(logger);
    if (fp) {
        fwrite(buf, 1, len, fp);
        if (logger->enable_fsync) {
            fflush(fp);
        }
    }
}
