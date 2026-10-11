// Compile multiwriter_rowdiff.c with -Dmalloc=inject_malloc
// -Dcalloc=inject_calloc -Drealloc=inject_realloc.
#include "mw_test.h"
#include <sys/wait.h>
#include <signal.h>

static int remaining, fired;
#define MAX_NTH 2000          // (the sweep ends at the first n-th allocation that does not exist; this only bounds a runaway)
static int fail_alloc(void) {
    if (remaining > 0 && --remaining == 0) { fired = 1; return 1; }
    return 0;
}
void *inject_malloc(size_t n) { return fail_alloc() ? NULL : malloc(n); }
void *inject_calloc(size_t n, size_t s) { return fail_alloc() ? NULL : calloc(n, s); }
void *inject_realloc(void *p, size_t n) { return fail_alloc() ? NULL : realloc(p, n); }

int main(void) {
    for (int layout = 0; layout < 3; layout++) {
        int nth, exhausted = 0;
        for (nth = 1; nth <= MAX_NTH && !exhausted; nth++) {
            char path[256]; mw_tmpdb(path, sizeof path, "rowdiff_oom");
            pid_t child = fork(); CHECK(child >= 0);
            if (child == 0) {
                alarm(15);
                char uri[512]; snprintf(uri, sizeof uri,
                    "file:%s?mw=1&mw_rebase=1&mw_rebase_backoff=0&mw_compact_ms=0&mw_log_max_mb=0", path);
                sqlite3 *a = NULL, *b = NULL;
                if (sqlite3_open_v2(uri, &a, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL)) _exit(2);
                if (sqlite3_open_v2(uri, &b, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL)) _exit(3);
                char seed[512];
                snprintf(seed, sizeof seed, "CREATE TABLE t(id INTEGER PRIMARY KEY,v)%s; INSERT INTO t VALUES(1,zeroblob(%d)),(2,0)", layout == 2 ? " WITHOUT ROWID" : "", layout == 1 ? 100 : 6000);
                if (mw_exec(a, seed)) _exit(4);
                if (layout && mw_exec(a, "WITH RECURSIVE n(i) AS (VALUES(3) UNION ALL SELECT i+1 FROM n WHERE i<300) INSERT INTO t SELECT i,zeroblob(100) FROM n")) _exit(4);
                if (mw_exec(a, "BEGIN; UPDATE t SET v=zeroblob(6001) WHERE id=1")) _exit(5);
                if (mw_exec(b, "UPDATE t SET v=1 WHERE id=2")) _exit(6);
                remaining = nth;
                int rc = mw_exec(a, "COMMIT");
                remaining = 0;
                // Failure must refuse the transaction cleanly; it must not lose
                // the other writer's change or acknowledge a partial replay.
                if (rc != SQLITE_OK) mw_exec(a, "ROLLBACK");
                if (mw_scalar(b, "SELECT v FROM t WHERE id=2") != 1) _exit(7);
                if (mw_scalar(b, "SELECT length(v) FROM t WHERE id=1") != (rc == SQLITE_OK ? 6001 : layout == 1 ? 100 : 6000)) _exit(8);
                sqlite3_close(a); sqlite3_close(b);
                _exit(fired ? 0 : 20);                 // (20: the commit made fewer than nth allocations: nothing was injected)
            }
            int status = 0; CHECK(waitpid(child, &status, 0) == child);
            printf("rowdiff layout %d allocation #%d: exit=%d signal=%d\n", layout, nth,
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                   WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 20) { exhausted = 1; CHECK(nth > 1); }       // (n-th allocation not reached: every earlier one was failed in turn)
            else CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            mw_rmdb(path);
        }
        CHECK(exhausted);
        printf("rowdiff layout %d: %d allocations of the commit, each failed in turn\n", layout, nth - 2);
    }
    MW_DONE();
}
