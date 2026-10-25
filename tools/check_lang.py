#!/usr/bin/env python3
"""Revisa la tabla de textos en inglés (src/lang_en.inc):

- que cada traducción tenga los mismos formatos (%d, %s, %.1f...) en el
  mismo orden que el texto en español (si no, snprintf lee mal los
  argumentos);
- que no haya textos repetidos;
- que cada tr("...") del código tenga su traducción, y también los textos
  que se traducen adentro de otra función (quest_banner, show_message,
  note, menu_net_failed) y los que se dibujan en español (font_draw...,
  draw_banner_line, text_in: los traduce lang_screen al dibujarlos).

Uso: tools/check_lang.py [raíz del proyecto]. Sale con 1 si hay errores.
"""
import os
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..')
src = os.path.join(root, 'src')
LIT = r'"((?:[^"\\\n]|\\.)*)"'
SPEC = re.compile(r'%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfgGcspn%])')


def specs(s):
    out = []
    for m in SPEC.finditer(s):
        conv = m.group(5)
        if conv == '%':
            continue
        kind = 'f' if conv in 'eEfgG' else 's' if conv == 's' else 'c' if conv == 'c' else 'd'
        out.append((m.group(4) or '') + kind)
    return out


text = open(os.path.join(src, 'lang_en.inc'), encoding='utf-8').read()
text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
pairs = re.findall(r'\{\s*' + LIT + r'\s*,\s*((?:' + LIT + r'\s*)+)\}', text)
errors = 0
seen = {}
for es, en_raw, _ in pairs:
    en = ''.join(re.findall(LIT, en_raw))
    if es in seen:
        print(f'repetido: "{es}"')
        errors += 1
    seen[es] = en
    if specs(es) != specs(en):
        print(f'formatos distintos: "{es}" {specs(es)} -> "{en}" {specs(en)}')
        errors += 1

def call_args(s, start):
    """Los argumentos (texto) de la llamada que abre en s[start] == '(' (a nivel 0, respetando comillas)"""
    args, depth, cur, i = [], 0, [], start + 1
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c in '([':
            depth += 1
        elif c in ')]':
            if depth == 0:
                args.append(''.join(cur))
                return args
            depth -= 1
        elif c == ',' and depth == 0:
            args.append(''.join(cur))
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return args


def top_literals(arg):
    """Las comillas del argumento que no están adentro de otra llamada (str_get("clave") es del juego)"""
    out, depth, i = [], 0, 0
    while i < len(arg):
        c = arg[i]
        if c == '"':
            m = re.match(LIT, arg[i:])
            if m:
                if depth == 0:
                    out.append(m.group(1))
                i += m.end()
                continue
        depth += c == '('
        depth -= c == ')'
        i += 1
    return out


# función -> qué argumento es el texto (las que llaman a tr adentro)
TRANSLATING = {'quest_banner': 0, 'show_message': 1, 'note': 0, 'menu_net_failed': 0}
# las que dibujan (lang_screen): el argumento del texto
DRAWING = {'font_draw': 2, 'font_draw_centered': 2, 'font_draw_right': 2, 'draw_banner_line': 3, 'text_in': 4, 'eco_note': 2}
SPANISH = re.compile(r'[áéíóúñ¿¡ÁÉÍÓÚÑ]|\b(el|la|los|las|del|que|con|para|por|una|sin|más|tus?|sus?|se|al|ya|'
                     r'tenés|podés|hablá|volvé|está|hay)\b')
missing = []
for name in sorted(os.listdir(src)):
    if not name.endswith(('.c', '.inc')) or name == 'lang.c':
        continue
    body = open(os.path.join(src, name), encoding='utf-8').read()
    for n, line in enumerate(body.split('\n'), 1):
        for m in re.finditer(r'\btr\(' + LIT + r'\)', line):
            if m.group(1) not in seen:
                missing.append(f'{name}:{n}: "{m.group(1)}"')
    for fn, k in list(TRANSLATING.items()) + list(DRAWING.items()):
        for m in re.finditer(r'\b' + fn + r'\s*\(', body):
            args = call_args(body, m.end() - 1)
            if k >= len(args) or 'tr(' in args[k]:
                continue
            for lit in top_literals(args[k]):
                if lit in seen or (fn in DRAWING and not SPANISH.search(lit)):
                    continue
                missing.append(f'{name}:{body.count(chr(10), 0, m.start()) + 1}: "{lit}" ({fn})')
for m in missing:
    print('sin traducción:', m)
print(f'{len(pairs)} textos, {errors} errores, {len(missing)} sin traducción')
sys.exit(1 if errors or missing else 0)
