#ifndef __PTHREAD_H_
#define __PTHREAD_H_

typedef unsigned long pthread_t;
int pthread_create(pthread_t *thread, void *attr, void *start_routine, void *arg);
int pthread_join(pthread_t thread, void **retval);

#endif