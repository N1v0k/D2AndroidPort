/*
 * d2fmt: prueba los lectores de formatos y vuelca los gráficos a BMP.
 *
 *   d2fmt dcc <archivo.dcc> <pal.dat> <salida.bmp> [dirección]
 *   d2fmt dc6 <archivo.dc6> <pal.dat> <salida.bmp>
 *   d2fmt dt1 <archivo.dt1> <pal.dat> <salida.bmp>
 *   d2fmt ds1 <archivo.ds1>
 *   d2fmt cof <archivo.cof>
 *   d2fmt flac <archivo.flac> <salida.raw> [stream]
 *   d2fmt raw <carpeta> <archivo.dcc>...   (los cuadros en crudo)
 */
#include "../src/fmt/flac.h"
#include "../src/fmt/formats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "no pude abrir %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static void put16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static int write_bmp8(const char *path, const uint8_t *px, int w, int h, const uint32_t pal[256])
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int stride = (w + 3) & ~3;
    uint32_t data_size = (uint32_t)(stride * h), off = 14 + 40 + 1024;
    fputc('B', f); fputc('M', f);
    put32(f, off + data_size); put32(f, 0); put32(f, off);
    put32(f, 40); put32(f, (uint32_t)w); put32(f, (uint32_t)h); put16(f, 1); put16(f, 8);
    put32(f, 0); put32(f, data_size); put32(f, 2835); put32(f, 2835); put32(f, 256); put32(f, 0);
    for (int i = 0; i < 256; i++) {
        fputc(pal[i] & 0xFF, f);
        fputc((pal[i] >> 8) & 0xFF, f);
        fputc((pal[i] >> 16) & 0xFF, f);
        fputc(0, f);
    }
    static const uint8_t pad[3] = { 0 };
    for (int y = h - 1; y >= 0; y--) {
        fwrite(px + (size_t)y * w, 1, (size_t)w, f);
        fwrite(pad, 1, (size_t)(stride - w), f);
    }
    fclose(f);
    printf("imagen: %s (%dx%d)\n", path, w, h);
    return 0;
}

static void blit(uint8_t *dst, int dw, int dh, int x0, int y0, const uint8_t *src, int sw, int sh)
{
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            int dx = x0 + x, dy = y0 + y;
            uint8_t c = src[y * sw + x];
            if (c && dx >= 0 && dy >= 0 && dx < dw && dy < dh)
                dst[dy * dw + dx] = c;
        }
}

static int load_pal(const char *path, uint32_t pal[256])
{
    size_t n;
    uint8_t *d = read_file(path, &n);
    int r = d ? pal_load(d, n, pal) : -1;
    free(d);
    if (r != 0)
        fprintf(stderr, "paleta inválida: %s\n", path);
    return r;
}

/* Pone los cuadros en fila respetando sus offsets, como se verían en el juego */
static int sprite_sheet(const Sprite *s, int dir, const uint32_t pal[256], const char *out)
{
    int minx = 0, miny = 0, maxx = 1, maxy = 1;
    for (int f = 0; f < s->frames_per_dir; f++) {
        SpriteFrame *fr = sprite_frame(s, dir, f);
        if (fr->off_x < minx) minx = fr->off_x;
        if (fr->off_y < miny) miny = fr->off_y;
        if (fr->off_x + fr->width > maxx) maxx = fr->off_x + fr->width;
        if (fr->off_y + fr->height > maxy) maxy = fr->off_y + fr->height;
    }
    int cw = maxx - minx + 4, ch = maxy - miny + 4;
    int cols = s->frames_per_dir < 8 ? s->frames_per_dir : 8;
    int rows = (s->frames_per_dir + cols - 1) / cols;
    int w = cw * cols, h = ch * rows;
    uint8_t *img = calloc((size_t)(w * h), 1);
    for (int f = 0; f < s->frames_per_dir; f++) {
        SpriteFrame *fr = sprite_frame(s, dir, f);
        int ox = (f % cols) * cw - minx + 2, oy = (f / cols) * ch - miny + 2;
        blit(img, w, h, ox + fr->off_x, oy + fr->off_y, fr->pixels, fr->width, fr->height);
    }
    int r = write_bmp8(out, img, w, h, pal);
    free(img);
    return r;
}

static int cmd_sprite(int is_dcc, int argc, char **argv)
{
    if (argc < 5)
        return 2;
    uint32_t pal[256];
    if (load_pal(argv[3], pal) != 0)
        return 1;
    size_t n;
    uint8_t *d = read_file(argv[2], &n);
    if (!d)
        return 1;
    Sprite s;
    int r = is_dcc ? dcc_load(d, n, &s) : dc6_load(d, n, &s);
    free(d);
    if (r != 0) {
        fprintf(stderr, "no se pudo decodificar %s\n", argv[2]);
        return 1;
    }
    int dir = argc > 5 ? atoi(argv[5]) : 0;
    printf("%d direcciones x %d cuadros\n", s.directions, s.frames_per_dir);
    SpriteFrame *f0 = sprite_frame(&s, dir < s.directions ? dir : 0, 0);
    printf("cuadro 0: %dx%d, offset (%d, %d)\n", f0->width, f0->height, f0->off_x, f0->off_y);
    r = sprite_sheet(&s, dir < s.directions ? dir : 0, pal, argv[4]);
    sprite_free(&s);
    return r;
}

/* Cuadro 0 de cada dirección en una fila, para ver en qué orden están */
static int cmd_dirs(int argc, char **argv)
{
    if (argc < 5)
        return 2;
    uint32_t pal[256];
    if (load_pal(argv[3], pal) != 0)
        return 1;
    size_t n;
    uint8_t *d = read_file(argv[2], &n);
    if (!d)
        return 1;
    Sprite s;
    int r = dcc_load(d, n, &s);
    free(d);
    if (r != 0) {
        fprintf(stderr, "no se pudo decodificar %s\n", argv[2]);
        return 1;
    }
    int minx = 0, miny = 0, maxx = 1, maxy = 1;
    for (int dir = 0; dir < s.directions; dir++) {
        SpriteFrame *fr = sprite_frame(&s, dir, 0);
        if (fr->off_x < minx) minx = fr->off_x;
        if (fr->off_y < miny) miny = fr->off_y;
        if (fr->off_x + fr->width > maxx) maxx = fr->off_x + fr->width;
        if (fr->off_y + fr->height > maxy) maxy = fr->off_y + fr->height;
    }
    int cw = maxx - minx + 6, ch = maxy - miny + 6, w = cw * s.directions;
    uint8_t *img = calloc((size_t)w * (size_t)ch, 1);
    for (int dir = 0; dir < s.directions; dir++) {
        SpriteFrame *fr = sprite_frame(&s, dir, 0);
        blit(img, w, ch, dir * cw - minx + 3 + fr->off_x, -miny + 3 + fr->off_y, fr->pixels, fr->width, fr->height);
    }
    r = write_bmp8(argv[4], img, w, ch, pal);
    free(img);
    sprite_free(&s);
    return r;
}

static int cmd_dt1(int argc, char **argv)
{
    if (argc < 5)
        return 2;
    uint32_t pal[256];
    if (load_pal(argv[3], pal) != 0)
        return 1;
    size_t n;
    uint8_t *d = read_file(argv[2], &n);
    if (!d)
        return 1;
    Dt1 t;
    int r = dt1_load(d, n, &t);
    free(d);
    if (r != 0) {
        fprintf(stderr, "no se pudo decodificar %s\n", argv[2]);
        return 1;
    }
    printf("%d tiles\n", t.num_tiles);
    int maxh = 80;
    for (int i = 0; i < t.num_tiles; i++) {
        Dt1Tile *ti = &t.tiles[i];
        printf("  #%-3d ori %-2d main %-2d sub %-2d rareza %-3d alto %-4d img_y %-4d bloques %dx%d\n", i,
               ti->orientation, ti->main_index, ti->sub_index, ti->rarity, ti->height, ti->img_y,
               ti->img_w, ti->img_h);
        if (ti->img_h > maxh)
            maxh = ti->img_h;
        if (getenv("D2FMT_FLAGS") && ti->orientation <= 9) {
            /* Marcas de colisión, ya con las filas invertidas como las usa el motor */
            for (int y = 0; y < 5; y++) {
                printf("        ");
                for (int x = 0; x < 5; x++)
                    putchar(ti->subtile_flags[(4 - y) * 5 + x] & 1 ? '#' : '.');
                putchar('\n');
            }
        }
    }
    if (maxh > 4096)
        maxh = 4096;
    int count = t.num_tiles < 0 ? 0 : t.num_tiles > 64 ? 64 : t.num_tiles, cols = 8;
    int cw = 164, ch = maxh + 4, rows = (count + cols - 1) / cols;
    if (rows == 0)
        rows = 1;
    uint8_t *img = calloc((size_t)cw * (size_t)cols * (size_t)ch * (size_t)rows, 1);
    for (int i = 0; i < count; i++) {
        Dt1Tile *ti = &t.tiles[i];
        if (ti->pixels)
            blit(img, cw * cols, ch * rows, (i % cols) * cw + 2, (i / cols) * ch + 2, ti->pixels, ti->img_w, ti->img_h);
    }
    r = write_bmp8(argv[4], img, cw * cols, ch * rows, pal);
    free(img);
    dt1_free(&t);
    return r;
}

static int cmd_ds1(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    size_t n;
    uint8_t *d = read_file(argv[2], &n);
    if (!d)
        return 1;
    Ds1 m;
    int r = ds1_load(d, n, &m);
    free(d);
    if (r != 0) {
        fprintf(stderr, "no se pudo leer %s\n", argv[2]);
        return 1;
    }
    printf("versión %d, %dx%d tiles, acto %d, paredes %d, pisos %d, objetos %d\n", m.version, m.width,
           m.height, m.act, m.num_walls, m.num_floors, m.num_objects);
    for (int i = 0; i < m.num_files; i++)
        printf("  dt1: %s\n", m.files[i]);
    int floors = 0, walls = 0;
    for (int y = 0; y < m.height; y++)
        for (int x = 0; x < m.width; x++) {
            floors += ds1_floor(&m, 0, x, y)->prop1 != 0;
            for (int l = 0; l < m.num_walls; l++)
                walls += ds1_wall(&m, l, x, y)->prop1 != 0;
        }
    printf("celdas con piso: %d, con pared: %d\n", floors, walls);
    for (int y = 0; y < m.height; y++)
        for (int x = 0; x < m.width; x++)
            for (int l = 0; l < m.num_walls; l++) {
                Ds1Cell *c = ds1_wall(&m, l, x, y);
                if (c->prop1 && (c->type == 10 || c->type == 11))
                    printf("  tile especial en (%d, %d): orientación %d estilo %d secuencia %d\n", x, y, c->type,
                           c->style, c->sequence);
            }
    for (int i = 0; i < m.num_objects; i++)
        printf("  objeto tipo %d id %3d en (%d, %d) flags %d\n", m.objects[i].type, m.objects[i].id,
               m.objects[i].x, m.objects[i].y, m.objects[i].flags);
    /* Los grupos de sustitución (archivos de reglas de lvlsub.txt) y lo que
       marca cada celda de su capa 0 de paredes (estilo/secuencia) */
    for (int i = 0; i < m.num_groups; i++) {
        const Ds1Group *g = &m.groups[i];
        printf("  grupo %d: (%d, %d) de %dx%d, %d alternativas\n", i, g->x, g->y, g->w, g->h, g->alts);
        for (int y = g->y; y < g->y + g->h; y++) {
            printf("   ");
            for (int x = g->x; x < g->x + (g->w + 1) * (g->alts + 1) && x < m.width; x++) {
                const Ds1Cell *c = ds1_wall(&m, 0, x, y);
                const Ds1Cell *f = ds1_floor(&m, 0, x, y);
                if (c->prop1 & 1)
                    printf(" %2d/%-2d", c->style, c->sequence);
                else
                    printf("  %s  ", f->prop1 & 2 ? "f2" : " .");
            }
            printf("\n");
        }
    }
    ds1_free(&m);
    return 0;
}

/* "\d2\data\global\tiles\act1\town\floor.tg1" -> "data/global/tiles/act1/town/floor.dt1" */
static void ds1_ref_to_path(const char *ref, char *out, size_t cap)
{
    char low[256];
    size_t i = 0;
    for (; ref[i] && i + 1 < sizeof low; i++)
        low[i] = ref[i] == '\\' ? '/' : (char)(ref[i] >= 'A' && ref[i] <= 'Z' ? ref[i] + 32 : ref[i]);
    low[i] = '\0';
    const char *start = strstr(low, "data/");
    snprintf(out, cap, "%s", start ? start : low);
    size_t n = strlen(out);
    if (n > 4 && out[n - 4] == '.')
        strcpy(out + n - 4, ".dt1");
}

static const Dt1Tile *ds1map_find(Dt1 *dt1, int n, int ori, int main, int sub)
{
    for (int f = 0; f < n; f++)
        for (int i = 0; i < dt1[f].num_tiles; i++) {
            const Dt1Tile *t = &dt1[f].tiles[i];
            if (t->orientation == ori && t->main_index == main && t->sub_index == sub)
                return t;
        }
    return NULL;
}

/* Dibuja un DS1 completo (pisos, paredes y techos) para ver cómo es */
static int cmd_ds1map(int argc, char **argv)
{
    if (argc < 5)
        return 2;
    uint32_t pal[256];
    if (load_pal(argv[3], pal) != 0)
        return 1;
    size_t n;
    uint8_t *d = read_file(argv[2], &n);
    Ds1 m;
    if (!d || ds1_load(d, n, &m) != 0) {
        free(d);
        fprintf(stderr, "no se pudo leer %s\n", argv[2]);
        return 1;
    }
    free(d);

    Dt1 dt1[32];
    int ndt1 = 0;
    for (int i = 0; i < m.num_files && ndt1 < 32; i++) {
        char path[256];
        ds1_ref_to_path(m.files[i], path, sizeof path);
        d = read_file(path, &n);
        if (d && dt1_load(d, n, &dt1[ndt1]) == 0)
            ndt1++;
        free(d);
    }

    /* Tamaño de la imagen: el rombo del mapa más lugar arriba para las paredes */
    int top_pad = 320;
    int w = (m.width + m.height) * 80, h = (m.width + m.height) * 40 + top_pad + 80;
    uint8_t *img = calloc((size_t)w * (size_t)h, 1);
    int ox = m.height * 80, oy = top_pad;

    for (int pass = 0; pass < 3; pass++)
        for (int ty = 0; ty < m.height; ty++)
            for (int tx = 0; tx < m.width; tx++) {
                int sx = (tx - ty) * 80 + ox, sy = (tx + ty) * 40 + oy;
                if (pass == 0) {
                    for (int l = 0; l < m.num_floors; l++) {
                        Ds1Cell *c = ds1_floor(&m, l, tx, ty);
                        const Dt1Tile *t = c->prop1 ? ds1map_find(dt1, ndt1, 0, c->style, c->sequence) : NULL;
                        if (t && t->pixels)
                            blit(img, w, h, sx - 80, sy + t->img_y, t->pixels, t->img_w, t->img_h);
                    }
                    continue;
                }
                for (int l = 0; l < m.num_walls; l++) {
                    Ds1Cell *c = ds1_wall(&m, l, tx, ty);
                    if (!c->prop1 || c->type == 10 || c->type == 11)
                        continue;
                    int roof = c->type == 15;
                    if ((pass == 2) != roof)
                        continue;
                    const Dt1Tile *t = ds1map_find(dt1, ndt1, c->type, c->style, c->sequence);
                    if (c->type == 3) {
                        const Dt1Tile *t4 = ds1map_find(dt1, ndt1, 4, c->style, c->sequence);
                        if (t4 && t4->pixels)
                            blit(img, w, h, sx - 80, sy + 80 + t4->img_y, t4->pixels, t4->img_w, t4->img_h);
                    }
                    if (t && t->pixels)
                        blit(img, w, h, sx - 80, roof ? sy - t->roof_height + t->img_y : sy + 80 + t->img_y,
                             t->pixels, t->img_w, t->img_h);
                }
            }

    printf("%s: %dx%d tiles\n", argv[2], m.width, m.height);
    int r = write_bmp8(argv[4], img, w, h, pal);
    free(img);
    for (int i = 0; i < ndt1; i++)
        dt1_free(&dt1[i]);
    ds1_free(&m);
    return r;
}

static int cmd_cof(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    size_t n;
    uint8_t *d = read_file(argv[2], &n);
    if (!d)
        return 1;
    Cof c;
    int r = cof_load(d, n, &c);
    free(d);
    if (r != 0) {
        fprintf(stderr, "no se pudo leer %s\n", argv[2]);
        return 1;
    }
    printf("%d capas, %d direcciones x %d cuadros\n", c.num_layers, c.directions, c.frames_per_dir);
    for (int i = 0; i < c.num_layers; i++)
        printf("  capa %s, arma %s, sombra %d, transparente %d, efecto %d\n", k_comp_names[c.layers[i].component],
               c.layers[i].weapon_class, c.layers[i].shadow, c.layers[i].transparent, c.layers[i].draw_effect);
    printf("orden de dibujo (cuadro 0), del fondo al frente:\n");
    for (int d = 0; d < c.directions; d++) {
        printf("  dir %2d:", d);
        for (int i = 0; i < c.num_layers; i++) {
            int v = c.priority[(d * c.frames_per_dir) * c.num_layers + i];
            printf(" %s", v < COMP_COUNT ? k_comp_names[v] : "??");
        }
        printf("\n");
    }
    cof_free(&c);
    return 0;
}

/* Decodifica un FLAC a 16 bits intercalados (para comparar con el MD5 que
   trae el archivo). Con "stream" lo lee de a pedazos como la música. */
static int cmd_flac(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    FILE *out = fopen(argv[3], "wb");
    if (!out)
        return 1;
    int frames = 0, channels = 0, rate = 0;
    const uint8_t *md5 = NULL;
    Flac *f = NULL;
    uint8_t *data = NULL;
    if (argc >= 5 && !strcmp(argv[4], "stream")) {
        f = flac_open(argv[2]);
        if (!f) {
            fprintf(stderr, "no pude abrir %s\n", argv[2]);
            fclose(out);
            return 1;
        }
        channels = flac_channels(f);
        rate = flac_rate(f);
        md5 = flac_md5(f);
        static int16_t buf[1000 * 8];
        int n;
        while ((n = flac_read(f, buf, 777)) > 0) {
            fwrite(buf, sizeof(int16_t) * (size_t)channels, (size_t)n, out);
            frames += n;
        }
    } else {
        size_t size;
        data = read_file(argv[2], &size);
        if (!data) {
            fclose(out);
            return 1;
        }
        f = flac_open_mem(data, size);
        md5 = f ? flac_md5(f) : NULL;
        int16_t *pcm = flac_decode(data, size, &frames, &channels, &rate);
        if (!pcm) {
            fprintf(stderr, "no pude decodificar %s\n", argv[2]);
            fclose(out);
            return 1;
        }
        fwrite(pcm, sizeof(int16_t) * (size_t)channels, (size_t)frames, out);
        free(pcm);
    }
    fclose(out);
    printf("%d Hz, %d canales, %d cuadros (dice %llu), md5 ", rate, channels, frames,
           f ? (unsigned long long)flac_total(f) : 0ULL);
    for (int i = 0; md5 && i < 16; i++)
        printf("%02x", md5[i]);
    printf("\n");
    flac_close(f);
    free(data);
    return 0;
}

/* Los cuadros de cada DCC en crudo, en <carpeta>/<nombre>.raw: "D2RW",
   direcciones y cuadros (int32), y por fila (las direcciones en sentido
   horario desde el sur, como los sectores del juego) su dirección en el DCC;
   después cada cuadro: ancho, alto, x e y desde el punto de anclaje (int32)
   y sus índices de paleta (0 = transparente) */
static int cmd_raw(int argc, char **argv)
{
    static const uint8_t k_dcc16[16] = { 4, 8, 0, 9, 5, 10, 1, 11, 6, 12, 2, 13, 7, 14, 3, 15 };
    static const uint8_t k_dcc8[8] = { 4, 0, 5, 1, 6, 2, 7, 3 };
    if (argc < 4)
        return 2;
    int bad = 0;
    for (int a = 3; a < argc; a++) {
        size_t n;
        uint8_t *d = read_file(argv[a], &n);
        Sprite s;
        if (!d || dcc_load(d, n, &s) != 0) {
            free(d);
            fprintf(stderr, "no se pudo decodificar %s\n", argv[a]);
            bad++;
            continue;
        }
        free(d);
        const char *base = strrchr(argv[a], '/');
        base = base ? base + 1 : argv[a];
        int len = (int)strlen(base);
        char path[1024];
        snprintf(path, sizeof path, "%s/%.*s.raw", argv[2], len > 4 ? len - 4 : len, base);
        FILE *f = fopen(path, "wb");
        if (!f) {
            sprite_free(&s);
            bad++;
            continue;
        }
        int32_t hdr[2] = { s.directions, s.frames_per_dir };
        fwrite("D2RW", 1, 4, f);
        fwrite(hdr, sizeof hdr, 1, f);
        for (int r = 0; r < s.directions; r++) {
            int32_t dd = s.directions == 16 ? k_dcc16[r] : s.directions == 8 ? k_dcc8[r] : r;
            fwrite(&dd, sizeof dd, 1, f);
        }
        for (int r = 0; r < s.directions; r++) {
            int dd = s.directions == 16 ? k_dcc16[r] : s.directions == 8 ? k_dcc8[r] : r;
            for (int k = 0; k < s.frames_per_dir; k++) {
                const SpriteFrame *fr = sprite_frame(&s, dd, k);
                int32_t v[4] = { fr->width, fr->height, fr->off_x, fr->off_y };
                fwrite(v, sizeof v, 1, f);
                if (fr->width > 0 && fr->height > 0)
                    fwrite(fr->pixels, 1, (size_t)fr->width * (size_t)fr->height, f);
            }
        }
        fclose(f);
        sprite_free(&s);
    }
    printf("%d archivos, %d con error\n", argc - 3, bad);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    int r = 2;
    if (argc >= 2) {
        if (!strcmp(argv[1], "dcc")) r = cmd_sprite(1, argc, argv);
        else if (!strcmp(argv[1], "dc6")) r = cmd_sprite(0, argc, argv);
        else if (!strcmp(argv[1], "dirs")) r = cmd_dirs(argc, argv);
        else if (!strcmp(argv[1], "dt1")) r = cmd_dt1(argc, argv);
        else if (!strcmp(argv[1], "ds1")) r = cmd_ds1(argc, argv);
        else if (!strcmp(argv[1], "ds1map")) r = cmd_ds1map(argc, argv);
        else if (!strcmp(argv[1], "cof")) r = cmd_cof(argc, argv);
        else if (!strcmp(argv[1], "flac")) r = cmd_flac(argc, argv);
        else if (!strcmp(argv[1], "raw")) r = cmd_raw(argc, argv);
    }
    if (r == 2)
        fprintf(stderr,
                "uso: d2fmt dcc <archivo.dcc> <pal.dat> <salida.bmp> [dirección]\n"
                "     d2fmt dc6 <archivo.dc6> <pal.dat> <salida.bmp>\n"
                "     d2fmt dt1 <archivo.dt1> <pal.dat> <salida.bmp>\n"
                "     d2fmt ds1 <archivo.ds1>\n"
                "     d2fmt cof <archivo.cof>\n"
                "     d2fmt flac <archivo.flac> <salida.raw> [stream]\n"
                "     d2fmt raw <carpeta> <archivo.dcc>...\n");
    return r;
}
