// The replay of a negative integer must not shift a negative value (undefined behaviour in C). The result is right either way: this test
// is a regression guard only under `test/sanitize.sh ubsan` (it is in its default list); in a plain build it passes on the old code too.
#include "mw_test.h"
#include "multiwriter.h"
int main(void){
 char p[256],u[512];mw_tmpdb(p,sizeof p,"negative");snprintf(u,sizeof u,"file:%s?mw=1&mw_rebase=1&mw_rebase_backoff=0",p);
 sqlite3 *a=NULL,*b=NULL;
 CHECK_RC(sqlite3_open_v2(u,&a,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI,NULL),SQLITE_OK);
 CHECK_RC(sqlite3_open_v2(u,&b,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI,NULL),SQLITE_OK);
 CHECK_RC(mw_exec(a,"CREATE TABLE t(id INTEGER PRIMARY KEY,v); INSERT INTO t VALUES(1,-129),(2,0)"),SQLITE_OK);
 CHECK_RC(mw_exec(a,"BEGIN; UPDATE t SET v=-130 WHERE id=1"),SQLITE_OK);
 CHECK_RC(mw_exec(b,"UPDATE t SET v=1 WHERE id=2"),SQLITE_OK);
 CHECK_RC(mw_exec(a,"COMMIT"),SQLITE_OK);
 CHECK(mw_scalar(b,"SELECT v FROM t WHERE id=1")==-130);
 sqlite3_close(a);sqlite3_close(b);mw_rmdb(p);MW_DONE();
}
