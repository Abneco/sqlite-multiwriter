// A statement's own conflict policy (OR ..., ON CONFLICT ...) is not in the row images: such a transaction is refused (BUSY), never replayed with the policy of the schema.
// IGNORE and REPLACE are refused too: that is the conservative answer, not a requirement of correctness there.
#include "mw_test.h"
#include "multiwriter.h"

int main(void) {
    const char *policies[] = {"ABORT", "FAIL", "ROLLBACK", "IGNORE", "REPLACE"};
    for (int wr = 0; wr < 2; wr++) for (int update = 0; update < 2; update++)
    for (unsigned k = 0; k < sizeof policies / sizeof *policies; k++) {
        char path[256], uri[512], sql[512];
        mw_tmpdb(path, sizeof path, "policy");
        snprintf(uri, sizeof uri, "file:%s?mw=1&mw_rebase=1&mw_rebase_backoff=0%s",
                 path, getenv("MW_TEST_MP") ? "&mw_mp=1" : "");
        sqlite3 *a = NULL, *b = NULL;
        CHECK_RC(sqlite3_open_v2(uri, &a, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
        CHECK_RC(sqlite3_open_v2(uri, &b, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
        snprintf(sql, sizeof sql, "CREATE TABLE t(id INTEGER PRIMARY KEY,u TEXT UNIQUE ON CONFLICT REPLACE)%s; INSERT INTO t VALUES(1,'seed')", wr ? " WITHOUT ROWID" : "");
        CHECK_RC(mw_exec(a, sql), SQLITE_OK);
        CHECK_RC(mw_exec(a, "BEGIN"), SQLITE_OK);
        snprintf(sql, sizeof sql, update
                 ? "/* request */ UPDATE /* policy */ OR %s t SET u='same' WHERE id=1"
                 : "/* request */ INSERT /* policy */ OR %s INTO t VALUES(10,'same')", policies[k]);
        CHECK_RC(mw_exec(a, sql), SQLITE_OK);
        CHECK_RC(mw_exec(b, "INSERT OR ABORT INTO t VALUES(20,'same')"), SQLITE_OK);
        int rc = mw_exec(a, "COMMIT");
        printf("%s OR %s, WITHOUT ROWID=%d: commit=%d\n", update ? "UPDATE" : "INSERT", policies[k], wr, rc);
        CHECK((rc & 255) == SQLITE_BUSY);
        CHECK(mw_scalar(b, "SELECT count(*) FROM t WHERE id=20 AND u='same'") == 1);
        if (!sqlite3_get_autocommit(a)) CHECK_RC(mw_exec(a, "ROLLBACK"), SQLITE_OK);
        CHECK(mw_scalar(b, "SELECT count(*) FROM t WHERE id=1 AND u='seed'") == 1);
        sqlite3_close(a); sqlite3_close(b); mw_rmdb(path);
    }
    // The UPSERT clause is a policy of the statement too: a replay would give the schema's (REPLACE) and delete the row that the other writer committed.
    const char *upserts[] = {
        "INSERT INTO t VALUES(10,'same') ON CONFLICT(u) DO NOTHING",
        "INSERT INTO t VALUES(10,'same') ON CONFLICT DO NOTHING",
        "INSERT INTO t VALUES(10,'same') ON CONFLICT(u) DO UPDATE SET id=excluded.id",
        "/* request */ INSERT /* upsert */ INTO t VALUES(10,'same') /* c */ ON /* c */ CONFLICT(u) DO NOTHING",
    };
    for (int wr = 0; wr < 2; wr++)
    for (unsigned k = 0; k < sizeof upserts / sizeof *upserts; k++) {
        char path[256], uri[512], sql[512];
        mw_tmpdb(path, sizeof path, "upsert");
        snprintf(uri, sizeof uri, "file:%s?mw=1&mw_rebase=1&mw_rebase_backoff=0%s",
                 path, getenv("MW_TEST_MP") ? "&mw_mp=1" : "");
        sqlite3 *a = NULL, *b = NULL;
        CHECK_RC(sqlite3_open_v2(uri, &a, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
        CHECK_RC(sqlite3_open_v2(uri, &b, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
        snprintf(sql, sizeof sql, "CREATE TABLE t(id INTEGER PRIMARY KEY,u TEXT UNIQUE ON CONFLICT REPLACE)%s; INSERT INTO t VALUES(1,'seed')", wr ? " WITHOUT ROWID" : "");
        CHECK_RC(mw_exec(a, sql), SQLITE_OK);
        CHECK_RC(mw_exec(a, "BEGIN"), SQLITE_OK);
        CHECK_RC(mw_exec(a, upserts[k]), SQLITE_OK);
        CHECK_RC(mw_exec(b, "INSERT INTO t VALUES(20,'same')"), SQLITE_OK);
        int rc = mw_exec(a, "COMMIT");
        printf("UPSERT #%u, WITHOUT ROWID=%d: commit=%d\n", k, wr, rc);
        CHECK((rc & 255) == SQLITE_BUSY);
        CHECK(mw_scalar(b, "SELECT count(*) FROM t WHERE id=20 AND u='same'") == 1);
        if (!sqlite3_get_autocommit(a)) CHECK_RC(mw_exec(a, "ROLLBACK"), SQLITE_OK);
        CHECK(mw_scalar(b, "SELECT count(*) FROM t") == 2);
        sqlite3_close(a); sqlite3_close(b); mw_rmdb(path);
    }
    MW_DONE();
}
