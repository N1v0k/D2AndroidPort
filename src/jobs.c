#include "jobs.h"

#include <SDL.h>

#define JOB_QUEUE 1024

typedef struct {
    JobFn fn;
    void *ptr;
    int   arg;
} Job;

static Job          s_queue[JOB_QUEUE];
static int          s_head, s_count;
static Job          s_idle[JOB_QUEUE]; /* los de jobs_push_idle: cuando no hay otros */
static int          s_idle_head, s_idle_count;
static int s_pending;
static SDL_mutex   *s_mx;
static SDL_cond    *s_cv;
static SDL_Thread  *s_thread;

static int worker(void *unused)
{
    (void)unused;
    for (;;) {
        SDL_LockMutex(s_mx);
        while (s_count == 0 && s_idle_count == 0)
            SDL_CondWait(s_cv, s_mx);
        Job j;
        if (s_count > 0) {
            j = s_queue[s_head];
            s_head = (s_head + 1) % JOB_QUEUE;
            s_count--;
        } else {
            j = s_idle[s_idle_head];
            s_idle_head = (s_idle_head + 1) % JOB_QUEUE;
            s_idle_count--;
        }
        SDL_UnlockMutex(s_mx);
        j.fn(j.ptr, j.arg);
        __atomic_sub_fetch(&s_pending, 1, __ATOMIC_ACQ_REL);
    }
    return 0;
}

void jobs_start(void)
{
    if (s_thread)
        return;
    s_mx = SDL_CreateMutex();
    s_cv = SDL_CreateCond();
    if (s_mx && s_cv)
        s_thread = SDL_CreateThread(worker, "d2jobs", NULL);
}

int jobs_running(void)
{
    return s_thread != NULL;
}

void jobs_push(JobFn fn, void *ptr, int arg)
{
    if (s_thread) {
        SDL_LockMutex(s_mx);
        if (s_count < JOB_QUEUE) {
            s_queue[(s_head + s_count) % JOB_QUEUE] = (Job){ fn, ptr, arg };
            s_count++;
            __atomic_add_fetch(&s_pending, 1, __ATOMIC_ACQ_REL);
            SDL_CondSignal(s_cv);
            SDL_UnlockMutex(s_mx);
            return;
        }
        SDL_UnlockMutex(s_mx);
    }
    fn(ptr, arg); /* sin hilo o con la cola llena: ahora */
}

void jobs_push_idle(JobFn fn, void *ptr, int arg)
{
    if (!s_thread) {
        fn(ptr, arg);
        return;
    }
    SDL_LockMutex(s_mx);
    if (s_idle_count < JOB_QUEUE) { /* (llena, no se hace) */
        s_idle[(s_idle_head + s_idle_count) % JOB_QUEUE] = (Job){ fn, ptr, arg };
        s_idle_count++;
        __atomic_add_fetch(&s_pending, 1, __ATOMIC_ACQ_REL);
        SDL_CondSignal(s_cv);
    }
    SDL_UnlockMutex(s_mx);
}

int jobs_push_first(JobFn fn, void *ptr, int arg)
{
    int queued = 0;
    if (s_thread) {
        SDL_LockMutex(s_mx);
        if (s_count < JOB_QUEUE) {
            s_head = (s_head + JOB_QUEUE - 1) % JOB_QUEUE;
            s_queue[s_head] = (Job){ fn, ptr, arg };
            s_count++;
            __atomic_add_fetch(&s_pending, 1, __ATOMIC_ACQ_REL);
            SDL_CondSignal(s_cv);
            queued = 1;
        }
        SDL_UnlockMutex(s_mx);
    }
    return queued;
}

int jobs_pending(void)
{
    return __atomic_load_n(&s_pending, __ATOMIC_ACQUIRE);
}

void jobs_wait(int max_ms)
{
    if (s_thread) { /* los que se pueden saltear y no empezaron, no se esperan */
        SDL_LockMutex(s_mx);
        __atomic_sub_fetch(&s_pending, s_idle_count, __ATOMIC_ACQ_REL);
        s_idle_count = 0;
        SDL_UnlockMutex(s_mx);
    }
    Uint32 t0 = SDL_GetTicks();
    while (s_thread && jobs_pending() > 0 && (int)(SDL_GetTicks() - t0) < max_ms)
        SDL_Delay(2);
}
