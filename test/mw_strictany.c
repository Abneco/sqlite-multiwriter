#include "mw_test.h"
#include "multiwriter.h"

int main(void) {
    char path[256], uri[512];
    mw_tmpdb(path, sizeof path, "any");
    snprintf(uri, sizeof uri, "file:%s?mw=1&mw_rebase=1&mw_rebase_backoff=0%s",
             path, getenv("MW_TEST_MP") ? "&mw_mp=1" : "");
    sqlite3 *a = NULL, *b = NULL;
    CHECK_RC(sqlite3_open_v2(uri, &a, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
    CHECK_RC(sqlite3_open_v2(uri, &b, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
    CHECK_RC(mw_exec(a, "CREATE TABLE t(id INTEGER PRIMARY KEY,v ANY,n INTEGER) STRICT; INSERT INTO t VALUES(1,1,0)"), SQLITE_OK);
    CHECK_RC(mw_exec(a, "BEGIN; UPDATE t SET n=length(typeof(v)) WHERE id=1"), SQLITE_OK);
    CHECK_RC(mw_exec(b, "UPDATE t SET v=1.0,n=n*10 WHERE id=1"), SQLITE_OK);
    /* Serial A,B gives n=70; serial B,A gives n=4. n=7 fits neither. */
    int rc = mw_exec(a, "COMMIT");
    printf("strict ANY: commit=%d, real=%lld, n=%lld\n", rc,
           (long long)mw_scalar(b, "SELECT typeof(v)='real' FROM t"),
           (long long)mw_scalar(b, "SELECT n FROM t"));
    CHECK((rc & 255) == SQLITE_BUSY);
    if (rc != SQLITE_OK) mw_exec(a, "ROLLBACK");
    sqlite3_close(a); sqlite3_close(b); mw_rmdb(path);
    MW_DONE();
}
