#include "formats.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static unsigned col_hash(const char *s)
{
    unsigned h = 2166136261u; /* FNV-1a sin mayúsculas (los nombres se comparan con strcasecmp) */
    for (; *s; s++)
        h = (h ^ (unsigned)tolower((unsigned char)*s)) * 16777619u;
    return h;
}

/* El índice de los nombres de columna: buscar una columna era comparar con
   todas (monstats.txt tiene casi 300) y se busca miles de veces al armar
   una zona. Con nombres repetidos queda la primera, como antes. */
static void build_col_hash(TxtTable *t)
{
    if (t->rows < 1)
        return; /* (sin la fila de los nombres) */
    int size = 16;
    while (size < t->cols * 2)
        size *= 2;
    t->col_hash = calloc((size_t)size, sizeof *t->col_hash);
    if (!t->col_hash)
        return; /* (sin índice se busca como antes) */
    t->col_mask = size - 1;
    for (int c = 0; c < t->cols; c++) {
        if (!t->cells[c])
            continue;
        unsigned i = col_hash(t->cells[c]) & (unsigned)t->col_mask;
        while (t->col_hash[i] && strcasecmp(t->cells[t->col_hash[i] - 1], t->cells[c]) != 0)
            i = (i + 1) & (unsigned)t->col_mask;
        if (!t->col_hash[i])
            t->col_hash[i] = c + 1;
    }
}

/* Tablas de data/global/excel: texto separado por tabulaciones, la primera
   fila tiene los nombres de las columnas. */
int txt_load(const uint8_t *data, size_t size, TxtTable *t)
{
    memset(t, 0, sizeof *t);
    t->buf = malloc(size + 1);
    if (!t->buf)
        return -1;
    memcpy(t->buf, data, size);
    t->buf[size] = '\0';

    /* Contar filas y columnas (las de la primera fila mandan) */
    int rows = 0, cols = 1;
    for (size_t i = 0; i < size; i++) {
        if (rows == 0 && t->buf[i] == '\t')
            cols++;
        if (t->buf[i] == '\n')
            rows++;
    }
    if (size && t->buf[size - 1] != '\n')
        rows++;
    t->rows = rows;
    t->cols = cols;
    t->cells = calloc((size_t)rows * (size_t)cols, sizeof *t->cells);
    if (!t->cells) {
        txt_free(t);
        return -1;
    }

    char *p = t->buf;
    for (int r = 0; r < rows && *p; r++) {
        char *end = strchr(p, '\n');
        if (end)
            *end = '\0';
        size_t len = strlen(p);
        if (len && p[len - 1] == '\r')
            p[len - 1] = '\0';
        for (int c = 0; c < cols; c++) {
            t->cells[r * cols + c] = p;
            char *tab = strchr(p, '\t');
            if (!tab) {
                for (c++; c < cols; c++)
                    t->cells[r * cols + c] = p + strlen(p); /* celdas vacías al final */
                break;
            }
            *tab = '\0';
            p = tab + 1;
        }
        if (!end)
            break;
        p = end + 1;
    }
    build_col_hash(t);
    return 0;
}

int txt_col(const TxtTable *t, const char *name)
{
    if (t->col_hash) {
        for (unsigned i = col_hash(name) & (unsigned)t->col_mask; t->col_hash[i]; i = (i + 1) & (unsigned)t->col_mask)
            if (strcasecmp(t->cells[t->col_hash[i] - 1], name) == 0)
                return t->col_hash[i] - 1;
        return -1;
    }
    for (int c = 0; c < t->cols; c++)
        if (t->cells[c] && strcasecmp(t->cells[c], name) == 0)
            return c;
    return -1;
}

int txt_find_row(const TxtTable *t, int col, const char *value)
{
    if (col < 0)
        return -1;
    for (int r = 1; r < t->rows; r++)
        if (t->cells[r * t->cols + col] && strcasecmp(t->cells[r * t->cols + col], value) == 0)
            return r;
    return -1;
}

const char *txt_get(const TxtTable *t, int row, int col)
{
    if (row < 0 || row >= t->rows || col < 0 || col >= t->cols || !t->cells[row * t->cols + col])
        return "";
    return t->cells[row * t->cols + col];
}

int txt_get_int(const TxtTable *t, int row, const char *col_name, int fallback)
{
    const char *s = txt_get(t, row, txt_col(t, col_name));
    return *s ? atoi(s) : fallback;
}

void txt_free(TxtTable *t)
{
    free(t->cells);
    free(t->buf);
    free(t->col_hash);
    memset(t, 0, sizeof *t);
}
