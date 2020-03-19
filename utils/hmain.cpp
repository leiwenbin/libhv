#include "hmain.h"

#include "hplatform.h"
#include "hlog.h"
#include "htime.h"
#include "herr.h"
#include "hthread.h"

main_ctx_t  g_main_ctx;
int         g_worker_processes_num = 0;
int         g_worker_threads_num = 0;
proc_ctx_t* g_worker_processes = NULL;
procedure_t g_worker_fn = NULL;
void*       g_worker_userdata = NULL;

int main_ctx_init(int argc, char** argv) {
    if (argc == 0 || argv == NULL) {
        argc = 1;
        argv = (char**)malloc(2*sizeof(char*));
        argv[0] = (char*)malloc(MAX_PATH);
        argv[1] = NULL;
#ifdef OS_WIN
        GetModuleFileName(NULL, argv[0], MAX_PATH);
#elif defined(OS_LINUX)
        readlink("/proc/self/exe", argv[0], MAX_PATH);
#else
        strcpy(argv[0], "./unnamed");
#endif
    }

    char* cwd = getcwd(g_main_ctx.run_path, sizeof(g_main_ctx.run_path));
    if (cwd == NULL) {
        printf("getcwd error\n");
    }
    //printf("run_path=%s\n", g_main_ctx.run_path);
    const char* b = argv[0];
    const char* e = b;
    while (*e) ++e;
    --e;
    while (e >= b) {
        if (*e == '/' || *e == '\\') {
            break;
        }
        --e;
    }
    strncpy(g_main_ctx.program_name, e+1, sizeof(g_main_ctx.program_name));
#ifdef OS_WIN
    if (strcmp(g_main_ctx.program_name+strlen(g_main_ctx.program_name)-4, ".exe") == 0) {
        *(g_main_ctx.program_name+strlen(g_main_ctx.program_name)-4) = '\0';
    }
#endif
    //printf("program_name=%s\n", g_main_ctx.program_name);
    char logpath[MAX_PATH] = {0};
    snprintf(logpath, sizeof(logpath), "%s/logs", g_main_ctx.run_path);
    MKDIR(logpath);
    snprintf(g_main_ctx.confile, sizeof(g_main_ctx.confile), "%s/etc/%s.conf", g_main_ctx.run_path, g_main_ctx.program_name);
    snprintf(g_main_ctx.pidfile, sizeof(g_main_ctx.pidfile), "%s/logs/%s.pid", g_main_ctx.run_path, g_main_ctx.program_name);
    snprintf(g_main_ctx.logfile, sizeof(g_main_ctx.confile), "%s/logs/%s.log", g_main_ctx.run_path, g_main_ctx.program_name);
    hlog_set_file(g_main_ctx.logfile);

    g_main_ctx.pid = getpid();
    g_main_ctx.oldpid = getpid_from_pidfile();
#ifdef OS_UNIX
    if (kill(g_main_ctx.oldpid, 0) == -1 && errno == ESRCH) {
        g_main_ctx.oldpid = -1;
    }
#else
    HANDLE hproc = OpenProcess(PROCESS_TERMINATE, FALSE, g_main_ctx.oldpid);
    if (hproc == NULL) {
        g_main_ctx.oldpid = -1;
    }
    else {
        CloseHandle(hproc);
    }
#endif

    // save arg
    int i = 0;
    g_main_ctx.os_argv = argv;
    g_main_ctx.argc = 0;
    g_main_ctx.arg_len = 0;
    for (i = 0; argv[i]; ++i) {
        g_main_ctx.arg_len += strlen(argv[i]) + 1;
    }
    g_main_ctx.argc = i;
    char* argp = (char*)malloc(g_main_ctx.arg_len);
    memset(argp, 0, g_main_ctx.arg_len);
    g_main_ctx.save_argv = (char**)malloc((g_main_ctx.argc+1) * sizeof(char*));
    char* cmdline = (char*)malloc(g_main_ctx.arg_len);
    g_main_ctx.cmdline = cmdline;
    for (i = 0; argv[i]; ++i) {
        g_main_ctx.save_argv[i] = argp;
        strcpy(g_main_ctx.save_argv[i], argv[i]);
        argp += strlen(argv[i]) + 1;

        strcpy(cmdline, argv[i]);
        cmdline += strlen(argv[i]);
        *cmdline = ' ';
        ++cmdline;
    }
    g_main_ctx.save_argv[g_main_ctx.argc] = NULL;
    g_main_ctx.cmdline[g_main_ctx.arg_len-1] = '\0';

#if defined(OS_WIN) || defined(OS_LINUX)
    // save env
    g_main_ctx.os_envp = environ;
    g_main_ctx.envc = 0;
    g_main_ctx.env_len = 0;
    for (i = 0; environ[i]; ++i) {
        g_main_ctx.env_len += strlen(environ[i]) + 1;
    }
    g_main_ctx.envc = i;
    char* envp = (char*)malloc(g_main_ctx.env_len);
    memset(envp, 0, g_main_ctx.env_len);
    g_main_ctx.save_envp = (char**)malloc((g_main_ctx.envc+1) * sizeof(char*));
    for (i = 0; environ[i]; ++i) {
        g_main_ctx.save_envp[i] = envp;
        strcpy(g_main_ctx.save_envp[i], environ[i]);
        envp += strlen(environ[i]) + 1;
    }
    g_main_ctx.save_envp[g_main_ctx.envc] = NULL;

    // parse env
    for (i = 0; environ[i]; ++i) {
        char* b = environ[i];
        char* delim = strchr(b, '=');
        if (delim == NULL) {
            continue;
        }
        g_main_ctx.env_kv[std::string(b, delim-b)] = std::string(delim+1);
    }
#endif

    return 0;
}

#define UNDEFINED_OPTION    -1
static int get_arg_type(int short_opt, const char* options) {
    if (options == NULL) return UNDEFINED_OPTION;
    const char* p = options;
    while (*p && *p != short_opt) ++p;
    if (*p == '\0')     return UNDEFINED_OPTION;
    if (*(p+1) == ':')  return REQUIRED_ARGUMENT;
    return NO_ARGUMENT;
}

int parse_opt(int argc, char** argv, const char* options) {
    for (int i = 1; argv[i]; ++i) {
        char* p = argv[i];
        if (*p != '-') {
            g_main_ctx.arg_list.push_back(argv[i]);
            continue;
        }
        while (*++p) {
            int arg_type = get_arg_type(*p, options);
            if (arg_type == UNDEFINED_OPTION) {
                printf("Invalid option '%c'\n", *p);
                return -20;
            } else if (arg_type == NO_ARGUMENT) {
                g_main_ctx.arg_kv[std::string(p, 1)] = OPTION_ENABLE;
                continue;
            } else if (arg_type == REQUIRED_ARGUMENT) {
                if (*(p+1) != '\0') {
                    g_main_ctx.arg_kv[std::string(p, 1)] = p+1;
                    break;
                } else if (argv[i+1] != NULL) {
                    g_main_ctx.arg_kv[std::string(p, 1)] = argv[++i];
                    break;
                } else {
                    printf("Option '%c' requires param\n", *p);
                    return -30;
                }
            }
        }
    }
    return 0;
}

static const option_t* get_option(const char* opt, const option_t* long_options, int size) {
    if (opt == NULL || long_options == NULL) return NULL;
    int len = strlen(opt);
    if (len == 0)   return NULL;
    if (len == 1) {
        for (int i = 0; i < size; ++i) {
            if (long_options[i].short_opt == *opt) {
                return &long_options[i];
            }
        }
    } else {
        for (int i = 0; i < size; ++i) {
            if (strcmp(long_options[i].long_opt, opt) == 0) {
                return &long_options[i];
            }
        }
    }

    return NULL;
}

#define MAX_OPTION      32
// opt type
#define NOPREFIX_OPTION 0
#define SHORT_OPTION    -1
#define LONG_OPTION     -2
int parse_opt_long(int argc, char** argv, const option_t* long_options, int size) {
    char opt[MAX_OPTION+1] = {0};
    for (int i = 1; argv[i]; ++i) {
        char* arg = argv[i];
        int opt_type = NOPREFIX_OPTION;
        // prefix
        if (*arg == OPTION_PREFIX) {
            ++arg;
            opt_type = SHORT_OPTION;
            if (*arg == OPTION_PREFIX) {
                ++arg;
                opt_type = LONG_OPTION;
            }
        }
        int arg_len  = strlen(arg);
        // delim
        char* delim = strchr(arg, OPTION_DELIM);
        if (delim == arg || delim == arg+arg_len-1 || delim-arg > MAX_OPTION) {
            printf("Invalid option '%s'\n", argv[i]);
            return -10;
        }
        if (delim) {
            memcpy(opt, arg, delim-arg);
            opt[delim-arg] = '\0';
        } else {
            if (opt_type == SHORT_OPTION) {
                *opt = *arg;
                opt[1] = '\0';
            } else {
                strncpy(opt, arg, MAX_OPTION);
            }
        }
        // get_option
        const option_t* pOption = get_option(opt, long_options, size);
        if (pOption == NULL) {
            if (delim == NULL && opt_type == NOPREFIX_OPTION) {
                g_main_ctx.arg_list.push_back(arg);
                continue;
            } else {
                printf("Invalid option: '%s'\n", argv[i]);
                return -10;
            }
        }
        const char* value = NULL;
        if (pOption->arg_type == NO_ARGUMENT) {
            // -h
            value = OPTION_ENABLE;
        } else if (pOption->arg_type == REQUIRED_ARGUMENT) {
            if (delim) {
                // --port=80
                value = delim+1;
            } else {
                if (opt_type == SHORT_OPTION && *(arg+1) != '\0') {
                    // p80
                    value = arg+1;
                } else if (argv[i+1] != NULL) {
                    // --port 80
                    value = argv[++i];
                } else {
                    printf("Option '%s' requires parament\n", opt);
                    return -20;
                }
            }
        }
        // preferred to use short_opt as key
        if (pOption->short_opt > 0) {
            g_main_ctx.arg_kv[std::string(1, pOption->short_opt)] = value;
        } else if (pOption->long_opt) {
            g_main_ctx.arg_kv[pOption->long_opt] = value;
        }
    }
    return 0;
}

const char* get_arg(const char* key) {
    auto iter = g_main_ctx.arg_kv.find(key);
    if (iter == g_main_ctx.arg_kv.end()) {
        return NULL;
    }
    return iter->second.c_str();
}

const char* get_env(const char* key) {
    auto iter = g_main_ctx.env_kv.find(key);
    if (iter == g_main_ctx.env_kv.end()) {
        return NULL;
    }
    return iter->second.c_str();
}

#ifdef OS_UNIX
/*
 * memory layout
 * argv[0]\0argv[1]\0argv[n]\0env[0]\0env[1]\0env[n]\0
 */
void setproctitle(const char* title) {
    //printf("proctitle=%s\n", title);
    int len = g_main_ctx.arg_len + g_main_ctx.env_len;
    if (g_main_ctx.os_argv && len) {
        strncpy(g_main_ctx.os_argv[0], title, len-1);
    }
}
#endif

int create_pidfile() {
    FILE* fp = fopen(g_main_ctx.pidfile, "w");
    if (fp == NULL) {
        hloge("fopen('%s') error: %d", g_main_ctx.pidfile, errno);
        return -1;
    }

    char pid[16] = {0};
    snprintf(pid, sizeof(pid), "%d\n", g_main_ctx.pid);
    fwrite(pid, 1, strlen(pid), fp);
    fclose(fp);
    hlogi("create_pidfile('%s') pid=%d", g_main_ctx.pidfile, g_main_ctx.pid);
    atexit(delete_pidfile);
    return 0;
}

void delete_pidfile() {
    hlogi("delete_pidfile('%s') pid=%d", g_main_ctx.pidfile, g_main_ctx.pid);
    remove(g_main_ctx.pidfile);
}

pid_t getpid_from_pidfile() {
    FILE* fp = fopen(g_main_ctx.pidfile, "r");
    if (fp == NULL) {
        // hloge("fopen('%s') error: %d", g_main_ctx.pidfile, errno);
        return -1;
    }
    char pid[64];
    int readbytes = fread(pid, 1, sizeof(pid), fp);
    fclose(fp);
    return readbytes <= 0 ? -1 : atoi(pid);
}

static procedure_t s_reload_fn = NULL;
static void*       s_reload_userdata = NULL;
#ifdef OS_UNIX
// unix use signal
#include <sys/wait.h>

void signal_handler(int signo) {
    hlogi("pid=%d recv signo=%d", getpid(), signo);
    switch (signo) {
    case SIGINT:
    case SIGNAL_TERMINATE:
        hlogi("killall processes");
        signal(SIGCHLD, SIG_IGN);
        // master send SIGKILL => workers
        for (int i = 0; i < g_worker_processes_num; ++i) {
            if (g_worker_processes[i].pid <= 0) break;
            kill(g_worker_processes[i].pid, SIGKILL);
            g_worker_processes[i].pid = -1;
        }
        exit(0);
        break;
    case SIGNAL_RELOAD:
        if (s_reload_fn) {
            s_reload_fn(s_reload_userdata);
            if (getpid_from_pidfile() == getpid()) {
                // master send SIGNAL_RELOAD => workers
                for (int i = 0; i < g_worker_processes_num; ++i) {
                    if (g_worker_processes[i].pid <= 0) break;
                    kill(g_worker_processes[i].pid, SIGNAL_RELOAD);
                }
            }
        }
        break;
    case SIGCHLD:
    {
        pid_t pid = 0;
        int status = 0;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            hlogw("proc stop/waiting, pid=%d status=%d", pid, status);
            for (int i = 0; i < g_worker_processes_num; ++i) {
                if (g_worker_processes[i].pid == pid) {
                    g_worker_processes[i].pid = -1;
                    hproc_spawn(&g_worker_processes[i]);
                    break;
                }
            }
        }
    }
        break;
    default:
        break;
    }
}

int signal_init(procedure_t reload_fn, void* reload_userdata) {
    s_reload_fn = reload_fn;
    s_reload_userdata = reload_userdata;

    signal(SIGINT, signal_handler);
    signal(SIGCHLD, signal_handler);
    signal(SIGNAL_TERMINATE, signal_handler);
    signal(SIGNAL_RELOAD, signal_handler);

    return 0;
}

#elif defined(OS_WIN)
// win32 use Event
//static HANDLE s_hEventTerm = NULL;
static HANDLE s_hEventReload = NULL;

#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif
void WINAPI on_timer(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) {
    DWORD ret;
    /*
    ret = WaitForSingleObject(s_hEventTerm, 0);
    if (ret == WAIT_OBJECT_0) {
        hlogi("pid=%d recv event [TERM]", getpid());
        if (getpid_from_pidfile() == getpid()) {
            timeKillEvent(uTimerID);
            exit(0);
        }
    }
    */

    ret = WaitForSingleObject(s_hEventReload, 0);
    if (ret == WAIT_OBJECT_0) {
        hlogi("pid=%d recv event [RELOAD]", getpid());
        if (s_reload_fn) {
            s_reload_fn(s_reload_userdata);
        }
    }
}

void signal_cleanup() {
    //CloseHandle(s_hEventTerm);
    //s_hEventTerm = NULL;
    CloseHandle(s_hEventReload);
    s_hEventReload = NULL;
}

int signal_init(procedure_t reload_fn, void* reload_userdata) {
    s_reload_fn = reload_fn;
    s_reload_userdata = reload_userdata;

    char eventname[MAX_PATH] = {0};
    //snprintf(eventname, sizeof(eventname), "%s_term_event", g_main_ctx.program_name);
    //s_hEventTerm = CreateEvent(NULL, FALSE, FALSE, eventname);
    //s_hEventTerm = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventname);
    snprintf(eventname, sizeof(eventname), "%s_reload_event", g_main_ctx.program_name);
    s_hEventReload = CreateEvent(NULL, FALSE, FALSE, eventname);

    timeSetEvent(1000, 1000, on_timer, 0, TIME_PERIODIC);

    atexit(signal_cleanup);
    return 0;
}
#endif

static void kill_proc(int pid) {
#ifdef OS_UNIX
    kill(pid, SIGNAL_TERMINATE);
#else
    //SetEvent(s_hEventTerm);
    //sleep(1);
    HANDLE hproc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hproc) {
        TerminateProcess(hproc, 0);
        CloseHandle(hproc);
    }
#endif
}

void handle_signal(const char* signal) {
    if (strcmp(signal, "start") == 0) {
        if (g_main_ctx.oldpid > 0) {
            printf("%s is already running, pid=%d\n", g_main_ctx.program_name, g_main_ctx.oldpid);
            exit(0);
        }
    } else if (strcmp(signal, "stop") == 0) {
        if (g_main_ctx.oldpid > 0) {
            kill_proc(g_main_ctx.oldpid);
            printf("%s stop/waiting\n", g_main_ctx.program_name);
        } else {
            printf("%s is already stopped\n", g_main_ctx.program_name);
        }
        exit(0);
    } else if (strcmp(signal, "restart") == 0) {
        if (g_main_ctx.oldpid > 0) {
            kill_proc(g_main_ctx.oldpid);
            printf("%s stop/waiting\n", g_main_ctx.program_name);
            msleep(1000);
        }
    } else if (strcmp(signal, "status") == 0) {
        if (g_main_ctx.oldpid > 0) {
            printf("%s start/running, pid=%d\n", g_main_ctx.program_name, g_main_ctx.oldpid);
        } else {
            printf("%s stop/waiting\n", g_main_ctx.program_name);
        }
        exit(0);
    } else if (strcmp(signal, "reload") == 0) {
        if (g_main_ctx.oldpid > 0) {
            printf("reload confile [%s]\n", g_main_ctx.confile);
#ifdef OS_UNIX
            kill(g_main_ctx.oldpid, SIGNAL_RELOAD);
#else
            SetEvent(s_hEventReload);
#endif
        }
        sleep(1);
        exit(0);
    } else {
        printf("Invalid signal: '%s'\n", signal);
        exit(0);
    }
    printf("%s start/running\n", g_main_ctx.program_name);
}

// master-workers processes
static HTHREAD_ROUTINE(worker_thread) {
    hlogi("worker_thread pid=%d tid=%d", getpid(), gettid());
    if (g_worker_fn) {
        g_worker_fn(g_worker_userdata);
    }
    return 0;
}

static void worker_init(void* userdata) {
#ifdef OS_UNIX
    char proctitle[256] = {0};
    snprintf(proctitle, sizeof(proctitle), "%s: worker process", g_main_ctx.program_name);
    setproctitle(proctitle);
    signal(SIGNAL_RELOAD, signal_handler);
#endif
}

static void worker_proc(void* userdata) {
    for (int i = 1; i < g_worker_threads_num; ++i) {
        hthread_create(worker_thread, NULL);
    }
    worker_thread(NULL);
}

int master_workers_run(procedure_t worker_fn, void* worker_userdata,
        int worker_processes, int worker_threads, bool wait) {
#ifdef OS_WIN
        // NOTE: Windows not provide MultiProcesses
        if (worker_threads == 0) {
            // MultiProcesses => MultiThreads
            worker_threads = worker_processes;
        }
        worker_processes = 0;
#endif
    if (worker_threads == 0) worker_threads = 1;

    g_worker_threads_num = worker_threads;
    g_worker_fn = worker_fn;
    g_worker_userdata = worker_userdata;

    if (worker_processes == 0) {
        // single process
        if (wait) {
            for (int i = 1; i < worker_threads; ++i) {
                hthread_create(worker_thread, NULL);
            }
            worker_thread(NULL);
        }
        else {
            for (int i = 0; i < worker_threads; ++i) {
                hthread_create(worker_thread, NULL);
            }
        }
    }
    else {
        if (g_worker_processes_num != 0) {
            return ERR_OVER_LIMIT;
        }
        // master-workers processes
#ifdef OS_UNIX
        char proctitle[256] = {0};
        snprintf(proctitle, sizeof(proctitle), "%s: master process", g_main_ctx.program_name);
        setproctitle(proctitle);
        signal(SIGNAL_RELOAD, signal_handler);
#endif
        g_worker_processes_num = worker_processes;
        int bytes = g_worker_processes_num * sizeof(proc_ctx_t);
        g_worker_processes = (proc_ctx_t*)malloc(bytes);
        memset(g_worker_processes, 0, bytes);
        proc_ctx_t* ctx = g_worker_processes;
        for (int i = 0; i < g_worker_processes_num; ++i, ++ctx) {
            ctx->init = worker_init;
            ctx->proc = worker_proc;
            hproc_spawn(ctx);
            hlogi("workers[%d] start/running, pid=%d", i, ctx->pid);
        }
        g_main_ctx.pid = getpid();
        hlogi("master start/running, pid=%d", g_main_ctx.pid);
        if (wait) {
            while (1) sleep (1);
        }
    }
    return 0;;
}
