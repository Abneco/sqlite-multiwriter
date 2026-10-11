// A commit that is told that it failed must not become visible. The append of one commit fails while another already has its place behind it (the log is failed: nothing can be cut in
// place), and an fsync that was already running covers a commit that is parked behind it: that fsync makes the commit durable and visible when it is done, so the commit must be
// acknowledged. Build multiwriter_log.c with -Dfsync=inject_fsync to hold that fsync.
#include "mw_test.h"
#include "multiwriter_internal.h"
#include <sys/wait.h>
#include <unistd.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int armed, entered, released;
int inject_fsync (int fd) {
    pthread_mutex_lock(&mu);
    if (armed) { armed = 0; entered = 1; pthread_cond_broadcast(&cv); while (!released) pthread_cond_wait(&cv, &mu); }
    pthread_mutex_unlock(&mu);
    return fsync(fd);
}

typedef struct { sqlite3 *db; const char *sql; int rc; } job;
static void *run (void *p) { job *j = p; j->rc = mw_exec(j->db, j->sql); return NULL; }
#define STEP(s) do { if (getenv("MW_TEST_TRACE")) fprintf(stderr, "step: %s\n", s); } while (0)
static void wait_epoch (mw_db *db, uint64_t e) { while (atomic_load(&db->next_epoch) < e) usleep(200); }

int main (void) {
    char path[256], uri[512]; mw_tmpdb(path, sizeof path, "holesync");
    snprintf(uri, sizeof uri, "file:%s?mw=1&mw_compact_ms=0&mw_log_max_mb=0", path);
    pid_t child = fork();
    if (child == 0) {
        alarm(30);
        setenv("MW_FAULT_DELAY_US", "1000000", 1);
        sqlite3 *c[4];
        for (int i = 0; i < 4; i++) if (sqlite3_open_v2(uri, &c[i], SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL)) _exit(2);
        if (mw_exec(c[0], "CREATE TABLE tx(v); CREATE TABLE tw(v); CREATE TABLE tf(v); CREATE TABLE ts(v); INSERT INTO tx VALUES(0); INSERT INTO tw VALUES(0); INSERT INTO tf VALUES(0); INSERT INTO ts VALUES(0)")) _exit(3);
        mw_lane *lane = NULL; if (sqlite3_file_control(c[0], "main", MW_FCNTL_LANE_PTR, &lane) != SQLITE_OK || !lane) _exit(4);
        mw_db *db = lane->db;
        uint64_t base = atomic_load(&db->next_epoch);
        job j[4] = { { c[0], "UPDATE tx SET v=1", -1 }, { c[1], "UPDATE tw SET v=1", -1 }, { c[2], "UPDATE tf SET v=1", -1 }, { c[3], "UPDATE ts SET v=1", -1 } };
        pthread_t t[4];
        // 1. x has its place and is held before its bytes; w is behind it. Then x copies, and the two are written and one fsync starts for both: it is held.
        armed = 1; mw_fault_arm(MW_FAULT_APPEND_STALL, 1);
        STEP("x"); pthread_create(&t[0], NULL, run, &j[0]); wait_epoch(db, base + 1); usleep(100000);
        STEP("w"); pthread_create(&t[1], NULL, run, &j[1]); wait_epoch(db, base + 2);
        STEP("wait fsync"); pthread_mutex_lock(&mu); while (!entered) pthread_cond_wait(&cv, &mu); pthread_mutex_unlock(&mu);
        // 2. f has its place and is held, s takes the place behind it; then the append of f fails: s is behind the hole, the log is failed.
        mw_fault_arm(MW_FAULT_APPEND_STALL, 1);
        STEP("f"); pthread_create(&t[2], NULL, run, &j[2]); wait_epoch(db, base + 3); usleep(100000);
        STEP("s"); pthread_create(&t[3], NULL, run, &j[3]); wait_epoch(db, base + 4); usleep(100000);       // (s appends at once: f is the first append that finds the fault)
        mw_fault_arm(MW_FAULT_LOG_WRITE_ERR, 1);
        STEP("wait failed");
        while (!atomic_load(&db->failed)) usleep(200);
        STEP("failed");
        usleep(100000);                                                    // (the commit that waits for the fsync has seen it)
        pthread_mutex_lock(&mu); released = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu);
        STEP("join"); for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
        mw_fault_disarm_all();
        long long v[4] = { mw_scalar(c[0], "SELECT v FROM tx"), mw_scalar(c[0], "SELECT v FROM tw"), mw_scalar(c[0], "SELECT v FROM tf"), mw_scalar(c[0], "SELECT v FROM ts") };
        const char *nm[4] = { "x", "w", "f", "s" };
        int code = 0;
        for (int i = 0; i < 4; i++) {
            fprintf(stderr, "%s: commit=%d, visible=%lld\n", nm[i], j[i].rc, v[i]);
            if ((j[i].rc == SQLITE_OK) != (v[i] == 1)) code |= 1;          // told that it failed and visible, or acknowledged and not there
        }
        if (j[0].rc || j[1].rc) code |= 2;                                  // the commits that the running fsync covers are acknowledged
        if (!j[2].rc || !j[3].rc) code |= 4;                                // the one that failed and the one behind the hole are refused
        _exit(code);
    }
    int st = 0; CHECK(waitpid(child, &st, 0) == child);
    printf("child: %s %d\n", WIFEXITED(st) ? "exit" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    mw_rmdb(path);
    MW_DONE();
}
