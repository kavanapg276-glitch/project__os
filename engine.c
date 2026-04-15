#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <time.h>
#include "monitor_ioctl.h"

#define SOCKET_PATH "/tmp/engine.sock"
#define LOG_DIR "logs/"

enum container_state { RUNNING, STOPPED, KILLED };

struct container_metadata {
    char id[64];
    pid_t pid;
    int active;
    enum container_state state;
    time_t start_time;
    int soft_mb;
    int hard_mb;
    char log_path[128];
    int exit_code;
    int exit_signal;
    int stop_requested;
};

struct container_metadata containers[256];
int num_containers = 0;
pthread_mutex_t meta_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char data[1024];
    int length;
    char container_id[64];
    int sentinel;
} log_entry_t;

log_entry_t ring_buffer[256];
int ring_head = 0;
int ring_tail = 0;
int ring_count = 0;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t log_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t log_not_full = PTHREAD_COND_INITIALIZER;
int logger_shutdown = 0;
pthread_t consumer_thread;

volatile sig_atomic_t daemon_running = 1;

struct cli_request {
    char cmd[16];
    char id[64];
    char rootfs[256];
    char command[256];
    int soft_mb;
    int hard_mb;
    int nice;
};

struct cli_response {
    int status;
    char output[4096];
    int exit_code;
};

void sigint_handler(int sig) {
    daemon_running = 0;
}

void sigchld_handler(int sig) {
    int saved_errno = errno;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        pthread_mutex_lock(&meta_mutex);
        for (int i = 0; i < num_containers; i++) {
            if (containers[i].pid == pid && containers[i].active) {
                if (WIFEXITED(status)) {
                    containers[i].exit_code = WEXITSTATUS(status);
                    containers[i].exit_signal = 0;
                } else if (WIFSIGNALED(status)) {
                    containers[i].exit_code = -1;
                    containers[i].exit_signal = WTERMSIG(status);
                }
                
                if (containers[i].stop_requested) {
                    containers[i].state = STOPPED;
                } else if (containers[i].exit_signal == SIGKILL && !containers[i].stop_requested) {
                    containers[i].state = KILLED;
                } else {
                    containers[i].state = STOPPED;
                }
                break;
            }
        }
        pthread_mutex_unlock(&meta_mutex);
    }
    errno = saved_errno;
}

void *log_consumer(void *arg) {
    while (1) {
        pthread_mutex_lock(&log_mutex);
        while (ring_count == 0 && !logger_shutdown) {
            pthread_cond_wait(&log_not_empty, &log_mutex);
        }
        
        if (ring_count == 0 && logger_shutdown) {
            pthread_mutex_unlock(&log_mutex);
            break;
        }

        log_entry_t entry = ring_buffer[ring_head];
        ring_head = (ring_head + 1) % 256;
        ring_count--;
        pthread_cond_signal(&log_not_full);
        pthread_mutex_unlock(&log_mutex);

        if (!entry.sentinel) {
            char path[256];
            snprintf(path, sizeof(path), LOG_DIR "%s.log", entry.container_id);
            FILE *f = fopen(path, "a");
            if (f) {
                fwrite(entry.data, 1, entry.length, f);
                fclose(f);
            }
        }
    }
    return NULL;
}

struct producer_args {
    int fd;
    char id[64];
};

void *producer_thread(void *arg) {
    struct producer_args *pargs = (struct producer_args *)arg;
    char buf[1024];
    int n;
    while ((n = read(pargs->fd, buf, sizeof(buf))) > 0) {
        pthread_mutex_lock(&log_mutex);
        while (ring_count == 256 && !logger_shutdown) {
            pthread_cond_wait(&log_not_full, &log_mutex);
        }
        if (logger_shutdown) {
            pthread_mutex_unlock(&log_mutex);
            break;
        }
        log_entry_t *entry = &ring_buffer[ring_tail];
        memcpy(entry->data, buf, n);
        entry->length = n;
        strncpy(entry->container_id, pargs->id, sizeof(entry->container_id) - 1);
        entry->container_id[sizeof(entry->container_id) - 1] = '\0';
        entry->sentinel = 0;
        
        ring_tail = (ring_tail + 1) % 256;
        ring_count++;
        pthread_cond_signal(&log_not_empty);
        pthread_mutex_unlock(&log_mutex);
    }
    
    pthread_mutex_lock(&log_mutex);
    while (ring_count == 256 && !logger_shutdown) {
        pthread_cond_wait(&log_not_full, &log_mutex);
    }
    if (!logger_shutdown) {
        log_entry_t *entry = &ring_buffer[ring_tail];
        entry->length = 0;
        strncpy(entry->container_id, pargs->id, sizeof(entry->container_id) - 1);
        entry->container_id[sizeof(entry->container_id) - 1] = '\0';
        entry->sentinel = 1;
        ring_tail = (ring_tail + 1) % 256;
        ring_count++;
        pthread_cond_signal(&log_not_empty);
    }
    pthread_mutex_unlock(&log_mutex);
    
    close(pargs->fd);
    free(pargs);
    return NULL;
}

struct child_args {
    int pipe_fd;
    char rootfs[256];
    char cmd[256];
    int nice_val;
};

static int child_exec(void *arg) {
    struct child_args *cargs = (struct child_args *)arg;
    
    for (int i = 3; i < 1024; i++) {
        if (i != cargs->pipe_fd) close(i);
    }
    
    dup2(cargs->pipe_fd, STDOUT_FILENO);
    dup2(cargs->pipe_fd, STDERR_FILENO);
    close(cargs->pipe_fd);

    if (cargs->nice_val != 0) {
        nice(cargs->nice_val);
    }

    if (mount(cargs->rootfs, cargs->rootfs, "", MS_BIND | MS_REC, NULL) < 0) {
        exit(1);
    }
    
    if (chroot(cargs->rootfs) < 0) {
        exit(1);
    }
    
    if (chdir("/") < 0) {
        exit(1);
    }
    
    mkdir("/proc", 0755);
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        exit(1);
    }

    char *exec_args[] = { "/bin/sh", "-c", cargs->cmd, NULL };
    execv("/bin/sh", exec_args);
    exit(1);
}

void *client_handler(void *arg) {
    int client_fd = (int)(long)arg;
    struct cli_request req;
    struct cli_response resp;
    memset(&resp, 0, sizeof(resp));
    
    int total = 0;
    while (total < sizeof(req)) {
        int r = read(client_fd, ((char*)&req) + total, sizeof(req) - total);
        if (r <= 0) {
            close(client_fd);
            return NULL;
        }
        total += r;
    }
    
    if (strcmp(req.cmd, "logs") == 0) {
        char path[256];
        snprintf(path, sizeof(path), LOG_DIR "%s.log", req.id);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[1024];
            int n;
            while ((n = read(fd, buf, sizeof(buf))) > 0) {
                write(client_fd, buf, n);
            }
            close(fd);
        }
        close(client_fd);
        return NULL;
    }
    
    if (strcmp(req.cmd, "ps") == 0) {
        pthread_mutex_lock(&meta_mutex);
        int offset = snprintf(resp.output, sizeof(resp.output), "ID\tPID\tSTATE\tSOFT_MB\tHARD_MB\tEXIT_CODE\n");
        for (int i = 0; i < num_containers; i++) {
            if (!containers[i].active) continue;
            char *state_str = "UNKNOWN";
            if (containers[i].state == RUNNING) state_str = "RUNNING";
            else if (containers[i].state == STOPPED) state_str = "STOPPED";
            else if (containers[i].state == KILLED) state_str = "KILLED";
            
            offset += snprintf(resp.output + offset, sizeof(resp.output) - offset,
                "%s\t%d\t%s\t%d\t%d\t%d\n",
                containers[i].id,
                containers[i].pid,
                state_str,
                containers[i].soft_mb,
                containers[i].hard_mb,
                containers[i].exit_code);
        }
        pthread_mutex_unlock(&meta_mutex);
        resp.status = 0;
        write(client_fd, &resp, sizeof(resp));
        close(client_fd);
        return NULL;
    }
    
    if (strcmp(req.cmd, "stop") == 0) {
        pthread_mutex_lock(&meta_mutex);
        struct container_metadata *meta = NULL;
        for (int i = 0; i < num_containers; i++) {
            if (strcmp(containers[i].id, req.id) == 0 && containers[i].active) {
                meta = &containers[i];
                break;
            }
        }
        if (meta && meta->state == RUNNING) {
            meta->stop_requested = 1;
            pid_t pid = meta->pid;
            pthread_mutex_unlock(&meta_mutex);
            
            kill(pid, SIGTERM);
            for (int i = 0; i < 50; i++) {
                pthread_mutex_lock(&meta_mutex);
                int state = meta->state;
                pthread_mutex_unlock(&meta_mutex);
                if (state != RUNNING) break;
                usleep(100000);
            }
            
            pthread_mutex_lock(&meta_mutex);
            if (meta->state == RUNNING) {
                kill(pid, SIGKILL);
            }
            pthread_mutex_unlock(&meta_mutex);
            
            resp.status = 0;
        } else {
            pthread_mutex_unlock(&meta_mutex);
            resp.status = -1;
        }
        write(client_fd, &resp, sizeof(resp));
        close(client_fd);
        return NULL;
    }

    if (strcmp(req.cmd, "run") == 0 || strcmp(req.cmd, "start") == 0) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            resp.status = -1;
            write(client_fd, &resp, sizeof(resp));
            close(client_fd);
            return NULL;
        }
        
        struct child_args *cargs = malloc(sizeof(struct child_args));
        cargs->pipe_fd = pipefd[1];
        strncpy(cargs->rootfs, req.rootfs, sizeof(cargs->rootfs) - 1);
        strncpy(cargs->cmd, req.command, sizeof(cargs->cmd) - 1);
        cargs->nice_val = req.nice;
        
        char *stack = malloc(1024 * 1024);
        pid_t pid = clone(child_exec, stack + 1024 * 1024, CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, cargs);
        close(pipefd[1]);
        
        if (pid > 0) {
            int mfd = open("/dev/container_monitor", O_RDWR);
            if (mfd >= 0) {
                struct container_config cfg;
                cfg.pid = pid;
                cfg.soft_limit_mb = req.soft_mb;
                cfg.hard_limit_mb = req.hard_mb;
                ioctl(mfd, MONITOR_REGISTER, &cfg);
                close(mfd);
            }
            
            pthread_mutex_lock(&meta_mutex);
            int idx = num_containers++;
            strncpy(containers[idx].id, req.id, sizeof(containers[idx].id) - 1);
            containers[idx].pid = pid;
            containers[idx].active = 1;
            containers[idx].state = RUNNING;
            containers[idx].soft_mb = req.soft_mb;
            containers[idx].hard_mb = req.hard_mb;
            containers[idx].stop_requested = 0;
            containers[idx].exit_code = 0;
            containers[idx].start_time = time(NULL);
            snprintf(containers[idx].log_path, sizeof(containers[idx].log_path), LOG_DIR "%s.log", req.id);
            pthread_mutex_unlock(&meta_mutex);
            
            struct producer_args *pa = malloc(sizeof(*pa));
            pa->fd = pipefd[0];
            strncpy(pa->id, req.id, sizeof(pa->id) - 1);
            pthread_t th;
            pthread_create(&th, NULL, producer_thread, pa);
            pthread_detach(th);
            
            snprintf(resp.output, sizeof(resp.output), "Started container %s with PID %d\n", req.id, pid);
            resp.status = 0;
        } else {
            close(pipefd[0]);
            resp.status = -1;
        }
        
        if (strcmp(req.cmd, "run") == 0 && resp.status == 0) {
            while (1) {
                pthread_mutex_lock(&meta_mutex);
                int done = 0;
                int ecode = 0;
                for (int i = 0; i < num_containers; i++) {
                    if (strcmp(containers[i].id, req.id) == 0 && containers[i].state != RUNNING) {
                        done = 1;
                        ecode = containers[i].exit_code;
                        break;
                    }
                }
                pthread_mutex_unlock(&meta_mutex);
                if (done) {
                    resp.exit_code = ecode;
                    break;
                }
                usleep(100000);
            }
        }
        write(client_fd, &resp, sizeof(resp));
    }

    close(client_fd);
    return NULL;
}

int run_daemon(void) {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    mkdir(LOG_DIR, 0755);
    
    pthread_create(&consumer_thread, NULL, log_consumer, NULL);
    
    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock == -1) return 1;
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    unlink(SOCKET_PATH);
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        return 1;
    }
    listen(server_sock, 10);
    
    while (daemon_running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_sock, &fds);
        struct timeval tv = {1, 0};
        
        int ret = select(server_sock + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            int client_fd = accept(server_sock, NULL, NULL);
            if (client_fd >= 0) {
                pthread_t th;
                pthread_create(&th, NULL, client_handler, (void*)(long)client_fd);
                pthread_detach(th);
            }
        }
    }
    
    for (int i = 0; i < num_containers; i++) {
        if (containers[i].state == RUNNING) {
            kill(containers[i].pid, SIGTERM);
        }
    }
    
    while (1) {
        int running = 0;
        pthread_mutex_lock(&meta_mutex);
        for (int i = 0; i < num_containers; i++) {
            if (containers[i].state == RUNNING) running = 1;
        }
        pthread_mutex_unlock(&meta_mutex);
        if (!running) break;
        usleep(100000);
    }
    
    pthread_mutex_lock(&log_mutex);
    logger_shutdown = 1;
    pthread_cond_broadcast(&log_not_empty);
    pthread_cond_broadcast(&log_not_full);
    pthread_mutex_unlock(&log_mutex);
    pthread_join(consumer_thread, NULL);
    
    close(server_sock);
    unlink(SOCKET_PATH);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return 1;
    }
    
    if (strcmp(argv[1], "daemon") == 0 || strcmp(argv[1], "supervisor") == 0) {
        printf("Supervisor running...\n");
        return run_daemon();
    }
    
    struct cli_request req;
    memset(&req, 0, sizeof(req));
    strncpy(req.cmd, argv[1], sizeof(req.cmd) - 1);
    
    int optind = 2;
    if (strcmp(req.cmd, "start") == 0 || strcmp(req.cmd, "run") == 0) {
        req.soft_mb = 0;
        req.hard_mb = 0;
        req.nice = 0;
        
        if (optind < argc) strncpy(req.id, argv[optind++], sizeof(req.id) - 1);
        if (optind < argc) strncpy(req.rootfs, argv[optind++], sizeof(req.rootfs) - 1);
        if (optind < argc) strncpy(req.command, argv[optind++], sizeof(req.command) - 1);
        
        while (optind < argc) {
            if (strcmp(argv[optind], "--soft-mib") == 0 && optind + 1 < argc) {
                req.soft_mb = atoi(argv[optind+1]);
                optind += 2;
            } else if (strcmp(argv[optind], "--hard-mib") == 0 && optind + 1 < argc) {
                req.hard_mb = atoi(argv[optind+1]);
                optind += 2;
            } else if (strcmp(argv[optind], "--nice") == 0 && optind + 1 < argc) {
                req.nice = atoi(argv[optind+1]);
                optind += 2;
            } else {
                optind++;
            }
        }
    } else if (strcmp(req.cmd, "stop") == 0 || strcmp(req.cmd, "logs") == 0) {
        if (optind < argc) strncpy(req.id, argv[optind], sizeof(req.id) - 1);
    }
    
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) return 1;
    
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(sock);
        return 1;
    }
    
    write(sock, &req, sizeof(req));
    
    if (strcmp(req.cmd, "logs") == 0) {
        char buf[1024];
        int n;
        while ((n = read(sock, buf, sizeof(buf))) > 0) {
            write(STDOUT_FILENO, buf, n);
        }
    } else {
        struct cli_response resp;
        int total = 0;
        while (total < sizeof(resp)) {
            int r = read(sock, ((char*)&resp) + total, sizeof(resp) - total);
            if (r <= 0) break;
            total += r;
        }
        
        if (total == sizeof(resp)) {
            if (strcmp(req.cmd, "ps") == 0) {
                printf("%s", resp.output);
            } else if (strcmp(req.cmd, "run") == 0) {
                if (resp.status == 0) {
                    printf("Exit code: %d\n", resp.exit_code);
                } else {
                    printf("Failed to run container\n");
                }
            } else if (strcmp(req.cmd, "start") == 0 || strcmp(req.cmd, "stop") == 0) {
                if (resp.status != 0) {
                    printf("Command failed\n");
                } else if (strcmp(req.cmd, "start") == 0 && resp.output[0] != '\0') {
                    printf("%s", resp.output);
                }
            }
        }
    }
    
    close(sock);
    return 0;
}
