// Build multiwriter_log.c with -Dpwritev=inject_pwritev to pause its real write.
#include "mw_test.h"
#include "multiwriter_internal.h"
#include <sys/uio.h>
#include <sys/wait.h>
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static int armed,entered,released;
ssize_t inject_pwritev(int fd,const struct iovec *iov,int n,off_t off){
 pthread_mutex_lock(&mu);
 if(armed){armed=0;entered=1;pthread_cond_broadcast(&cv);while(!released)pthread_cond_wait(&cv,&mu);}
 pthread_mutex_unlock(&mu);return pwritev(fd,iov,n,off);
}
typedef struct{sqlite3*d;const char*q;int rc;}job;
static void *run(void*p){job*j=p;j->rc=mw_exec(j->d,j->q);return NULL;}
int main(void){
 char path[256],uri[512];mw_tmpdb(path,sizeof path,"normal");
 snprintf(uri,sizeof uri,"file:%s?mw=1&mw_compact_ms=0&mw_log_max_mb=0",path);
 pid_t child=fork();if(child==0){
  alarm(15);sqlite3*a=NULL,*b=NULL;
  if(sqlite3_open_v2(uri,&a,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI,NULL))_exit(2);
  if(sqlite3_open_v2(uri,&b,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_URI,NULL))_exit(3);
  if(mw_exec(a,"CREATE TABLE a(v); CREATE TABLE b(v); INSERT INTO a VALUES(0); INSERT INTO b VALUES(0); PRAGMA synchronous=NORMAL"))_exit(4);
  if(mw_exec(b,"PRAGMA synchronous=NORMAL"))_exit(5);
  mw_lane *lane=NULL;sqlite3_file_control(a,"main",MW_FCNTL_LANE_PTR,&lane);
  uint64_t epoch=atomic_load(&lane->db->epoch);
  job ja={a,"UPDATE a SET v=1",-1},jb={b,"UPDATE b SET v=1",-1};pthread_t ta,tb;
  armed=1;pthread_create(&ta,NULL,run,&ja);
  pthread_mutex_lock(&mu);while(!entered)pthread_cond_wait(&cv,&mu);pthread_mutex_unlock(&mu);
  pthread_create(&tb,NULL,run,&jb);
  for(;;){pthread_mutex_lock(&lane->db->log_mu);int ready=lane->db->written_upto>=epoch+2 && (lane->db->sync_waiters>0 || atomic_load(&lane->db->vis_parked)>0);pthread_mutex_unlock(&lane->db->log_mu);if(ready)break;usleep(100);}
  pthread_mutex_lock(&mu);released=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&mu);
  pthread_join(ta,NULL);pthread_join(tb,NULL);
  fprintf(stderr,"acknowledged commits: A=%d B=%d; in-memory values=%lld,%lld\n",ja.rc,jb.rc,(long long)mw_scalar(a,"SELECT v FROM a"),(long long)mw_scalar(b,"SELECT v FROM b"));
  _exit(ja.rc||jb.rc?6:0);
 }
 int st;waitpid(child,&st,0);CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0);
 sqlite3*d=NULL;CHECK_RC(sqlite3_open_v2(uri,&d,SQLITE_OPEN_READWRITE|SQLITE_OPEN_URI,NULL),SQLITE_OK);
 printf("after process exit: A=%lld B=%lld\n",(long long)mw_scalar(d,"SELECT v FROM a"),(long long)mw_scalar(d,"SELECT v FROM b"));
 CHECK(mw_scalar(d,"SELECT v FROM a")==1);CHECK(mw_scalar(d,"SELECT v FROM b")==1);
 sqlite3_close(d);mw_rmdb(path);MW_DONE();
}
