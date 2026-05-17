#include <stdlib.h>
#include <string.h>
#include "threadpool.h"

#ifdef _WIN32
#include <windows.h>
#include <limits.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct task_node {
    threadpool_task_fn fn;
    void *data;
    struct task_node *next;
} task_node_t;

#ifdef _WIN32

typedef HANDLE thread_handle_t;
typedef CRITICAL_SECTION mutex_t;
typedef HANDLE sem_t;
typedef HANDLE event_t;

static void mutex_init(mutex_t *mutex) {
    InitializeCriticalSection(mutex);
}

static void mutex_destroy(mutex_t *mutex) {
    DeleteCriticalSection(mutex);
}

static void mutex_lock(mutex_t *mutex) {
    EnterCriticalSection(mutex);
}

static void mutex_unlock(mutex_t *mutex) {
    LeaveCriticalSection(mutex);
}

#else

typedef pthread_t thread_handle_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;

static void mutex_init(mutex_t *mutex) {
    pthread_mutex_init(mutex, NULL);
}

static void mutex_destroy(mutex_t *mutex) {
    pthread_mutex_destroy(mutex);
}

static void mutex_lock(mutex_t *mutex) {
    pthread_mutex_lock(mutex);
}

static void mutex_unlock(mutex_t *mutex) {
    pthread_mutex_unlock(mutex);
}

static void cond_init(cond_t *cond) {
    pthread_cond_init(cond, NULL);
}

static void cond_wait(cond_t *cond, mutex_t *mutex) {
    pthread_cond_wait(cond, mutex);
}

static void cond_signal(cond_t *cond) {
    pthread_cond_signal(cond);
}

static void cond_broadcast(cond_t *cond) {
    pthread_cond_broadcast(cond);
}

#endif

typedef struct {
    struct threadpool *pool;
    int index;
} worker_arg_t;

struct threadpool {
    int num_threads;
    thread_handle_t *threads;
    worker_arg_t *worker_args;
    task_node_t *head;
    task_node_t *tail;
    int pending;
    int stop;
    mutex_t mutex;
#ifdef _WIN32
    sem_t work_semaphore;
    event_t completed_event;
#else
    cond_t cond;
#endif
};

static void task_queue_push(struct threadpool *pool, task_node_t *node) {
    if (pool->tail) {
        pool->tail->next = node;
    } else {
        pool->head = node;
    }
    pool->tail = node;
}

static task_node_t *task_queue_pop(struct threadpool *pool) {
    task_node_t *node = pool->head;
    if (node) {
        pool->head = node->next;
        if (pool->head == NULL) {
            pool->tail = NULL;
        }
        node->next = NULL;
    }
    return node;
}

#ifdef _WIN32
static DWORD WINAPI threadpool_worker(LPVOID arg) {
    worker_arg_t *worker_arg = (worker_arg_t*)arg;
    struct threadpool *pool = worker_arg->pool;
    int worker_index = worker_arg->index;
    while (1) {
        task_node_t *node = NULL;
        WaitForSingleObject(pool->work_semaphore, INFINITE);
        mutex_lock(&pool->mutex);
        if (pool->stop && pool->head == NULL) {
            mutex_unlock(&pool->mutex);
            break;
        }
        node = task_queue_pop(pool);
        mutex_unlock(&pool->mutex);

        if (node) {
            node->fn(node->data, worker_index);
            free(node);
            mutex_lock(&pool->mutex);
            pool->pending -= 1;
            if (pool->pending == 0) {
                SetEvent(pool->completed_event);
            }
            mutex_unlock(&pool->mutex);
        }
    }
    return 0;
}
#else
static void *threadpool_worker(void *arg) {
    worker_arg_t *worker_arg = (worker_arg_t*)arg;
    struct threadpool *pool = worker_arg->pool;
    int worker_index = worker_arg->index;
    while (1) {
        task_node_t *node;
        mutex_lock(&pool->mutex);
        while (!pool->stop && pool->head == NULL) {
            cond_wait(&pool->cond, &pool->mutex);
        }
        if (pool->stop && pool->head == NULL) {
            mutex_unlock(&pool->mutex);
            break;
        }
        node = task_queue_pop(pool);
        mutex_unlock(&pool->mutex);

        if (node) {
            node->fn(node->data, worker_index);
            free(node);
            mutex_lock(&pool->mutex);
            pool->pending -= 1;
            if (pool->pending == 0) {
                cond_broadcast(&pool->cond);
            }
            mutex_unlock(&pool->mutex);
        }
    }
    return NULL;
}
#endif

int threadpool_default_threads(void) {
    const char *env_value = getenv("RENDERER_THREADS");
    if (env_value != NULL && env_value[0] != '\0') {
        int requested = atoi(env_value);
        if (requested <= 0) {
            return 0;
        }
        return requested;
    }
#ifdef _WIN32
    {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return (int)info.dwNumberOfProcessors;
    }
#else
    {
        long count = sysconf(_SC_NPROCESSORS_ONLN);
        if (count < 1) {
            count = 1;
        }
        return (int)count;
    }
#endif
}

threadpool_t *threadpool_create(int num_threads) {
    threadpool_t *pool;
    int i;

    if (num_threads <= 1) {
        return NULL;
    }

    pool = (threadpool_t*)malloc(sizeof(threadpool_t));
    memset(pool, 0, sizeof(threadpool_t));
    pool->num_threads = num_threads;
    pool->threads = (thread_handle_t*)malloc(sizeof(thread_handle_t) * num_threads);
    pool->worker_args = (worker_arg_t*)malloc(sizeof(worker_arg_t) * num_threads);

    mutex_init(&pool->mutex);
#ifdef _WIN32
    pool->work_semaphore = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    pool->completed_event = CreateEvent(NULL, TRUE, TRUE, NULL);
#else
    cond_init(&pool->cond);
#endif

    for (i = 0; i < num_threads; i++) {
        pool->worker_args[i].pool = pool;
        pool->worker_args[i].index = i;
#ifdef _WIN32
        pool->threads[i] = CreateThread(NULL, 0, threadpool_worker,
                                        &pool->worker_args[i], 0, NULL);
#else
        pthread_create(&pool->threads[i], NULL, threadpool_worker,
                       &pool->worker_args[i]);
#endif
    }

    return pool;
}

void threadpool_release(threadpool_t *pool) {
    int i;
    task_node_t *node;

    if (pool == NULL) {
        return;
    }

    threadpool_wait(pool);

    mutex_lock(&pool->mutex);
    pool->stop = 1;
    mutex_unlock(&pool->mutex);

#ifdef _WIN32
    ReleaseSemaphore(pool->work_semaphore, pool->num_threads, NULL);
#else
    cond_broadcast(&pool->cond);
#endif

    for (i = 0; i < pool->num_threads; i++) {
#ifdef _WIN32
        WaitForSingleObject(pool->threads[i], INFINITE);
        CloseHandle(pool->threads[i]);
#else
        pthread_join(pool->threads[i], NULL);
#endif
    }

    node = pool->head;
    while (node) {
        task_node_t *next = node->next;
        free(node);
        node = next;
    }

    mutex_destroy(&pool->mutex);
#ifdef _WIN32
    CloseHandle(pool->work_semaphore);
    CloseHandle(pool->completed_event);
#endif
    free(pool->threads);
    free(pool->worker_args);
    free(pool);
}

void threadpool_submit(threadpool_t *pool, threadpool_task_fn fn,
                       void *task_data) {
    task_node_t *node;

    if (pool == NULL) {
        fn(task_data, 0);
        return;
    }

    node = (task_node_t*)malloc(sizeof(task_node_t));
    node->fn = fn;
    node->data = task_data;
    node->next = NULL;

    mutex_lock(&pool->mutex);
    task_queue_push(pool, node);
    pool->pending += 1;
    #ifdef _WIN32
    ResetEvent(pool->completed_event);
    #endif
    mutex_unlock(&pool->mutex);

#ifdef _WIN32
    ReleaseSemaphore(pool->work_semaphore, 1, NULL);
#else
    cond_signal(&pool->cond);
#endif
}

void threadpool_wait(threadpool_t *pool) {
    if (pool == NULL) {
        return;
    }

#ifdef _WIN32
    WaitForSingleObject(pool->completed_event, INFINITE);
#else
    mutex_lock(&pool->mutex);
    while (pool->pending > 0) {
        cond_wait(&pool->cond, &pool->mutex);
    }
    mutex_unlock(&pool->mutex);
#endif
}

int threadpool_num_threads(threadpool_t *pool) {
    if (pool == NULL) {
        return 0;
    }
    return pool->num_threads;
}
