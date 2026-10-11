// MW_PORTABLE_TEST: ATTACH must honor synchronous even without the engine's trace hook.
#include "mw_test.h"
#include "multiwriter_internal.h"

static void check_full(sqlite3 *db, mw_lane *lane) {
    CHECK(mw_scalar(db, "PRAGMA aux.synchronous") == 2);
    CHECK(lane->sync_level == 2);
}

int main(void) {
    for (int main_engine = 0; main_engine < 2; main_engine++)
    for (int mp = 0; mp < 2; mp++)
    for (int rebase = 0; rebase < 2; rebase++)
    for (int replace_trace = 0; replace_trace < 2; replace_trace++) {
        char mainpath[256], auxpath[256], uri[512];
        mw_tmpdb(mainpath, sizeof mainpath, "attach_main");
        mw_tmpdb(auxpath, sizeof auxpath, "attach_aux");
        snprintf(uri, sizeof uri, "file:%s?mw=%d&mw_rebase=%d&mw_log_max_mb=0", mainpath, main_engine, rebase);
        sqlite3 *db = NULL;
        CHECK_RC(sqlite3_open_v2(uri, &db, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
        if (replace_trace) CHECK_RC(sqlite3_trace_v2(db, 0, NULL, NULL), SQLITE_OK);
        // Reuse the schema name after DETACH: the new lane must be bound too.
        for (int round = 0; round < 2; round++) {
            snprintf(uri, sizeof uri, "file:%s?mw=1&mw_mp=%d&mw_rebase=%d&mw_compact_ms=0&mw_log_max_mb=0", auxpath, mp, rebase);
            char *sql = sqlite3_mprintf("ATTACH %Q AS aux", uri);
            CHECK_RC(mw_exec(db, sql), SQLITE_OK); sqlite3_free(sql);
            CHECK_RC(mw_exec(db, "CREATE TABLE IF NOT EXISTS aux.t(v); PRAGMA aux.synchronous=FULL"), SQLITE_OK);
            mw_lane *lane = NULL;
            CHECK_RC(sqlite3_file_control(db, "aux", MW_FCNTL_LANE_PTR, &lane), SQLITE_OK);
            CHECK(lane != NULL);
            if (!lane) { sqlite3_close(db); mw_rmdb(mainpath); mw_rmdb(auxpath); MW_DONE(); }
            CHECK(lane->rd_db == db);
            CHECK_RC(mw_exec(db, "BEGIN"), SQLITE_OK);
            // SQLite rejects this even before a snapshot has been acquired.
            CHECK_RC(mw_exec(db, "PRAGMA aux.synchronous=OFF"), SQLITE_ERROR);
            check_full(db, lane);
            CHECK_RC(mw_exec(db, "INSERT INTO aux.t VALUES(1)"), SQLITE_OK);
            CHECK_RC(mw_exec(db, "PRAGMA aux.synchronous=OFF"), SQLITE_ERROR);
            check_full(db, lane);
            mw_db_stats before, after;
            CHECK_RC(sqlite3_file_control(db, "aux", MW_FCNTL_DBSTATS, &before), SQLITE_OK);
            CHECK_RC(mw_exec(db, "COMMIT"), SQLITE_OK);
            CHECK_RC(sqlite3_file_control(db, "aux", MW_FCNTL_DBSTATS, &after), SQLITE_OK);
            printf("ATTACH main=%d mp=%d rebase=%d trace_removed=%d round=%d: FULL syncs=%llu\n",
                   main_engine, mp, rebase, replace_trace, round,
                   (unsigned long long)(after.log_syncs - before.log_syncs));
            CHECK(after.log_syncs > before.log_syncs);
            CHECK(mw_scalar(db, "SELECT count(*) FROM aux.t") == round + 1);
            CHECK_RC(mw_exec(db, "DETACH aux"), SQLITE_OK);
        }
        CHECK_RC(sqlite3_close(db), SQLITE_OK);
        mw_rmdb(mainpath); mw_rmdb(auxpath);
    }
    MW_DONE();
}
