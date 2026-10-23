#ifndef JOBS_H
#define JOBS_H

/* Trabajos en otro hilo (decodificar gráficos y sonidos de una zona nueva
   sin trabar el juego; la Pi tiene 4 núcleos y el juego usa uno). Hasta
   jobs_start, y si la cola se llena, jobs_push los hace en el momento. */
typedef void (*JobFn)(void *ptr, int arg);

void jobs_start(void);
int  jobs_running(void);
void jobs_push(JobFn fn, void *ptr, int arg);
/* Adelante de los que esperan (lo que hace falta antes: los DT1 de la zona
   que se va a armar, delante de los gráficos que se piden al llegar). Sin
   hilo o con la cola llena no lo hace: 0 */
int  jobs_push_first(JobFn fn, void *ptr, int arg);
/* Lo que conviene tener hecho pero se puede saltear (precargar sonidos): va
   después de todos los otros; con la cola llena no se hace y jobs_wait
   descarta los que no empezaron. Sin el hilo, ahora. */
void jobs_push_idle(JobFn fn, void *ptr, int arg);
int  jobs_pending(void); /* los que faltan (en la cola o haciéndose) */
void jobs_wait(int max_ms); /* espera a que terminen (al salir) */

/* Una marca que comparten el juego y el hilo de trabajos (por ejemplo,
   "cargando" -> "listo"): lo que el hilo escribió antes de poner la marca se
   ve completo del otro lado. Atómicos del compilador (GCC y Clang), que
   ThreadSanitizer entiende, a diferencia de los de SDL. */
static inline int job_flag_get(const int *flag)
{
    return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

static inline void job_flag_set(int *flag, int value)
{
    __atomic_store_n(flag, value, __ATOMIC_RELEASE);
}

#endif
