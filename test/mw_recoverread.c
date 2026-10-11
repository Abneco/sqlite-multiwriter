#include "mw_test.h"
#include "multiwriter.h"
#include <errno.h>
#include <sys/wait.h>
static int op(const char *u,sqlite3 **d){return sqlite3_open_v2(u,d,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI,NULL);}
int main(void){
 setenv("MW_SEG_MB","1",1);
 for(int nth=1;nth<=24;nth++){
  char path[256],uri[512];mw_tmpdb(path,sizeof path,"recovery");
  snprintf(uri,sizeof uri,"file:%s?mw=1&mw_mp=1&mw_compact_ms=0&mw_log_max_mb=0",path);
  pid_t p=fork();if(p==0){sqlite3*d=NULL;if(op(uri,&d)!=SQLITE_OK)_exit(2);
   if(mw_exec(d,"CREATE TABLE t(v)")!=SQLITE_OK)_exit(3);
   for(int i=0;i<4;i++)if(mw_exec(d,"INSERT INTO t VALUES(zeroblob(600000))")!=SQLITE_OK)_exit(4);
   _exit(0);
  }
  int status;waitpid(p,&status,0);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
  sqlite3*d=NULL;mw_io_fault_arm(MW_IO_READ,nth,EIO,0,0);int rc=op(uri,&d);mw_io_fault_disarm();sqlite3_close(d);d=NULL;
  int rr=op(uri,&d);long long count=rr==SQLITE_OK?mw_scalar(d,"SELECT count(*) FROM t"):-1;
  printf("read failure #%d: first open=%d, retry=%d, rows=%lld (expected 4)\n",nth,rc,rr,count);
  CHECK(rr==SQLITE_OK&&count==4);sqlite3_close(d);mw_rmdb(path);
 }
 MW_DONE();
}
