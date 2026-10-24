#ifndef LANG_H
#define LANG_H

/* El idioma del juego: los trece del Resurrected. Lo elige el importador
   (anota el código, "esMX", "deDE"..., en idioma.txt, al lado de data/; los
   paquetes viejos dicen "es" o "en"), el menú principal (opciones.txt, que
   gana) o D2_LANG.

   Los textos del Resurrected salen de sus .json en ese idioma (strings_load);
   los del motor están escritos en español y tr() da su traducción (las
   tablas lang_*.inc; lo que falta en una, en inglés). Lo que se dibuja pasa
   por lang_screen(), así los textos fijos de las tablas del motor salen
   traducidos sin tocar cada uso. Las voces y el sonido de las cinemáticas
   son los del idioma que se importó (lang_installed). */

enum {
    LANG_ES,    /* español de Latinoamérica (esMX): el de los textos del motor */
    LANG_EN,
    LANG_ES_ES, /* español de España */
    LANG_DE,
    LANG_FR,
    LANG_IT,
    LANG_PT,    /* portugués de Brasil */
    LANG_PL,
    LANG_RU,
    LANG_JA,
    LANG_KO,
    LANG_ZH_CN, /* chino simplificado */
    LANG_ZH_TW, /* chino tradicional */
    LANG_COUNT
};

void        lang_init(const char *pref); /* pref: el código elegido en el menú (NULL o "" = el del importador) */
int         lang_current(void);
int         lang_en(void);               /* 1 si el juego va en inglés */
int         lang_find(const char *code); /* "deDE" (o "es"/"en") -> LANG_*; -1 = no está */
const char *lang_code(int lang);         /* "esMX", "enUS", "deDE"... (los de los .json) */
const char *lang_name(int lang);         /* en su idioma: "Deutsch", "日本語" */
const char *lang_strings_code(void);     /* el del idioma actual */
const char *lang_installed(void);        /* el que se importó (sus voces); "" = no se sabe */
const char *lang_font_dir(void);         /* las fuentes del original: "latin" o "latin2" (polaco) */
const char *lang_ttf(void);              /* la .ttf del Resurrected para lo que no está en ellas */
const char *tr(const char *es);          /* el texto del motor en el idioma del juego */
const char *lang_screen(const char *s);  /* lo mismo para lo que se dibuja: lo que no está queda igual */

#endif
