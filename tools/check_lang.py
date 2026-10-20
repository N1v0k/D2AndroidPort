#!/usr/bin/env python3
"""Revisa la tabla de textos en inglés (src/lang_en.inc):

- que cada traducción tenga los mismos formatos (%d, %s, %.1f...) en el
  mismo orden que el texto en español (si no, snprintf lee mal los
  argumentos);
- que no haya textos repetidos;
- que cada tr("...") del código tenga su traducción.

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

missing = []
for name in sorted(os.listdir(src)):
    if not name.endswith(('.c', '.inc')) or name == 'lang.c':
        continue
    for n, line in enumerate(open(os.path.join(src, name), encoding='utf-8'), 1):
        for m in re.finditer(r'\btr\(' + LIT + r'\)', line):
            if m.group(1) not in seen:
                missing.append(f'{name}:{n}: "{m.group(1)}"')
for m in missing:
    print('sin traducción:', m)
print(f'{len(pairs)} textos, {errors} errores, {len(missing)} sin traducción')
sys.exit(1 if errors or missing else 0)
