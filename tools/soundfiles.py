#!/usr/bin/env python3
"""
soundfiles.py: lista los archivos de sonido y música que usa el motor, para
armar el paquete de la Pi sin copiar todos los efectos del juego.

  soundfiles.py <carpeta que tiene data/> especificación...

  NOMBRE          un sonido de sounds.txt (con todo su grupo)
  mon:ID          los de un monstruo de monstats.txt (su fila de monsounds.txt)
  class:ama       los de las habilidades de una clase y sus proyectiles
  items           los de todos los objetos (al caer, al guardarlos y al usarlos)
  missile:NOMBRE  los de un proyectil de missiles.txt (y los de su explosión)
  env:N           música, ambiente y ruidos sueltos de una fila de soundenviron.txt
  vo:NOMBRE       las voces de un NPC del Acto 1 que dice el motor (presentación,
                  charla y las misiones del acto), según vo.json y metadata-audio-vo.json
  vokey:CLAVE     la voz de una sola clave de vo.json (el tomo de la torre)

Escribe una ruta por línea (data/global/sfx/..., data/global/music/... o
data/local/sfx/... para las voces), solo de los archivos que existen.
"""
import csv
import json
import os
import re
import sys


def read_table(root, name):
    path = os.path.join(root, 'data', 'global', 'excel', name + '.txt')
    with open(path, encoding='latin-1', newline='') as f:
        rows = list(csv.reader(f, delimiter='\t', quoting=csv.QUOTE_NONE))
    head = rows[0]
    return [dict(zip(head, r)) for r in rows[1:]]


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    root = sys.argv[1]
    sounds = read_table(root, 'sounds')
    index = {}
    for i, r in enumerate(sounds):
        index.setdefault(r['Sound'].lower(), i)
    files = set()

    def add_sound(name):
        i = index.get(name.strip().lower())
        if i is None:
            return
        size = max(1, int(sounds[i].get('Group Size') or 0))
        for r in sounds[i:i + size]:
            f = r['FileName'].strip()
            if not f or f.lower() == 'none.flac':
                continue
            kind = 'music' if r.get('IsMusic') == '1' else 'sfx'
            where = 'local' if r.get('IsLocal') == '1' else 'global'
            files.add('data/%s/%s/%s' % (where, kind, f.replace('\\', '/').lower()))

    missiles = {r['Missile'].lower(): r for r in read_table(root, 'missiles') if r.get('Missile')}

    def add_missile(name, seen=None):
        seen = seen if seen is not None else set()
        m = missiles.get(name.lower())
        if not m or name.lower() in seen:
            return
        seen.add(name.lower())
        for col in ('TravelSound', 'HitSound', 'ProgSound', 'OnDiedSound'):
            add_sound(m.get(col, ''))
        for col in ('ExplosionMissile', 'SubMissile1', 'SubMissile2', 'SubMissile3', 'HitSubMissile1'):
            if m.get(col):
                add_missile(m[col], seen)

    for spec in sys.argv[2:]:
        if spec.startswith('mon:'):
            monstats = read_table(root, 'monstats')
            monsounds = {r['Id'].lower(): r for r in read_table(root, 'monsounds') if r.get('Id')}
            for m in monstats:
                if m.get('Id', '').lower() != spec[4:].lower():
                    continue
                for col in ('MonSound', 'UMonSound'):
                    row = monsounds.get(m.get(col, '').lower())
                    if not row:
                        continue
                    for k, v in row.items():
                        if k in ('Id', 'EOL') or not v or v.lstrip('-').isdigit():
                            continue
                        add_sound(v)
        elif spec.startswith('class:'):
            for s in read_table(root, 'skills'):
                if s.get('charclass') != spec[6:]:
                    continue
                for col in ('stsound', 'stsoundclass', 'dosound', 'dosound a', 'dosound b', 'tgtsound', 'prgsound'):
                    add_sound(s.get(col, ''))
                for col in ('srvmissile', 'srvmissilea', 'srvmissileb', 'srvmissilec'):
                    if s.get(col):
                        add_missile(s[col])
        elif spec == 'items':
            for table in ('weapons', 'armor', 'misc'):
                for r in read_table(root, table):
                    add_sound(r.get('dropsound', ''))
                    add_sound(r.get('usesound', ''))
        elif spec.startswith('missile:'):
            add_missile(spec[8:])
        elif spec.startswith('vo:') or spec.startswith('vokey:'):
            name = spec.split(':', 1)[1]
            strings = os.path.join(root, 'data', 'local', 'lng', 'strings')
            vo = json.load(open(os.path.join(strings, 'vo.json'), encoding='utf-8-sig'))
            meta = json.load(open(os.path.join(strings, 'metadata', 'metadata-audio-vo.json'), encoding='utf-8-sig'))
            by_id = {m['Id']: m for m in meta}
            if spec.startswith('vokey:'):
                pattern = re.compile('^%s$' % re.escape(name))
            else:
                pattern = re.compile(r'^(A[12]Q[1-6]\w*%s(Main|Alt|2\w*)?|%s(Act1|Act)?Intro\w*|%sGossip\d+)$' % (name, name, name))
            for e in vo:
                m = by_id.get(e.get('id'))
                if not m or not pattern.match(e.get('Key', '')):
                    continue
                act = (m.get('ExtraMetadata') or 'Act1').lower()
                audio = os.path.splitext(m['AudioFileName'])[0].lower() + '.flac'
                speaker = m['Speaker'].lower()
                # Algunas carpetas no se llaman como el que habla (igual que en strings.c)
                speaker = {'deckardcain': 'cain', 'greiz': 'griez', 'andariel': 'monster', 'bloodraven': 'monster',
                           'the countess': 'monster', 'the smith': 'monster'}.get(speaker, speaker)
                files.add('data/local/sfx/%s/%s/%s' % (act, speaker, audio))
        elif spec.startswith('env:'):
            for r in read_table(root, 'soundenviron'):
                if r.get('Index') == spec[4:]:
                    for col in ('Song', 'Day Ambience', 'Day Event'):
                        add_sound(r.get(col, ''))
        else:
            add_sound(spec)

    for f in sorted(files):
        if os.path.isfile(os.path.join(root, f)):
            print(f)
        else:
            print('falta %s' % f, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
