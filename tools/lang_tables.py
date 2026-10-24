#!/usr/bin/env python3
"""Las tablas de los textos del motor en los otros idiomas (src/lang_*.inc).

La lista maestra es src/lang_en.inc (cada texto en español con su inglés).
Las traducciones se escriben en tools/lang/<idioma>.txt, una por renglón:

    número<TAB>traducción

con el número del texto en la lista (el que da `lista`). Este script arma
src/lang_<idioma>.inc con los mismos grupos que lang_en.inc y revisa que
cada traducción tenga los mismos formatos (%d, %s...) que el español.

Uso:
  tools/lang_tables.py lista [desde] [hasta]   la lista numerada (español | inglés)
  tools/lang_tables.py armar                   arma todas las tablas (sale con 1 si hay errores)
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SRC = os.path.join(ROOT, 'src')
LANG_DIR = os.path.join(ROOT, 'tools', 'lang')
LIT = r'"((?:[^"\\\n]|\\.)*)"'
SPEC = re.compile(r'%[-+ #0]*(\d+|\*)?(\.(\d+|\*))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfgGcspn%])')
# idioma -> (archivo .inc, nombre)
LANGS = {
    'es_es': 'español de España', 'de': 'alemán', 'fr': 'francés', 'it': 'italiano', 'pt': 'portugués de Brasil',
    'pl': 'polaco', 'ru': 'ruso', 'ja': 'japonés', 'ko': 'coreano', 'zh_cn': 'chino simplificado',
    'zh_tw': 'chino tradicional',
}


def specs(s):
    out = []
    for m in SPEC.finditer(s):
        conv = m.group(5)
        if conv == '%':
            continue
        kind = 'f' if conv in 'eEfgG' else 's' if conv == 's' else 'c' if conv == 'c' else 'd'
        out.append((m.group(4) or '') + kind)
    return out


def unescape(s):
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), s)


def escape(s):
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def master():
    """[(grupo, español, inglés)]: los textos de lang_en.inc, en orden, con el comentario de su grupo"""
    out, group = [], ''
    text = open(os.path.join(SRC, 'lang_en.inc'), encoding='utf-8').read()
    pos = 0
    token = re.compile(r'/\*.*?\*/|\{\s*' + LIT + r'\s*,\s*((?:' + LIT + r'\s*)+)\}', re.S)
    for m in token.finditer(text):
        if m.group(0).startswith('/*'):
            if pos > 0:  # (el primero es la explicación del archivo)
                group = m.group(0)
            pos += 1
            continue
        pos += 1
        en = ''.join(re.findall(LIT, m.group(2)))
        out.append((group, unescape(m.group(1)), unescape(en)))
    return out


def cmd_list(a, b):
    for i, (group, es, en) in enumerate(master(), 1):
        if a <= i <= b:
            print(f'{i}\t{escape(es)}\t{escape(en)}')


def cmd_build():
    rows = master()
    errors = 0
    for lang, name in LANGS.items():
        path = os.path.join(LANG_DIR, lang + '.txt')
        tr = {}
        if os.path.exists(path):
            for n, line in enumerate(open(path, encoding='utf-8'), 1):
                line = line.rstrip('\n')
                if not line.strip() or line.startswith('#'):
                    continue
                num, _, text = line.partition('\t')
                if not num.strip().isdigit():
                    print(f'{lang}.txt:{n}: sin número')
                    errors += 1
                    continue
                tr[int(num)] = unescape(text)
        out = [f'/* Los textos del motor en {name}: {{ "español", "traducción" }}. Se arma con\n'
               f'   tools/lang_tables.py desde tools/lang/{lang}.txt (lo que falta sale en inglés). */\n']
        group = None
        done = 0
        for i, (g, es, en) in enumerate(rows, 1):
            if i not in tr:
                continue
            t = tr[i]
            if specs(t) != specs(es):
                print(f'{lang}: formatos distintos en {i}: "{es}" {specs(es)} -> "{t}" {specs(t)}')
                errors += 1
                continue
            if g != group:
                out.append('\n' + g + '\n')
                group = g
            out.append(f'{{ "{escape(es)}", "{escape(t)}" }},\n')
            done += 1
        extra = [k for k in tr if k < 1 or k > len(rows)]
        for k in extra:
            print(f'{lang}: el número {k} no está en la lista')
            errors += 1
        open(os.path.join(SRC, f'lang_{lang}.inc'), 'w', encoding='utf-8', newline='\n').write(''.join(out))
        print(f'{lang}: {done} de {len(rows)}')
    return errors


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'lista':
        a = int(sys.argv[2]) if len(sys.argv) > 2 else 1
        b = int(sys.argv[3]) if len(sys.argv) > 3 else 10 ** 9
        cmd_list(a, b)
    elif len(sys.argv) > 1 and sys.argv[1] == 'armar':
        sys.exit(1 if cmd_build() else 0)
    else:
        print(__doc__)
