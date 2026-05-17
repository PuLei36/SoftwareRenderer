#ifndef THREADPOOL_H
#define THREADPOOL_H

typedef struct threadpool threadpool_t;
typedef void (*threadpool_task_fn)(void *task_data, int worker_index);

int threadpool_default_threads(void);
threadpool_t *threadpool_create(int num_threads);
void threadpool_release(threadpool_t *pool);
void threadpool_submit(threadpool_t *pool, threadpool_task_fn fn,
                       void *task_data);
void threadpool_wait(threadpool_t *pool);
int threadpool_num_threads(threadpool_t *pool);

#endif
