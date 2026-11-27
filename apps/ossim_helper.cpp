// ossim_helper.cpp - controlled helper for the "Live Linux Demo" pages (Phase 2).
//
// The Qt GUI is multithreaded, so it never calls fork() itself: it starts this small
// single-threaded program with QProcess, and this program performs the real system calls on
// its OWN children, threads and private IPC objects only. Every event is printed as one line:
//
//     T=<ms> ROLE=<parent|child|...> PID=<pid> PPID=<ppid> EVENT=<name> | <detail>
//
// Demos: fork exec wait sleep zombie orphan signal | shm msg pipe fifo | threads <n> <base>
//        Phase 3: fsdemo (file syscalls) | worker <s> <MiB> <busy%> (System Monitor) | jobs (process groups)
// Rules: no root, no arbitrary commands, no signals to foreign processes, bounded waits,
// every child is reaped and every IPC object / temporary file is removed on all paths.
#include <algorithm>
#include <cerrno>
#include <climits>
#include <dirent.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

static volatile sig_atomic_t g_stop = 0;           // set by SIGTERM (Stop button)
static volatile sig_atomic_t g_usr1 = 0;
static long g_t0 = 0;
static const char* g_self = "ossim_helper";

static long nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// One event line, written with a single write() so lines from parent / children never mix.
static void emit(const char* role, const char* event, const std::string& detail) {
    char buf[1024];
    int n = snprintf(buf, sizeof buf, "T=%ld ROLE=%s PID=%d PPID=%d EVENT=%s | %s\n", nowMs() - g_t0, role,
                     (int)getpid(), (int)getppid(), event, detail.c_str());
    if (n > 0) (void)!write(1, buf, std::min<size_t>((size_t)n, sizeof buf - 1));
}
static std::string str(long v) { return std::to_string(v); }
static std::string err(const char* what) { return std::string(what) + " failed: " + strerror(errno); }

static void onTerm(int) { g_stop = 1; }
static void onUsr1(int) { g_usr1 = 1; }

static void msleep(int ms) {
    for (int i = 0; i < ms / 10 && !g_stop; ++i) usleep(10000);
}

// Bounded wait for one child (never hangs): returns the raw status, or -1 (killed after timeout).
static int reap(pid_t pid, const char* role, int timeoutMs = 5000) {
    int st = 0;
    long start = nowMs();
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) { emit(role, "error", err("waitpid")); return -1; }
        if (g_stop || nowMs() - start > timeoutMs) {
            kill(pid, SIGKILL);                     // our own child only
            waitpid(pid, &st, 0);
            emit(role, "cleanup", "child " + str(pid) + (g_stop ? " stopped by the user" : " timed out") + " and reaped");
            return -1;
        }
        usleep(5000);
    }
}

static std::string statusText(int st) {
    if (st < 0) return "not finished normally";
    if (WIFEXITED(st)) return "exited with status " + str(WEXITSTATUS(st));
    if (WIFSIGNALED(st)) return "killed by signal " + str(WTERMSIG(st));
    return "status " + str(st);
}

// ------------------------------------------------------------------ process lifecycle

static int demoFork() {
    emit("parent", "before-fork", "one process is about to call fork()");
    pid_t c = fork();
    if (c < 0) { emit("parent", "error", err("fork")); return 1; }
    if (c == 0) {
        emit("child", "fork-returned", "fork() returned 0 in the child; new PID " + str(getpid()) + ", parent " + str(getppid()));
        msleep(200);
        emit("child", "exit", "the child calls exit(7)");
        _exit(7);
    }
    emit("parent", "fork-returned", "fork() returned " + str(c) + " (the child's PID) in the parent");
    int st = reap(c, "parent");
    emit("parent", "reaped", "waitpid(" + str(c) + ") -> child " + statusText(st));
    return 0;
}

static int demoExec() {
    pid_t c = fork();
    if (c < 0) { emit("parent", "error", err("fork")); return 1; }
    if (c == 0) {
        emit("child", "before-exec", "PID " + str(getpid()) + " calls execl(\"ossim_helper\", \"exec-target\")");
        execl(g_self, g_self, "exec-target", (char*)nullptr);
        emit("child", "error", err("execl"));
        _exit(127);
    }
    emit("parent", "fork-returned", "child PID " + str(c) + " will replace its program image");
    int st = reap(c, "parent");
    emit("parent", "reaped", "waitpid(" + str(c) + ") -> child " + statusText(st));
    return 0;
}

static int execTarget() {
    emit("child", "exec-image", "new program image (ossim_helper exec-target) is running with the SAME PID " + str(getpid()));
    emit("child", "exit", "the new image calls exit(3)");
    return 3;
}

static int demoWait() {
    const int delays[3] = {300, 100, 200}, codes[3] = {1, 2, 3};
    pid_t kids[3];
    for (int i = 0; i < 3; ++i) {
        kids[i] = fork();
        if (kids[i] < 0) { emit("parent", "error", err("fork")); break; }
        if (kids[i] == 0) {
            emit("child", "started", "child " + str(i) + " works for " + str(delays[i]) + " ms, then exit(" + str(codes[i]) + ")");
            msleep(delays[i]);
            _exit(codes[i]);
        }
        emit("parent", "forked", "child " + str(i) + " has PID " + str(kids[i]));
    }
    for (int done = 0; done < 3;) {
        int st = 0;
        pid_t r = waitpid(-1, &st, WNOHANG);
        if (r > 0) { ++done; emit("parent", "reaped", "wait() returned PID " + str(r) + ": " + statusText(st)); continue; }
        if (r < 0 && errno == ECHILD) break;
        if (g_stop) { for (pid_t k : kids) if (k > 0) reap(k, "parent", 0); break; }
        usleep(5000);
    }
    emit("parent", "done", "every child was reaped (order = order of termination, decided by the OS)");
    return 0;
}

static int demoSleep() {
    pid_t c = fork();
    if (c < 0) { emit("parent", "error", err("fork")); return 1; }
    if (c == 0) {
        emit("child", "sleep", "sleep(1): the child is put to sleep (blocked) for about 1 second");
        long t = nowMs();
        sleep(1);
        emit("child", "woke-up", "woke up after " + str(nowMs() - t) + " ms of wall-clock time (measured, varies)");
        _exit(0);
    }
    emit("parent", "waiting", "the parent waits with waitpid() (non-busy)");
    int st = reap(c, "parent");
    emit("parent", "reaped", "child " + statusText(st));
    return 0;
}

static char procState(pid_t pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    FILE* f = fopen(path, "r");
    if (!f) return '?';
    int p;
    char comm[256], s = '?';
    if (fscanf(f, "%d %255s %c", &p, comm, &s) != 3) s = '?';
    fclose(f);
    return s;
}

static int demoZombie() {
    pid_t c = fork();
    if (c < 0) { emit("parent", "error", err("fork")); return 1; }
    if (c == 0) {
        emit("child", "exit", "the child exits at once with status 5");
        _exit(5);
    }
    emit("parent", "not-waiting", "the parent does NOT call wait() for 400 ms");
    msleep(400);
    char s = procState(c);
    if (s == 'Z') emit("parent", "zombie-observed", "/proc/" + str(c) + "/stat state = Z: the child is a zombie (exited, not reaped)");
    else emit("parent", "zombie-check", std::string("state of the child = '") + s + "' (/proc not available or not a zombie)");
    int st = reap(c, "parent");
    emit("parent", "reaped", "waitpid() collects the exit status: " + statusText(st));
    emit("parent", "zombie-gone", procState(c) == '?' ? "/proc/" + str(c) + " no longer exists: the zombie is gone"
                                                     : "the entry still exists");
    return 0;
}

static int demoOrphan() {
#ifdef __linux__
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) == 0)
        emit("helper", "subreaper", "this helper is a child subreaper (prctl PR_SET_CHILD_SUBREAPER): orphans of its subtree are reparented to it");
#else
    emit("helper", "note", "no subreaper support: an orphan is reparented to a system process");
#endif
    pid_t mid = fork();
    if (mid < 0) { emit("helper", "error", err("fork")); return 1; }
    if (mid == 0) {
        pid_t c = fork();
        if (c < 0) { emit("parent", "error", err("fork")); _exit(1); }
        if (c == 0) {
            emit("child", "started", "my parent is PID " + str(getppid()));
            msleep(300);
            emit("child", "reparented", "after the parent exited, my parent is PID " + str(getppid()) +
                                            " (the subreaper; without one it would be init or a session subreaper)");
            _exit(9);
        }
        msleep(50);
        emit("parent", "exit", "the parent exits WITHOUT waiting for child " + str(c) + " -> the child becomes an orphan");
        _exit(0);
    }
    int st = reap(mid, "helper");
    emit("helper", "reaped", "parent process " + str(mid) + " " + statusText(st));
    // the orphan is now our child (subreaper): reap it with a bounded wait
    long start = nowMs();
    for (;;) {
        int s2 = 0;
        pid_t r = waitpid(-1, &s2, WNOHANG);
        if (r > 0) { emit("helper", "reaped-orphan", "orphan " + str(r) + " " + statusText(s2) + ", reaped by the subreaper"); break; }
        if (r < 0) { emit("helper", "note", "the orphan was adopted elsewhere (no subreaper): it is reaped by that process"); break; }
        if (g_stop || nowMs() - start > 3000) { emit("helper", "timeout", "orphan not collected in time"); break; }
        usleep(5000);
    }
    return 0;
}

static int demoSignal() {
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, &old);          // no race: SIGUSR1 stays pending until sigsuspend
    pid_t c = fork();
    if (c < 0) { emit("parent", "error", err("fork")); return 1; }
    if (c == 0) {
        struct sigaction sa{};
        sa.sa_handler = onUsr1;                    // async-signal-safe: only sets a flag
        sigaction(SIGUSR1, &sa, nullptr);
        emit("child", "waiting", "SIGUSR1 handler installed; sigsuspend() waits for the signal");
        long start = nowMs();
        while (!g_usr1 && !g_stop && nowMs() - start < 3000) {
            sigset_t wait = old;
            sigdelset(&wait, SIGUSR1);
            sigsuspend(&wait);
        }
        emit("child", g_usr1 ? "signal-received" : "timeout", g_usr1 ? "the handler ran (flag set); the child exits(0)" : "no signal");
        _exit(g_usr1 ? 0 : 1);
    }
    sigprocmask(SIG_SETMASK, &old, nullptr);
    msleep(150);
    emit("parent", "kill", "kill(" + str(c) + ", SIGUSR1): signal sent to OUR child only");
    kill(c, SIGUSR1);
    int st = reap(c, "parent");
    emit("parent", "reaped", "child " + statusText(st));
    return 0;
}

// ------------------------------------------------------------------ IPC

static int demoShm() {
    int id = shmget(IPC_PRIVATE, 64, IPC_CREAT | 0600);
    if (id < 0) { emit("server", "error", err("shmget")); return 1; }
    emit("server", "shmget", "System V segment id " + str(id) + ", 64 bytes, permissions 0600 (private key)");
    int sync[2];
    if (pipe(sync) != 0) { shmctl(id, IPC_RMID, nullptr); emit("server", "error", err("pipe")); return 1; }
    pid_t c = fork();
    if (c < 0) { shmctl(id, IPC_RMID, nullptr); emit("server", "error", err("fork")); return 1; }
    if (c == 0) {
        close(sync[1]);
        char b;
        ssize_t n = read(sync[0], &b, 1);          // handoff: wait until the server has written
        if (n != 1) _exit(2);
        void* p = shmat(id, nullptr, SHM_RDONLY);
        if (p == (void*)-1) { emit("client", "error", err("shmat")); _exit(1); }
        char addr[32];
        snprintf(addr, sizeof addr, "%p", p);
        shmid_ds ds{};
        shmctl(id, IPC_STAT, &ds);
        emit("client", "shmat", std::string("attached read-only at virtual address ") + addr + ", attachments now " + str((long)ds.shm_nattch));
        emit("client", "read", std::string("read \"") + (const char*)p + "\" from the shared segment");
        shmdt(p);
        emit("client", "shmdt", "detached");
        _exit(0);
    }
    close(sync[0]);
    void* p = shmat(id, nullptr, 0);
    if (p == (void*)-1) {
        emit("server", "error", err("shmat"));
        close(sync[1]);
        reap(c, "server");
        shmctl(id, IPC_RMID, nullptr);
        return 1;
    }
    char addr[32];
    snprintf(addr, sizeof addr, "%p", p);
    emit("server", "shmat", std::string("attached read-write at virtual address ") + addr + " (the client may get a different address)");
    std::strcpy((char*)p, "HELLO");
    emit("server", "write", "wrote \"HELLO\" (5 bytes + NUL) at offset 0");
    (void)!write(sync[1], "x", 1);
    close(sync[1]);
    int st = reap(c, "server");
    emit("server", "client-done", "client " + statusText(st));
    shmdt(p);
    emit("server", "shmdt", "detached");
    if (shmctl(id, IPC_RMID, nullptr) == 0) emit("server", "removed", "shmctl(IPC_RMID): segment " + str(id) + " removed");
    else emit("server", "error", err("shmctl(IPC_RMID)"));
    return 0;
}

struct DemoMsg { long mtype; char text[64]; };

static int demoMsg() {
    int q = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (q < 0) { emit("receiver", "error", err("msgget")); return 1; }
    emit("receiver", "msgget", "System V message queue id " + str(q) + " (private key)");
    DemoMsg m{};
    if (msgrcv(q, &m, sizeof m.text, 0, IPC_NOWAIT) < 0)
        emit("receiver", "nonblocking-fail", std::string("msgrcv(IPC_NOWAIT) on the empty queue failed immediately: ") +
                                                 (errno == ENOMSG ? "ENOMSG (not blocked)" : strerror(errno)));
    pid_t c = fork();
    if (c < 0) { msgctl(q, IPC_RMID, nullptr); emit("receiver", "error", err("fork")); return 1; }
    if (c == 0) {
        msleep(300);
        const char* texts[2] = {"first", "second"};
        for (const char* t : texts) {
            DemoMsg s{};
            s.mtype = 1;
            std::strncpy(s.text, t, sizeof s.text - 1);
            if (msgsnd(q, &s, std::strlen(s.text) + 1, 0) == 0) emit("sender", "msgsnd", std::string("sent \"") + t + "\" (type 1)");
            else emit("sender", "error", err("msgsnd"));
        }
        _exit(0);
    }
    for (int i = 0; i < 2 && !g_stop; ++i) {
        emit("receiver", "msgrcv-wait", "blocking msgrcv(): waits until a message arrives");
        ssize_t n = msgrcv(q, &m, sizeof m.text, 0, 0);
        if (n < 0) { emit("receiver", "error", err("msgrcv")); break; }
        emit("receiver", "msgrcv", std::string("received \"") + m.text + "\" (" + str(n) + " bytes)");
    }
    int st = reap(c, "receiver");
    emit("receiver", "sender-done", "sender " + statusText(st));
    if (msgctl(q, IPC_RMID, nullptr) == 0) emit("receiver", "removed", "msgctl(IPC_RMID): queue " + str(q) + " removed");
    return 0;
}

static int demoPipe() {
    int fd[2];
    if (pipe(fd) != 0) { emit("parent", "error", err("pipe")); return 1; }
    emit("parent", "pipe", "pipe() -> read end fd " + str(fd[0]) + ", write end fd " + str(fd[1]));
    pid_t c = fork();
    if (c < 0) { close(fd[0]); close(fd[1]); emit("parent", "error", err("fork")); return 1; }
    if (c == 0) {
        close(fd[0]);
        const char* parts[2] = {"hello ", "through a pipe"};
        for (const char* t : parts) {
            ssize_t n = write(fd[1], t, std::strlen(t));
            emit("child", "write", "write() " + str(n) + " bytes: \"" + t + "\"");
            msleep(100);
        }
        close(fd[1]);
        emit("child", "close", "closed the write end");
        _exit(0);
    }
    close(fd[1]);
    emit("parent", "close", "parent closed its copy of the write end (needed to ever see EOF)");
    char buf[9];
    std::string all;
    for (;;) {
        ssize_t n = read(fd[0], buf, 8);             // small reads: no message boundaries in a byte stream
        if (n < 0 && errno == EINTR) { if (g_stop) break; continue; }
        if (n < 0) { emit("parent", "error", err("read")); break; }
        if (n == 0) { emit("parent", "eof", "read() returned 0: EOF (all write ends closed, buffer empty)"); break; }
        buf[n] = 0;
        all += buf;
        emit("parent", "read", "read() " + str(n) + " bytes: \"" + buf + "\"");
    }
    close(fd[0]);
    int st = reap(c, "parent");
    emit("parent", "done", "received \"" + all + "\" (" + str((long)all.size()) + " bytes); child " + statusText(st));
    return 0;
}

static int fifoWriter(const char* path) {
    int fd = open(path, O_WRONLY);                   // blocks until the reader opens the FIFO
    if (fd < 0) { emit("writer", "error", err("open")); return 1; }
    emit("writer", "open", std::string("opened the FIFO for writing: ") + path);
    const char* t = "message over a named pipe";
    ssize_t n = write(fd, t, std::strlen(t));
    emit("writer", "write", "write() " + str(n) + " bytes");
    close(fd);
    emit("writer", "close", "closed");
    return 0;
}

static int demoFifo() {
    char dir[] = "/tmp/ossim-fifo-XXXXXX";
    if (!mkdtemp(dir)) { emit("reader", "error", err("mkdtemp")); return 1; }
    std::string path = std::string(dir) + "/demo.fifo";
    if (mkfifo(path.c_str(), 0600) != 0) { emit("reader", "error", err("mkfifo")); rmdir(dir); return 1; }
    emit("reader", "mkfifo", "created FIFO " + path + " (owned temporary directory)");
    pid_t c = fork();
    if (c < 0) { unlink(path.c_str()); rmdir(dir); emit("reader", "error", err("fork")); return 1; }
    if (c == 0) {
        execl(g_self, g_self, "fifo-writer", path.c_str(), (char*)nullptr);   // separately launched endpoint
        _exit(127);
    }
    int fd = open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        emit("reader", "open", "opened the FIFO for reading (the writer is another program)");
        char buf[64];
        std::string all;
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf - 1)) > 0) { buf[n] = 0; all += buf; }
        emit("reader", "read", "read \"" + all + "\"; then EOF");
        close(fd);
    } else {
        emit("reader", "error", err("open"));
    }
    int st = reap(c, "reader");
    emit("reader", "writer-done", "writer " + statusText(st));
    unlink(path.c_str());
    rmdir(dir);
    emit("reader", "removed", "FIFO and temporary directory removed");
    return 0;
}

// ------------------------------------------------------------------ threads

struct ThreadArg { int index; long input; };
struct ThreadResult { int index; long input; long sum; long tid; };

static void* threadMain(void* a) {
    ThreadArg* arg = static_cast<ThreadArg*>(a);   // owned by main(), alive until join
    long tid = 0;
#ifdef __linux__
    tid = (long)syscall(SYS_gettid);
#endif
    emit("thread", "started", "thread " + str(arg->index) + " received argument " + str(arg->input) +
                                  " (Linux TID " + str(tid) + ")");
    long sum = 0;
    for (long i = 1; i <= arg->input * 1000 && !g_stop; ++i) sum += i;
    ThreadResult* r = new ThreadResult{arg->index, arg->input, sum, tid};   // ownership -> joiner
    emit("thread", "finished", "thread " + str(arg->index) + " computed sum(1.." + str(arg->input * 1000) + ") = " + str(sum) +
                                   (arg->index % 2 ? " and calls pthread_exit()" : " and returns"));
    if (arg->index % 2) pthread_exit(r);
    return r;
}

static int demoThreads(int n, long base) {
    if (n < 1 || n > 16 || base < 1 || base > 1000) { emit("main", "error", "threads 1..16, base 1..1000"); return 2; }
    std::vector<pthread_t> t(n);
    std::vector<ThreadArg> args(n);                  // stable storage for the whole run
    int created = 0;
    for (int i = 0; i < n; ++i) {
        args[i] = ThreadArg{i, base + i};
        if (pthread_create(&t[i], nullptr, threadMain, &args[i]) != 0) { emit("main", "error", "pthread_create failed"); break; }
        emit("main", "created", "pthread_create(thread " + str(i) + ", argument " + str(args[i].input) + ")");
        ++created;
    }
    bool allOk = true;
    for (int i = 0; i < created; ++i) {
        void* res = nullptr;
        pthread_join(t[i], &res);
        ThreadResult* r = static_cast<ThreadResult*>(res);
        long m = r->input * 1000;
        bool ok = g_stop || r->sum == m * (m + 1) / 2;
        allOk = allOk && ok;
        emit("main", "joined", "pthread_join(thread " + str(i) + ") -> result " + str(r->sum) + (ok ? " (correct)" : " (WRONG)"));
        delete r;
    }
    emit("main", "done", allOk ? "all threads joined, every result correct" : "some result was wrong");
    return allOk ? 0 : 1;
}


// ------------------------------------------------------------------ Phase 3: file syscalls

// Real open/write/lseek/read/fstat/link/readdir/unlink/close/rmdir, only inside a private
// mkdtemp() directory that is removed on every path.
static int demoFs() {
    char dir[] = "/tmp/ossim_fs_XXXXXX";
    if (!mkdtemp(dir)) { emit("main", "error", err("mkdtemp")); return 1; }
    std::string a = std::string(dir) + "/notes.txt", b = std::string(dir) + "/second-name.txt";
    auto cleanup = [&]() { unlink(a.c_str()); unlink(b.c_str()); rmdir(dir); };
    emit("main", "mkdtemp", std::string("private temporary directory ") + dir);
    int fd = open(a.c_str(), O_CREAT | O_EXCL | O_RDWR, 0640);
    if (fd < 0) { emit("main", "error", err("open")); cleanup(); return 1; }
    emit("main", "open", "open(\"notes.txt\", O_CREAT|O_EXCL|O_RDWR, 0640) = fd " + str(fd));
    const char* text = "hello from a real file\n";
    ssize_t w = write(fd, text, strlen(text));
    emit("main", "write", "write(fd, 23 bytes) = " + str(w) + "; offset is now " + str((long)lseek(fd, 0, SEEK_CUR)));
    lseek(fd, 0, SEEK_SET);
    char buf[16] = {0};
    ssize_t r = read(fd, buf, 5);
    emit("main", "read", "lseek(fd, 0) then read(fd, 5) = " + str(r) + " bytes: \"" + std::string(buf, r > 0 ? r : 0) + "\"");
    struct stat st{};
    fstat(fd, &st);
    char mode[16];
    snprintf(mode, sizeof mode, "%03o", (unsigned)(st.st_mode & 0777));
    emit("main", "fstat", "inode " + str((long)st.st_ino) + ", size " + str((long)st.st_size) + ", mode " + mode + ", link count " + str((long)st.st_nlink));
    if (link(a.c_str(), b.c_str()) == 0) {
        fstat(fd, &st);
        emit("main", "link", "link(notes.txt, second-name.txt): same inode " + str((long)st.st_ino) + ", link count " + str((long)st.st_nlink));
    } else emit("main", "link-failed", err("link") + " (the temporary filesystem may not support hard links)");
    if (DIR* d = opendir(dir)) {
        std::string names;
        while (dirent* e = readdir(d)) if (std::strcmp(e->d_name, ".") && std::strcmp(e->d_name, "..")) names += std::string(names.empty() ? "" : ", ") + e->d_name;
        closedir(d);
        emit("main", "readdir", "directory entries: " + names);
    }
    unlink(a.c_str());
    unlink(b.c_str());
    fstat(fd, &st);
    emit("main", "unlink", "both names unlinked while fd " + str(fd) + " is open: link count " + str((long)st.st_nlink) + ", data still reachable");
    lseek(fd, 0, SEEK_SET);
    r = read(fd, buf, 5);
    emit("main", "read", "read(fd, 5) after unlink = " + str(r) + " bytes: \"" + std::string(buf, r > 0 ? r : 0) + "\"");
    close(fd);
    emit("main", "close", "close(fd): the last reference is gone, the kernel may now free the inode and its blocks");
    cleanup();
    emit("main", "removed", "temporary directory removed");
    return 0;
}

// ------------------------------------------------------------------ Phase 3: owned monitor worker

static volatile sig_atomic_t g_cont = 0;
static void onCont(int) { g_cont = 1; }

// A harmless bounded worker for the System Monitor: uses `busy` percent of one core and touches
// `mb` MiB, for at most `seconds`. SIGTERM = cooperative stop; SIGSTOP / SIGCONT come from the GUI.
static int demoWorker(int seconds, int mb, int busy) {
    if (seconds < 1 || seconds > 120 || mb < 0 || mb > 256 || busy < 0 || busy > 100) { emit("worker", "error", "seconds 1..120, MiB 0..256, busy 0..100"); return 2; }
    struct sigaction sc{};
    sc.sa_handler = onCont;
    sigaction(SIGCONT, &sc, nullptr);
    std::vector<char> mem((size_t)mb * 1024 * 1024);
    for (size_t i = 0; i < mem.size(); i += 4096) mem[i] = (char)i;        // touch every page -> resident
    emit("worker", "started", "busy " + str(busy) + "% of one core, " + str(mb) + " MiB touched, at most " + str(seconds) + " s");
    long start = nowMs(), lastTick = start;
    volatile unsigned long spin = 0;
    while (!g_stop && nowMs() - start < seconds * 1000L) {
        long slice = nowMs();
        while (nowMs() - slice < busy) ++spin;                             // busy part of 100 ms
        if (busy < 100) usleep((useconds_t)(100 - busy) * 1000);
        if (g_cont) { g_cont = 0; emit("worker", "continued", "received SIGCONT: running again"); }
        if (nowMs() - lastTick >= 1000) { lastTick = nowMs(); emit("worker", "tick", "alive for " + str((nowMs() - start) / 1000) + " s"); }
    }
    emit("worker", g_stop ? "stopped" : "finished", g_stop ? "SIGTERM received: cooperative stop, memory released" : "time limit reached");
    return 0;
}

// ------------------------------------------------------------------ Phase 3: process groups / job control

static int reapUntraced(pid_t pid, int flags, int timeoutMs) {
    int st = 0;
    long start = nowMs();
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG | flags);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        if (g_stop || nowMs() - start > timeoutMs) return -2;
        usleep(5000);
    }
}

// Background job in its own process group stopped / continued / terminated with group signals,
// and a two-stage pipeline job (one process group, connected by pipe()). Only own children.
static int demoJobs() {
    emit("shell", "session", "helper PID " + str(getpid()) + ", process group " + str((long)getpgrp()) + ", session " + str((long)getsid(0)) +
                             " (no controlling terminal is used: tcsetpgrp() is not called)");
    pid_t w = fork();
    if (w < 0) { emit("shell", "error", err("fork")); return 1; }
    if (w == 0) {
        setpgid(0, 0);
        signal(SIGTERM, SIG_DFL);
        for (int i = 0; i < 50; ++i) { usleep(100000); if (i % 3 == 0) emit("job1", "working", "background job running (tick " + str(i) + ")"); }
        _exit(0);
    }
    setpgid(w, w);
    emit("shell", "job-started", "[1] " + str(w) + ": background job in NEW process group " + str(w));
    msleep(400);
    kill(-w, SIGSTOP);                                          // the whole group (our child only)
    int st = reapUntraced(w, WUNTRACED, 3000);
    emit("shell", "job-stopped", "kill(-" + str(w) + ", SIGSTOP) -> waitpid(WUNTRACED): " +
                                 (st >= 0 && WIFSTOPPED(st) ? "stopped by signal " + str(WSTOPSIG(st)) : std::string("no stop report")));
    msleep(300);
    kill(-w, SIGCONT);
    st = reapUntraced(w, WCONTINUED, 3000);
    emit("shell", "job-continued", "kill(-" + str(w) + ", SIGCONT) -> waitpid(WCONTINUED): " + (st >= 0 && WIFCONTINUED(st) ? "continued" : "no continue report"));
    // pipeline job: producer | consumer in one process group
    int p[2];
    if (pipe(p) != 0) { emit("shell", "error", err("pipe")); kill(-w, SIGKILL); reap(w, "shell"); return 1; }
    pid_t a = fork();
    if (a == 0) {
        setpgid(0, 0);
        close(p[0]);
        for (int i = 1; i <= 4; ++i) {
            std::string line = "item " + str(i) + "\n";
            (void)!write(p[1], line.c_str(), line.size());
            emit("producer", "write", "wrote '" + line.substr(0, line.size() - 1) + "' into the pipe");
            usleep(80000);
        }
        close(p[1]);
        _exit(0);
    }
    pid_t b = a > 0 ? fork() : -1;
    if (b == 0) {
        setpgid(0, a);
        close(p[1]);
        char buf[64];
        std::string acc;
        ssize_t n;
        while ((n = read(p[0], buf, sizeof buf)) > 0) {
            acc.append(buf, n);
            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                std::string l = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                for (char& c : l) c = (char)toupper((unsigned char)c);
                emit("consumer", "read", "read and transformed -> '" + l + "'");
            }
        }
        _exit(0);
    }
    close(p[0]);
    close(p[1]);
    if (a > 0) setpgid(a, a);
    if (b > 0) setpgid(b, a);
    emit("shell", "pipeline", "[2] producer " + str(a) + " | consumer " + str(b) + " share process group " + str(a));
    if (a > 0) emit("shell", "reaped", "producer " + statusText(reap(a, "shell")));
    if (b > 0) emit("shell", "reaped", "consumer " + statusText(reap(b, "shell")));
    kill(-w, SIGTERM);
    emit("shell", "job-terminated", "[1] kill(-" + str(w) + ", SIGTERM) -> " + statusText(reap(w, "shell")));
    emit("shell", "done", "all jobs reaped; no signal was sent outside the helper's own children");
    return 0;
}

int main(int argc, char** argv) {
    g_t0 = nowMs();
#ifdef __linux__
    static char selfPath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", selfPath, sizeof selfPath - 1);
    if (len > 0) { selfPath[len] = 0; g_self = selfPath; } else g_self = argv[0];
#else
    g_self = argv[0];
#endif
    std::string cmd = argc > 1 ? argv[1] : "list";
    if (cmd == "exec-target") return execTarget();
    if (cmd == "fifo-writer") return argc > 2 ? fifoWriter(argv[2]) : 2;
    setpgid(0, 0);                                   // own process group: Stop signals only our tree
    struct sigaction sa{};
    sa.sa_handler = onTerm;                          // no SA_RESTART: blocking calls return EINTR
    sigaction(SIGTERM, &sa, nullptr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (cmd == "fork") return demoFork();
    if (cmd == "exec") return demoExec();
    if (cmd == "wait") return demoWait();
    if (cmd == "sleep") return demoSleep();
    if (cmd == "zombie") return demoZombie();
    if (cmd == "orphan") return demoOrphan();
    if (cmd == "signal") return demoSignal();
    if (cmd == "shm") return demoShm();
    if (cmd == "msg") return demoMsg();
    if (cmd == "pipe") return demoPipe();
    if (cmd == "fifo") return demoFifo();
    if (cmd == "threads") return demoThreads(argc > 2 ? std::atoi(argv[2]) : 4, argc > 3 ? std::atol(argv[3]) : 10);
    if (cmd == "fsdemo") return demoFs();
    if (cmd == "worker") return demoWorker(argc > 2 ? std::atoi(argv[2]) : 20, argc > 3 ? std::atoi(argv[3]) : 16, argc > 4 ? std::atoi(argv[4]) : 30);
    if (cmd == "jobs") return demoJobs();
    std::printf("usage: ossim_helper fork|exec|wait|sleep|zombie|orphan|signal|shm|msg|pipe|fifo|threads <n> <base>|fsdemo|worker <s> <MiB> <busy%%>|jobs\n");
    return cmd == "list" ? 0 : 2;
}
