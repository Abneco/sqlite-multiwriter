#include "mw_test.h"
#include "multiwriter_internal.h"
int main(void) {
    char path[256], uri[512]; mw_tmpdb(path, sizeof path, "sync");
    snprintf(uri, sizeof uri, "file:%s?mw=1&mw_log_max_mb=0", path);
    sqlite3 *db = NULL;
    CHECK_RC(sqlite3_open_v2(uri, &db, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI, NULL), SQLITE_OK);
    mw_lane *probe=NULL; CHECK_RC(sqlite3_file_control(db,"main",MW_FCNTL_LANE_PTR,&probe),SQLITE_OK); CHECK(probe);
    const char *values[]={"OFF","no","false","ON","yes","true","normal","FULL","extra","unknown","4","7","8","255","-1","2147483648","0xFFFFFFFF","0x2","002"};
    for(unsigned i=0;i<sizeof values/sizeof *values;i++) {
        char q[80];snprintf(q,sizeof q,"PRAGMA synchronous=%s",values[i]);
        CHECK_RC(mw_exec(db,q),SQLITE_OK);
        printf("%s: sqlite=%lld lane=%d\n",values[i],(long long)mw_scalar(db,"PRAGMA synchronous"),probe->sync_level);
        CHECK(probe->sync_level==mw_scalar(db,"PRAGMA synchronous"));
    }
    CHECK_RC(mw_exec(db, "CREATE TABLE t(v); PRAGMA synchronous=FULL; BEGIN; INSERT INTO t VALUES(1)"), SQLITE_OK);
    CHECK_RC(mw_exec(db, "PRAGMA synchronous=OFF"), SQLITE_ERROR);
    mw_lane *lane = NULL; CHECK_RC(sqlite3_file_control(db,"main",MW_FCNTL_LANE_PTR,&lane), SQLITE_OK);
    printf("rejected OFF: SQLite=%lld engine=%d\n", (long long)mw_scalar(db,"PRAGMA synchronous"),lane->sync_level);
    CHECK(lane->sync_level == mw_scalar(db,"PRAGMA synchronous"));
    mw_db_stats before, after;
    CHECK_RC(sqlite3_file_control(db,"main",MW_FCNTL_DBSTATS,&before),SQLITE_OK);
    CHECK_RC(mw_exec(db,"COMMIT"), SQLITE_OK);
    CHECK_RC(sqlite3_file_control(db,"main",MW_FCNTL_DBSTATS,&after),SQLITE_OK);
    printf("FULL commit: log sync delta=%llu\n",(unsigned long long)(after.log_syncs-before.log_syncs));
    CHECK(after.log_syncs > before.log_syncs);
    sqlite3_close(db); mw_rmdb(path); MW_DONE();
}
