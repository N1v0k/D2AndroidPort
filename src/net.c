#include "net.h"
#include "lang.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* La red local (net.h, docs/plan-lan.md). Mensajes: [largo u32][tipo u8][datos], todo en orden
   de la Pi (little endian); los controles van tal cual (PlayerInput): todas las máquinas tienen el
   mismo ejecutable (se revisa al unirse). */

#ifdef _WIN32 /* (la red, por ahora solo en Linux: la Pi y la PC) */
int net_host(int port, const NetLocal *mine) { (void)port; (void)mine; return -1; }
int net_join(const char *host, int port, const NetLocal *mine) { (void)host; (void)port; (void)mine; return -1; }
void net_poll(void) {}
int net_role(void) { return NET_OFF; }
int net_lobby(int *cls, int *level, int *owner, int *diffmax, int max) { (void)cls; (void)level; (void)owner; (void)diffmax; (void)max; return 0; }
int net_start(int diff, int mode, int terror, uint32_t seed, long long epoch) { (void)diff; (void)mode; (void)terror; (void)seed; (void)epoch; return -1; }
const NetStart *net_started(void) { return NULL; }
void net_send_input(uint32_t tick, const PlayerInput *in, int n) { (void)tick; (void)in; (void)n; }
int net_frame(uint32_t tick, PlayerInput *all) { (void)tick; (void)all; return 0; }
void net_send_hash(uint32_t tick, uint64_t hash) { (void)tick; (void)hash; }
const char *net_error(void) { return "sin red en Windows"; }
void net_close(void) {}
void net_discover_start(void) {}
int net_discover_poll(char (*hosts)[64], int *players, int *modes, int max) { (void)hosts; (void)players; (void)modes; (void)max; return 0; }
void net_discover_stop(void) {}
const char *net_my_ip(void) { return ""; }
#else

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NET_MAGIC   "D2PN1 " __DATE__ " " __TIME__ /* (el mismo ejecutable en todas) */
#define NET_RING    256 /* ticks de controles guardados (10 segundos) */
#define NET_HRING   64  /* resúmenes del estado guardados */
#define NET_TIMEOUT 60  /* segundos sin noticias de una máquina: se cortó (armar zonas en la Pi tarda) */

enum { M_HELLO = 1, M_WELCOME, M_LOBBY, M_START, M_INPUT, M_FRAME, M_HASH, M_ERROR };

typedef struct {
    int      fd;                 /* -1 = esta misma (el anfitrión) o libre */
    int      used;
    uint8_t *in, *out;
    size_t   in_len, in_cap, out_len, out_cap;
    int      hello;              /* ya dijo quién es */
    int      num, first;         /* sus jugadores: cuántos y el primero en la partida */
    int      cls[MAX_LOCAL], level[MAX_LOCAL], diffmax[MAX_LOCAL];
    char    *save[MAX_LOCAL];
    size_t   save_len[MAX_LOCAL];
    time_t   seen;
    uint32_t hash_tick[NET_HRING];
    uint64_t hash_val[NET_HRING];
    uint8_t  hash_have[NET_HRING];
} Peer;

static int      s_role, s_listen = -1, s_udp = -1, s_disc = -1;
static Peer     s_peer[NET_MAX_PEERS]; /* el anfitrión: [0] es él; los otros: [0] es el anfitrión */
static int      s_npeers;
static char     s_err[160];
static int      s_has_err;
static NetStart s_start;
static int      s_started;
static int      s_lobby_n, s_lobby_cls[MAX_PLAYERS], s_lobby_level[MAX_PLAYERS], s_lobby_owner[MAX_PLAYERS], s_lobby_diff[MAX_PLAYERS];
/* Los controles de cada tick: los repartidos (todas) y, el anfitrión, los que va juntando */
static PlayerInput s_frame[NET_RING][MAX_PLAYERS];
static uint32_t    s_frame_tick[NET_RING];
static uint8_t     s_frame_ok[NET_RING];
static PlayerInput s_pend[NET_RING][MAX_PLAYERS];
static uint32_t    s_pend_tick[NET_RING];
static uint8_t     s_pend_have[NET_RING][NET_MAX_PEERS];
static uint32_t    s_next_build; /* el anfitrión: el próximo tick a repartir */
static uint32_t    s_my_hash_tick[NET_HRING];
static uint64_t    s_my_hash[NET_HRING];
static uint8_t     s_my_hash_have[NET_HRING];

static void fail(const char *msg);

/* ---- Bytes ---- */

static int buf_add(uint8_t **b, size_t *len, size_t *cap, const void *data, size_t n)
{
    if (*len + n > *cap) {
        size_t c = *cap ? *cap : 4096;
        while (c < *len + n)
            c *= 2;
        uint8_t *nb = realloc(*b, c);
        if (!nb)
            return -1;
        *b = nb;
        *cap = c;
    }
    memcpy(*b + *len, data, n);
    *len += n;
    return 0;
}

typedef struct {
    uint8_t *d;
    size_t   len, cap;
} Msg;

static void m_bytes(Msg *m, const void *p, size_t n) { buf_add(&m->d, &m->len, &m->cap, p, n); }
static void m_u8(Msg *m, unsigned v) { uint8_t b = (uint8_t)v; m_bytes(m, &b, 1); }
static void m_u32(Msg *m, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    m_bytes(m, b, 4);
}
static void m_u64(Msg *m, uint64_t v)
{
    m_u32(m, (uint32_t)v);
    m_u32(m, (uint32_t)(v >> 32));
}

static void m_begin(Msg *m, int type)
{
    memset(m, 0, sizeof *m);
    m_u32(m, 0); /* (el largo, al final) */
    m_u8(m, (unsigned)type);
}

typedef struct {
    const uint8_t *d;
    size_t         len, pos;
    int            bad;
} Rd;

static uint32_t r_u8(Rd *r)
{
    if (r->pos + 1 > r->len) {
        r->bad = 1;
        return 0;
    }
    return r->d[r->pos++];
}
static uint32_t r_u32(Rd *r)
{
    if (r->pos + 4 > r->len) {
        r->bad = 1;
        return 0;
    }
    const uint8_t *b = r->d + r->pos;
    r->pos += 4;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static uint64_t r_u64(Rd *r)
{
    uint64_t lo = r_u32(r);
    return lo | (uint64_t)r_u32(r) << 32;
}
static const uint8_t *r_bytes(Rd *r, size_t n)
{
    if (r->pos + n > r->len) {
        r->bad = 1;
        return NULL;
    }
    const uint8_t *p = r->d + r->pos;
    r->pos += n;
    return p;
}

/* ---- Conexiones ---- */

static void send_to(Peer *p, Msg *m)
{
    uint32_t n = (uint32_t)m->len;
    m->d[0] = (uint8_t)n;
    m->d[1] = (uint8_t)(n >> 8);
    m->d[2] = (uint8_t)(n >> 16);
    m->d[3] = (uint8_t)(n >> 24);
    if (p->fd >= 0)
        buf_add(&p->out, &p->out_len, &p->out_cap, m->d, m->len);
}

static void flush_peer(Peer *p)
{
    while (p->fd >= 0 && p->out_len > 0) {
        ssize_t k = send(p->fd, p->out, p->out_len, MSG_NOSIGNAL);
        if (k <= 0) {
            if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                return;
            close(p->fd);
            p->fd = -1;
            p->out_len = 0;
            return;
        }
        memmove(p->out, p->out + k, p->out_len - (size_t)k);
        p->out_len -= (size_t)k;
    }
}

static void flush_all(void)
{
    for (int i = 0; i < s_npeers; i++)
        flush_peer(&s_peer[i]);
}

static void broadcast(Msg *m) /* a todos los que están conectados (el anfitrión: las otras máquinas) */
{
    for (int i = 0; i < s_npeers; i++)
        if (s_peer[i].fd >= 0 && s_peer[i].hello)
            send_to(&s_peer[i], m);
}

static void set_socket(int fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); /* (los controles, ya: sin juntarlos) */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static void peer_free(Peer *p)
{
    if (p->fd >= 0)
        close(p->fd);
    free(p->in);
    free(p->out);
    for (int k = 0; k < MAX_LOCAL; k++)
        free(p->save[k]);
    memset(p, 0, sizeof *p);
    p->fd = -1;
}

static void fail(const char *msg)
{
    if (s_has_err)
        return;
    s_has_err = 1;
    snprintf(s_err, sizeof s_err, "%s", msg);
    fprintf(stderr, "red: %s\n", msg);
    if (s_role == NET_HOST) { /* que se enteren las otras */
        Msg m;
        m_begin(&m, M_ERROR);
        m_bytes(&m, msg, strlen(msg) + 1);
        broadcast(&m);
        free(m.d);
        flush_all();
    }
}

/* ---- El lobby ---- */

static int lobby_count(void)
{
    int n = 0;
    for (int i = 0; i < s_npeers; i++)
        if (s_peer[i].used && (i == 0 || s_peer[i].hello))
            n += s_peer[i].num;
    return n;
}

static void send_lobby(void)
{
    Msg m;
    m_begin(&m, M_LOBBY);
    m_u8(&m, (unsigned)lobby_count());
    for (int i = 0; i < s_npeers; i++)
        if (s_peer[i].used && (i == 0 || s_peer[i].hello))
            for (int k = 0; k < s_peer[i].num; k++) {
                m_u8(&m, (unsigned)i);
                m_u8(&m, (unsigned)s_peer[i].cls[k]);
                m_u8(&m, (unsigned)s_peer[i].level[k]);
                m_u8(&m, (unsigned)s_peer[i].diffmax[k]);
            }
    broadcast(&m);
    free(m.d);
}

static void set_local(Peer *p, const NetLocal *mine)
{
    p->num = mine->num < 1 ? 1 : mine->num > MAX_LOCAL ? MAX_LOCAL : mine->num;
    for (int k = 0; k < p->num; k++) {
        p->cls[k] = mine->cls[k];
        p->level[k] = mine->level[k];
        p->diffmax[k] = mine->diffmax[k];
        p->save_len[k] = mine->save[k] ? mine->save_len[k] : 0;
        p->save[k] = malloc(p->save_len[k] + 1);
        if (p->save[k]) {
            if (p->save_len[k])
                memcpy(p->save[k], mine->save[k], p->save_len[k]);
            p->save[k][p->save_len[k]] = '\0';
        }
    }
}

static void reset_state(void)
{
    for (int i = 0; i < NET_MAX_PEERS; i++)
        peer_free(&s_peer[i]);
    s_npeers = 0;
    s_has_err = s_started = 0;
    s_err[0] = '\0';
    for (int i = 0; i < MAX_PLAYERS; i++)
        free(s_start.save[i]);
    memset(&s_start, 0, sizeof s_start);
    memset(s_frame_ok, 0, sizeof s_frame_ok);
    memset(s_pend_have, 0, sizeof s_pend_have);
    memset(s_my_hash_have, 0, sizeof s_my_hash_have);
    for (int i = 0; i < NET_RING; i++)
        s_pend_tick[i] = s_frame_tick[i] = UINT32_MAX;
    s_next_build = 0;
    s_lobby_n = 0;
}

static int s_host_mode; /* lo que anuncia el anfitrión (NetLocal.mode) */

int net_host(int port, const NetLocal *mine)
{
    net_close();
    reset_state();
    s_host_mode = mine->mode;
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0)
        return -1;
    int one = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(s_listen, (struct sockaddr *)&a, sizeof a) != 0 || listen(s_listen, NET_MAX_PEERS) != 0) {
        fprintf(stderr, "red: no pude escuchar en el puerto %d: %s\n", port, strerror(errno));
        close(s_listen);
        s_listen = -1;
        return -1;
    }
    fcntl(s_listen, F_SETFL, fcntl(s_listen, F_GETFL, 0) | O_NONBLOCK);
    /* Las respuestas a los que buscan partidas (UDP, el mismo puerto) */
    s_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_udp >= 0) {
        setsockopt(s_udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (bind(s_udp, (struct sockaddr *)&a, sizeof a) != 0) {
            close(s_udp);
            s_udp = -1;
        } else {
            fcntl(s_udp, F_SETFL, fcntl(s_udp, F_GETFL, 0) | O_NONBLOCK);
        }
    }
    s_role = NET_HOST;
    s_npeers = 1;
    Peer *me = &s_peer[0];
    me->used = 1;
    me->fd = -1;
    me->hello = 1;
    set_local(me, mine);
    printf("red: esperando en el puerto %d (%s)\n", port, net_my_ip());
    return 0;
}

int net_join(const char *host, int port, const NetLocal *mine)
{
    net_close();
    reset_state();
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) {
        fprintf(stderr, "red: no encuentro %s\n", host);
        return -1;
    }
    /* Sin bloquear el menú más de 3 segundos si no hay nadie en esa IP */
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol), ok = 0;
    if (fd >= 0) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
            ok = 1;
        } else if (errno == EINPROGRESS) {
            struct pollfd pf = { fd, POLLOUT, 0 };
            int err = 0;
            socklen_t el = sizeof err;
            ok = poll(&pf, 1, 3000) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0;
        }
    }
    freeaddrinfo(res);
    if (!ok) {
        fprintf(stderr, "red: no pude conectarme a %s:%d (%s)\n", host, port, strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    set_socket(fd);
    s_role = NET_CLIENT;
    s_npeers = 1;
    Peer *h = &s_peer[0];
    h->used = h->hello = 1;
    h->fd = fd;
    h->seen = time(NULL);
    Msg m;
    m_begin(&m, M_HELLO);
    m_bytes(&m, NET_MAGIC, strlen(NET_MAGIC) + 1);
    int num = mine->num < 1 ? 1 : mine->num > MAX_LOCAL ? MAX_LOCAL : mine->num;
    m_u8(&m, (unsigned)num);
    for (int k = 0; k < num; k++) {
        m_u8(&m, (unsigned)mine->cls[k]);
        m_u8(&m, (unsigned)mine->level[k]);
        m_u8(&m, (unsigned)mine->diffmax[k]);
        size_t n = mine->save[k] ? mine->save_len[k] : 0;
        m_u32(&m, (uint32_t)n);
        if (n)
            m_bytes(&m, mine->save[k], n);
    }
    send_to(h, &m);
    free(m.d);
    flush_all();
    printf("red: conectado a %s:%d\n", host, port);
    return 0;
}

/* ---- Los controles ---- */

static void put_frame(uint32_t tick, const PlayerInput *all, int n)
{
    int i = (int)(tick % NET_RING);
    memset(s_frame[i], 0, sizeof s_frame[i]);
    memcpy(s_frame[i], all, (size_t)n * sizeof *all);
    s_frame_tick[i] = tick;
    s_frame_ok[i] = 1;
}

/* El anfitrión: reparte los ticks que ya tienen los controles de todas las máquinas */
static void build_frames(void)
{
    if (s_role != NET_HOST || !s_started || s_has_err)
        return;
    for (;;) {
        uint32_t t = s_next_build;
        int i = (int)(t % NET_RING), all = 1;
        if (t >= (uint32_t)s_start.delay) {
            if (s_pend_tick[i] != t)
                return;
            for (int p = 0; p < s_npeers && all; p++)
                all = !s_peer[p].used || s_pend_have[i][p];
        } else { /* (los primeros, quietos) */
            memset(s_pend[i], 0, sizeof s_pend[i]);
        }
        if (!all)
            return;
        put_frame(t, s_pend[i], s_start.num_players);
        Msg m;
        m_begin(&m, M_FRAME);
        m_u32(&m, t);
        m_u8(&m, (unsigned)s_start.num_players);
        m_bytes(&m, s_pend[i], (size_t)s_start.num_players * sizeof(PlayerInput));
        broadcast(&m);
        free(m.d);
        s_pend_tick[i] = UINT32_MAX;
        memset(s_pend_have[i], 0, sizeof s_pend_have[i]);
        s_next_build = t + 1;
    }
}

static void host_input(int peer, uint32_t tick, const PlayerInput *in, int n)
{
    if (tick < s_next_build || tick >= s_next_build + NET_RING) /* (viejo o demasiado adelantado) */
        return;
    int i = (int)(tick % NET_RING);
    if (s_pend_tick[i] != tick) {
        s_pend_tick[i] = tick;
        memset(s_pend[i], 0, sizeof s_pend[i]);
        memset(s_pend_have[i], 0, sizeof s_pend_have[i]);
    }
    Peer *p = &s_peer[peer];
    for (int k = 0; k < n && k < p->num; k++)
        s_pend[i][p->first + k] = in[k];
    s_pend_have[i][peer] = 1;
    build_frames();
}

void net_send_input(uint32_t tick, const PlayerInput *in, int n)
{
    if (!s_started || s_has_err)
        return;
    if (s_role == NET_HOST) {
        host_input(0, tick, in, n);
    } else if (s_role == NET_CLIENT) {
        Msg m;
        m_begin(&m, M_INPUT);
        m_u32(&m, tick);
        m_u8(&m, (unsigned)n);
        m_bytes(&m, in, (size_t)n * sizeof *in);
        send_to(&s_peer[0], &m);
        free(m.d);
    }
    flush_all();
}

int net_frame(uint32_t tick, PlayerInput *all)
{
    int i = (int)(tick % NET_RING);
    if (!s_started || !s_frame_ok[i] || s_frame_tick[i] != tick)
        return 0;
    memcpy(all, s_frame[i], sizeof s_frame[i]);
    return 1;
}

/* ---- El resumen del estado ---- */

static void check_hash(int peer)
{
    Peer *p = &s_peer[peer];
    for (int k = 0; k < NET_HRING; k++) {
        if (!p->hash_have[k] || !s_my_hash_have[k] || p->hash_tick[k] != s_my_hash_tick[k])
            continue;
        if (p->hash_val[k] != s_my_hash[k]) {
            char msg[160];
            snprintf(msg, sizeof msg, tr("Las máquinas se separaron (la %d, en el tick %u)"), peer + 1, (unsigned)p->hash_tick[k]);
            fail(msg);
        }
        p->hash_have[k] = 0;
    }
}

void net_send_hash(uint32_t tick, uint64_t hash)
{
    if (!s_started || s_has_err)
        return;
    int k = (int)((tick / 25u) % NET_HRING);
    if (s_role == NET_HOST) {
        s_my_hash_tick[k] = tick;
        s_my_hash[k] = hash;
        s_my_hash_have[k] = 1;
        for (int p = 1; p < s_npeers; p++)
            check_hash(p);
    } else if (s_role == NET_CLIENT) {
        Msg m;
        m_begin(&m, M_HASH);
        m_u32(&m, tick);
        m_u64(&m, hash);
        send_to(&s_peer[0], &m);
        free(m.d);
        flush_all();
    }
}

/* ---- Lo que llega ---- */

static void on_hello(int pi, Rd *r)
{
    Peer *p = &s_peer[pi];
    const uint8_t *magic = r_bytes(r, strlen(NET_MAGIC) + 1);
    if (!magic || memcmp(magic, NET_MAGIC, strlen(NET_MAGIC) + 1) != 0) {
        fprintf(stderr, "red: la máquina %d tiene otra versión del juego: no entra\n", pi + 1);
        peer_free(p);
        return;
    }
    p->num = (int)r_u8(r);
    p->num = p->num < 1 ? 1 : p->num > MAX_LOCAL ? MAX_LOCAL : p->num;
    for (int k = 0; k < p->num; k++) {
        p->cls[k] = (int)r_u8(r);
        p->level[k] = (int)r_u8(r);
        p->diffmax[k] = (int)r_u8(r);
        uint32_t n = r_u32(r);
        const uint8_t *d = r_bytes(r, n);
        p->save[k] = malloc((size_t)n + 1);
        if (p->save[k] && d) {
            memcpy(p->save[k], d, n);
            p->save[k][n] = '\0';
            p->save_len[k] = n;
        }
    }
    if (r->bad || lobby_count() + p->num > MAX_PLAYERS || s_started) {
        fprintf(stderr, "red: la máquina %d no entra (llena o ya empezó)\n", pi + 1);
        peer_free(p);
        return;
    }
    p->hello = 1;
    printf("red: se unió la máquina %d con %d jugador%s\n", pi + 1, p->num, p->num > 1 ? "es" : "");
    send_lobby();
}

static void on_start(Rd *r)
{
    NetStart *s = &s_start;
    s->seed = r_u32(r);
    s->epoch = (long long)r_u64(r);
    s->diff = (int)r_u8(r);
    s->mode = (int)r_u8(r);
    s->terror = (int)r_u8(r);
    s->delay = (int)r_u8(r);
    s->me = (int)r_u8(r);
    s->num_players = (int)r_u8(r);
    for (int i = 0; i < s->num_players && i < MAX_PLAYERS; i++) {
        s->owner[i] = (int)r_u8(r);
        s->cls[i] = (int)r_u8(r);
        uint32_t n = r_u32(r);
        const uint8_t *d = r_bytes(r, n);
        s->save[i] = malloc((size_t)n + 1);
        if (s->save[i] && d) {
            memcpy(s->save[i], d, n);
            s->save[i][n] = '\0';
            s->save_len[i] = n;
        }
    }
    if (r->bad) {
        fail(tr("El anfitrión mandó un comienzo que no se entiende"));
        return;
    }
    s_started = 1;
    printf("red: empieza la partida (%d jugadores, esta máquina es la %d)\n", s->num_players, s->me + 1);
}

static void on_message(int pi, int type, Rd *r)
{
    Peer *p = &s_peer[pi];
    p->seen = time(NULL);
    switch (type) {
    case M_HELLO:
        if (s_role == NET_HOST && !p->hello)
            on_hello(pi, r);
        break;
    case M_LOBBY:
        if (s_role == NET_CLIENT) {
            s_lobby_n = (int)r_u8(r);
            for (int i = 0; i < s_lobby_n && i < MAX_PLAYERS; i++) {
                s_lobby_owner[i] = (int)r_u8(r);
                s_lobby_cls[i] = (int)r_u8(r);
                s_lobby_level[i] = (int)r_u8(r);
                s_lobby_diff[i] = (int)r_u8(r);
            }
        }
        break;
    case M_START:
        if (s_role == NET_CLIENT && !s_started)
            on_start(r);
        break;
    case M_INPUT:
        if (s_role == NET_HOST && p->hello) {
            uint32_t tick = r_u32(r);
            int n = (int)r_u8(r);
            const uint8_t *d = r_bytes(r, (size_t)n * sizeof(PlayerInput));
            if (d && n <= MAX_LOCAL) {
                PlayerInput in[MAX_LOCAL];
                memcpy(in, d, (size_t)n * sizeof *in);
                host_input(pi, tick, in, n);
            }
        }
        break;
    case M_FRAME:
        if (s_role == NET_CLIENT) {
            uint32_t tick = r_u32(r);
            int n = (int)r_u8(r);
            const uint8_t *d = r_bytes(r, (size_t)n * sizeof(PlayerInput));
            if (d && n <= MAX_PLAYERS) {
                PlayerInput all[MAX_PLAYERS];
                memcpy(all, d, (size_t)n * sizeof(PlayerInput));
                put_frame(tick, all, n);
            }
        }
        break;
    case M_HASH:
        if (s_role == NET_HOST) {
            uint32_t tick = r_u32(r);
            uint64_t h = r_u64(r);
            int k = (int)((tick / 25u) % NET_HRING);
            p->hash_tick[k] = tick;
            p->hash_val[k] = h;
            p->hash_have[k] = 1;
            check_hash(pi);
        }
        break;
    case M_ERROR:
        if (s_role == NET_CLIENT) {
            char msg[160];
            size_t n = r->len - r->pos < sizeof msg - 1 ? r->len - r->pos : sizeof msg - 1;
            memcpy(msg, r->d + r->pos, n);
            msg[n] = '\0';
            fail(msg);
        }
        break;
    default:
        break;
    }
}

static void read_peer(int pi)
{
    Peer *p = &s_peer[pi];
    for (;;) {
        uint8_t buf[16384];
        ssize_t k = recv(p->fd, buf, sizeof buf, 0);
        if (k > 0) {
            buf_add(&p->in, &p->in_len, &p->in_cap, buf, (size_t)k);
            continue;
        }
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            break;
        /* se cortó */
        char msg[96];
        if (s_role == NET_CLIENT)
            snprintf(msg, sizeof msg, "%s", tr("Se cortó la conexión con el anfitrión"));
        else
            snprintf(msg, sizeof msg, tr("Se fue la máquina %d"), pi + 1);
        int was = p->hello;
        close(p->fd);
        p->fd = -1;
        if (s_role == NET_HOST && !s_started) { /* (en el lobby se va y listo) */
            peer_free(p);
            if (was)
                send_lobby();
            return;
        }
        fail(msg);
        return;
    }
    size_t pos = 0;
    while (p->in_len - pos >= 5) {
        const uint8_t *b = p->in + pos;
        uint32_t n = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        if (n < 5 || n > (64u << 20)) {
            fail(tr("Llegó algo que no se entiende"));
            return;
        }
        if (p->in_len - pos < n)
            break;
        Rd r = { b + 5, n - 5, 0, 0 };
        on_message(pi, b[4], &r);
        if (!s_peer[pi].used && s_role == NET_HOST) /* (lo echó on_hello) */
            return;
        pos += n;
    }
    if (pos) {
        memmove(p->in, p->in + pos, p->in_len - pos);
        p->in_len -= pos;
    }
}

void net_poll(void)
{
    if (s_role == NET_OFF)
        return;
    if (s_role == NET_HOST && s_listen >= 0 && !s_started) {
        for (;;) {
            int fd = accept(s_listen, NULL, NULL);
            if (fd < 0)
                break;
            int slot = -1;
            for (int i = 1; i < NET_MAX_PEERS && slot < 0; i++)
                if (!s_peer[i].used)
                    slot = i;
            if (slot < 0) {
                close(fd);
                continue;
            }
            set_socket(fd);
            Peer *p = &s_peer[slot];
            memset(p, 0, sizeof *p);
            p->used = 1;
            p->fd = fd;
            p->seen = time(NULL);
            if (slot >= s_npeers)
                s_npeers = slot + 1;
        }
    }
    if (s_role == NET_HOST && s_udp >= 0 && !s_started) { /* "¿hay partidas?" */
        for (;;) {
            char q[16];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            ssize_t k = recvfrom(s_udp, q, sizeof q, 0, (struct sockaddr *)&from, &fl);
            if (k <= 0)
                break;
            if (k >= 5 && memcmp(q, "D2PN?", 5) == 0) {
                char a[16];
                int n = snprintf(a, sizeof a, "D2PN!%d %d", lobby_count(), s_host_mode);
                sendto(s_udp, a, (size_t)n, 0, (struct sockaddr *)&from, fl);
            }
        }
    }
    for (int i = 0; i < s_npeers; i++) {
        Peer *p = &s_peer[i];
        if (!p->used || p->fd < 0)
            continue;
        read_peer(i);
        if (s_has_err)
            break;
        if (s_peer[i].used && s_peer[i].fd >= 0 && time(NULL) - s_peer[i].seen > NET_TIMEOUT && s_started) {
            char msg[96];
            snprintf(msg, sizeof msg, tr(s_role == NET_CLIENT ? "El anfitrión no contesta" : "La máquina %d no contesta"), i + 1);
            fail(msg);
        }
    }
    build_frames();
    flush_all();
}

int net_role(void)
{
    return s_role;
}

int net_lobby(int *cls, int *level, int *owner, int *diffmax, int max)
{
    int n = 0;
    if (s_role == NET_HOST) {
        for (int i = 0; i < s_npeers; i++)
            if (s_peer[i].used && (i == 0 || s_peer[i].hello))
                for (int k = 0; k < s_peer[i].num && n < max; k++, n++) {
                    cls[n] = s_peer[i].cls[k];
                    level[n] = s_peer[i].level[k];
                    owner[n] = i;
                    if (diffmax)
                        diffmax[n] = s_peer[i].diffmax[k];
                }
        return n;
    }
    for (; n < s_lobby_n && n < max; n++) {
        cls[n] = s_lobby_cls[n];
        level[n] = s_lobby_level[n];
        owner[n] = s_lobby_owner[n];
        if (diffmax)
            diffmax[n] = s_lobby_diff[n];
    }
    return n;
}

int net_start(int diff, int mode, int terror, uint32_t seed, long long epoch)
{
    if (s_role != NET_HOST || s_started)
        return -1;
    NetStart *s = &s_start;
    s->seed = seed;
    s->epoch = epoch;
    s->diff = diff;
    s->mode = mode;
    s->terror = terror;
    s->delay = NET_DELAY;
    s->num_players = 0;
    for (int i = 0; i < s_npeers; i++) {
        Peer *p = &s_peer[i];
        if (!p->used || (i > 0 && !p->hello))
            continue;
        p->first = s->num_players;
        for (int k = 0; k < p->num && s->num_players < MAX_PLAYERS; k++) {
            int j = s->num_players++;
            s->owner[j] = i;
            s->cls[j] = p->cls[k];
            s->save_len[j] = p->save_len[k];
            s->save[j] = malloc(p->save_len[k] + 1);
            if (s->save[j]) {
                memcpy(s->save[j], p->save[k] ? p->save[k] : "", p->save_len[k]);
                s->save[j][p->save_len[k]] = '\0';
            }
        }
    }
    for (int i = 1; i < s_npeers; i++) { /* a cada una, con su número */
        Peer *p = &s_peer[i];
        if (!p->used || !p->hello || p->fd < 0)
            continue;
        Msg m;
        m_begin(&m, M_START);
        m_u32(&m, s->seed);
        m_u64(&m, (uint64_t)s->epoch);
        m_u8(&m, (unsigned)s->diff);
        m_u8(&m, (unsigned)s->mode);
        m_u8(&m, (unsigned)s->terror);
        m_u8(&m, (unsigned)s->delay);
        m_u8(&m, (unsigned)i);
        m_u8(&m, (unsigned)s->num_players);
        for (int j = 0; j < s->num_players; j++) {
            m_u8(&m, (unsigned)s->owner[j]);
            m_u8(&m, (unsigned)s->cls[j]);
            m_u32(&m, (uint32_t)s->save_len[j]);
            m_bytes(&m, s->save[j], s->save_len[j]);
        }
        send_to(p, &m);
        free(m.d);
    }
    s->me = 0;
    s_started = 1;
    if (s_listen >= 0) { /* ya no entra nadie */
        close(s_listen);
        s_listen = -1;
    }
    build_frames(); /* (los primeros ticks, quietos) */
    flush_all();
    printf("red: empieza la partida con %d jugadores en %d máquinas\n", s->num_players, s_npeers);
    return 0;
}

const NetStart *net_started(void)
{
    return s_started ? &s_start : NULL;
}

const char *net_error(void)
{
    return s_has_err ? s_err : NULL;
}

void net_close(void)
{
    flush_all();
    if (s_listen >= 0)
        close(s_listen);
    if (s_udp >= 0)
        close(s_udp);
    s_listen = s_udp = -1;
    for (int i = 0; i < NET_MAX_PEERS; i++)
        peer_free(&s_peer[i]);
    s_npeers = 0;
    s_role = NET_OFF;
    s_started = 0;
}

/* ---- Buscar partidas ---- */

static char s_found[8][64];
static int  s_found_players[8], s_found_mode[8], s_nfound;

/* La pregunta a toda la red; la primera vez abre el socket y empieza la lista (después la lista
   se mantiene: preguntar de nuevo solo agrega o actualiza) */
void net_discover_start(void)
{
    if (s_disc < 0) {
        s_nfound = 0;
        s_disc = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_disc < 0)
            return;
        int one = 1;
        setsockopt(s_disc, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        fcntl(s_disc, F_SETFL, fcntl(s_disc, F_GETFL, 0) | O_NONBLOCK);
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(NET_PORT);
    a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(s_disc, "D2PN?", 5, 0, (struct sockaddr *)&a, sizeof a);
}

int net_discover_poll(char (*hosts)[64], int *players, int *modes, int max)
{
    while (s_disc >= 0) {
        char a[32];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t k = recvfrom(s_disc, a, sizeof a - 1, 0, (struct sockaddr *)&from, &fl);
        if (k <= 0)
            break;
        a[k] = '\0';
        if (k < 5 || memcmp(a, "D2PN!", 5) != 0)
            continue;
        char ip[64];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        int at = 0; /* "D2PN!jugadores modo": una nueva, o la misma con los jugadores de ahora */
        while (at < s_nfound && strcmp(s_found[at], ip) != 0)
            at++;
        if (at == 8)
            continue;
        if (at == s_nfound)
            snprintf(s_found[s_nfound++], sizeof s_found[0], "%s", ip);
        s_found_players[at] = s_found_mode[at] = 0;
        sscanf(a + 5, "%d %d", &s_found_players[at], &s_found_mode[at]);
    }
    int n = s_nfound < max ? s_nfound : max;
    for (int i = 0; i < n; i++) {
        snprintf(hosts[i], 64, "%.63s", s_found[i]);
        players[i] = s_found_players[i];
        modes[i] = s_found_mode[i];
    }
    return n;
}

void net_discover_stop(void)
{
    if (s_disc >= 0)
        close(s_disc);
    s_disc = -1;
}

const char *net_my_ip(void)
{
    static char ip[64];
    ip[0] = '\0';
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0)
        return ip;
    for (struct ifaddrs *i = list; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && strcmp(i->ifa_name, "lo") != 0) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, sizeof ip);
            break;
        }
    freeifaddrs(list);
    return ip;
}

#endif
