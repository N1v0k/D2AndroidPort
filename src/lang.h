#ifndef LANG_H
#define LANG_H

/* El idioma del juego: español o inglés. Lo elige el importador (anota "es"
   o "en" en idioma.txt, al lado de data/) y se puede cambiar con D2_LANG.

   Los textos del Resurrected salen de sus .json en ese idioma (strings_load);
   los del motor están escritos en español y tr() da su traducción (la
   tabla de lang_en.inc). Lo que se dibuja pasa por lang_screen(), así los
   textos fijos de las tablas del motor salen traducidos sin tocar cada uso. */

void        lang_init(void);
int         lang_en(void);           /* 1 si el juego va en inglés */
const char *lang_strings_code(void); /* "esMX" o "enUS", el idioma de los .json */
const char *tr(const char *es);      /* el texto del motor en el idioma del juego */
const char *lang_screen(const char *s); /* lo mismo para lo que se dibuja: lo que no está queda igual */

#endif
