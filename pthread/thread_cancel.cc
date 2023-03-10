#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>


void *tfn(void *arg){
 while (1) {
 printf("thread: pid = %d, tid = %lu\n", getpid(), pthread_self());
//  pthread_testcancel();
 sleep(1);
 }

 return NULL;
 }

 int main(int argc, char *argv[]){
 pthread_t tid;

 int ret = pthread_create(&tid, NULL, tfn, NULL);
 if (ret != 0) {
 fprintf(stderr, "pthread_create error:%s\n", strerror(ret));
 exit(1);
 }

 printf("main: pid = %d, tid = %lu\n", getpid(), pthread_self());

 sleep(5);

 ret = pthread_cancel(tid); // 终止线程
 if (ret != 0) {
 fprintf(stderr, "pthread_cancel error:%s\n", strerror(ret));
 exit(1);
 }

 while (1);

 pthread_exit((void *)0);
}