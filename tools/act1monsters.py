#!/usr/bin/env python3
"""
act1monsters.py: los monstruos que pueden aparecer en un acto (para armar
el paquete de la Pi con sus gráficos y sonidos y nada más).

  act1monsters.py <carpeta que tiene data/> ids|tokens [acto]

El acto es 1 si no se dice; con 2, los del Acto 2 (sus niveles, sus jefes,
la gente de Lut Gholein y los mercenarios del desierto); con 3, los del
Acto 3 (con Mefisto, el Orbe cautivante, la gente del muelle, los Lobos de
Hierro y lo que ponen los marcadores de monpreset: ranas, Water Watchers y
fetiches).

  ids     los Id de monstats.txt (uno por línea)
  tokens  las carpetas de data/global/monsters que hacen falta

Salen de levels.txt (mon1..mon10 de los niveles del Acto 1), de los jefes
con nombre que ponen los mapas del acto (superuniques.txt), de lo que esos
invocan (spawn y minion1 de monstats.txt) y de los que el motor pone aparte
(Andariel, Caín, los Caídos de cada nivel, la mercenaria y la Valquiria de
la Amazona).
"""
import csv
import os
import sys

SUPERUNIQUES = {
    1: ('Bishibosh', 'Bonebreak', 'Coldcrow', 'Rakanishu', 'Treehead WoodFist', 'Griswold',
        'The Countess', 'Pitspawn Fouldog', 'Boneash', 'The Smith', 'Corpsefire'),
    2: ('Radament', 'The Summoner', 'Coldworm the Burrower', 'Fangskin', 'Beetleburst', 'Leatherarm', 'Fire Eye',
        'Dark Elder', 'Ancient Kaa the Soulless', 'Creeping Feature', 'Bloodwitch the Wild'),
    3: ('Web Mage the Burning', 'Witch Doctor Endugu', 'Stormtree', 'Sarina the Battlemaid', 'Icehawk Riftwing',
        'Ismail Vilehand', 'Geleb Flamefinger', 'Bremm Sparkfist', 'Toorc Icefist', 'Wyand Voidfinger',
        'Maffer Dragonhand'),
}
EXTRA = {
    1: ('andariel', 'cain1', 'roguehire', 'bloodraven', 'fallen1', 'fallen2', 'fallen3', 'fallen4',
        'fallenshaman1', 'fallenshaman2', 'fallenshaman3', 'fallenshaman4', 'gargoyletrap', 'valkyrie'),
    2: ('duriel', 'radament', 'summoner', 'tyrael1', 'atma', 'drognan', 'elzix', 'fara', 'geglash', 'greiz',
        'jerhyn', 'lysander', 'meshif1', 'warriv2', 'cain2', 'act2guard1', 'act2guard2', 'act2guard4', 'act2guard5',
        'act2male', 'act2female', 'act2child', 'act2vendor1', 'act2vendor2', 'act2hire',
        # los que ponen los gusanos (spawn de monstats.txt): huevos y larvas
        'maggotegg1', 'maggotegg2', 'maggotegg3', 'maggotbaby1', 'maggotbaby2', 'maggotbaby3'),
    3: ('mephisto', 'compellingorb', 'cain3', 'ormus', 'alkor', 'asheara', 'hratli', 'natalya', 'meshif2', 'act3hire',
        'act3male', 'act3female',
        # los de los marcadores de monpreset (units.c, monster_variant) y la emboscada de la Gidbinn
        'fetish1', 'fetish2', 'fetish3', 'fetish4', 'fetish5', 'fetishshaman1', 'fetishshaman2', 'fetishshaman3',
        'fetishshaman4', 'fetishshaman5', 'frogdemon1', 'frogdemon2', 'frogdemon3', 'tentacle1', 'tentacle2',
        'tentacle3', 'tentaclehead1', 'tentaclehead2', 'tentaclehead3',
        # el fetiche jefe que sale del altar de la Gidbinn y su escolta (minion1/minion2)
        'fetish11', 'fetishblow3'),
}


def read_table(root, name):
    path = os.path.join(root, 'data', 'global', 'excel', name + '.txt')
    with open(path, encoding='latin-1', newline='') as f:
        rows = list(csv.reader(f, delimiter='\t', quoting=csv.QUOTE_NONE))
    head = rows[0]
    return [dict(zip(head, r)) for r in rows[1:]]


def main():
    if len(sys.argv) not in (3, 4) or sys.argv[2] not in ('ids', 'tokens'):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    root = sys.argv[1]
    act = int(sys.argv[3]) if len(sys.argv) == 4 else 1
    if act not in EXTRA:
        print('acto %d: todavía no' % act, file=sys.stderr)
        return 2
    monstats = {r['Id'].lower(): r for r in read_table(root, 'monstats') if r.get('Id')}
    ids = set(e.lower() for e in EXTRA[act])
    for lv in read_table(root, 'levels'):
        if lv.get('Act') != str(act - 1):
            continue
        for k in range(1, 11):
            m = lv.get('mon%d' % k, '')
            if m:
                ids.add(m.lower())
    for su in read_table(root, 'superuniques'):
        if su.get('Superunique') in SUPERUNIQUES[act] and su.get('Class'):
            ids.add(su['Class'].lower())
    # Lo que invocan (los nidos sueltan cuervos, Blood Raven levanta zombis)
    for i in list(ids):
        m = monstats.get(i)
        for col in ('spawn', 'minion1', 'minion2'):
            if m and m.get(col):
                ids.add(m[col].lower())
    ids = sorted(i for i in ids if i in monstats)
    if sys.argv[2] == 'ids':
        print('\n'.join(ids))
    else:
        tokens = sorted({monstats[i]['Code'].lower() for i in ids if monstats[i].get('Code')})
        for t in tokens:
            if os.path.isdir(os.path.join(root, 'data', 'global', 'monsters', t)):
                print(t)
            else:
                print('falta la carpeta de %s' % t, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
