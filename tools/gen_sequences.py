#!/usr/bin/env python3
"""
gen_sequences.py: arma src/sequences.c con las secuencias de animación de las
habilidades del Bárbaro, la Amazona, la Hechicera, el Paladín, la Asesina y el Druida, sacadas de SequenceTbls.cpp de D2MOO
(github.com/ThePhrozenKeep/D2MOO, source/D2Common/src/DataTbls). Son las
tablas del original que dicen, para cada clase de arma, qué cuadro de qué
modo se muestra en cada paso y en cuál cae el golpe.

  gen_sequences.py SequenceTbls.cpp src/sequences.c
"""
import re
import sys

# Las del Bárbaro, la Amazona, la Hechicera, el Paladín, la Asesina y el Druida (el número es seqnum de skills.txt)
WANTED = {1: 'Jab', 2: 'Sacrifice', 6: 'Inferno', 7: 'Strafe', 8: 'Impale', 9: 'Fend', 10: 'Whirlwind', 11: 'DoubleSwing',
          12: 'Lightning', 13: 'Leap', 14: 'LeapAttack', 15: 'DoubleThrow', 16: 'DragonClaw', 19: 'DragonTalon',
          18: 'ArcticBlast', 21: 'DragonFlight', 23: 'BladeFury'}
MODES = {'PLRMODE_ATTACK1': 'SEQ_A1', 'PLRMODE_ATTACK2': 'SEQ_A2', 'PLRMODE_CAST': 'SEQ_SC',
         'PLRMODE_THROW': 'SEQ_TH', 'PLRMODE_SPECIAL1': 'SEQ_S1', 'PLRMODE_SPECIAL3': 'SEQ_S3',
         'PLRMODE_KICK': 'SEQ_KK', 'PLRMODE_SPECIAL4': 'SEQ_S4', '0': 'SEQ_A1'}
EVENTS = {'ANIMSEQ_EVENT_NONE': 'SEQ_EV_NONE', 'ANIMSEQ_EVENT_MELEE_ATTACK': 'SEQ_EV_HIT',
          'ANIMSEQ_EVENT_PLAY_SOUND': 'SEQ_EV_SOUND'}


def main():
    src = open(sys.argv[1], encoding='utf-8').read()
    arrays = {}
    for m in re.finditer(r'D2AnimSeqTxt (\w+)\[(\d+)\] =\s*\{(.*?)\n\};', src, re.S):
        rows = re.findall(r'\{\s*0,\s*(\w+),\s*(\d+),\s*(\d+),\s*(\w+)\s*\}', m.group(3))
        arrays[m.group(1)] = rows
    structs = {}
    for m in re.finditer(r'D2PlayerWeaponSequencesStrc (\w+) =\s*\{(.*?)\n\};', src, re.S):
        recs = re.findall(r'(\w+),\s*(\d+),\s*(\d+),', m.group(2))
        structs[m.group(1)] = [(name if name != '0' else None, int(n)) for name, n, _ in recs]
    table = re.search(r'gPlayerWeaponsSequenceTable\[\d+\] =\s*\{(.*?)\};', src, re.S).group(1)
    order = [t.strip().lstrip('&') for t in table.split(',') if t.strip()]

    out = ['/* Generado por tools/gen_sequences.py desde SequenceTbls.cpp de D2MOO: no editar a mano. */',
           '#include "sequences.h"', '', '#include <stddef.h>', '#include <string.h>', '']
    used = set()
    records = {}
    for num, label in WANTED.items():
        struct = order[num]
        recs = structs[struct] + [(None, 0)] * (14 - len(structs[struct]))
        records[num] = recs
        for name, _ in recs:
            if name:
                used.add(name)
    for name in sorted(used):
        rows = arrays[name]
        out.append('static const SeqFrame %s[%d] = {' % (name, len(rows)))
        for mode, frame, d, ev in rows:
            if mode == '0' and frame == '0' and ev == 'ANIMSEQ_EVENT_NONE':
                continue  # el relleno del final (Rechazo con 1hs)
            out.append('    { %s, %s, %s },' % (MODES[mode], frame, EVENTS.get(ev, 'SEQ_EV_NONE')))
        out.append('};')
        out.append('')
    out.append('typedef struct {')
    out.append('    const SeqFrame *frames;')
    out.append('    int             count;')
    out.append('} SeqRecord;')
    out.append('')
    out.append('/* Por seqnum y por clase de arma: hth, 1ht, 2ht, 1hs, 2hs, bow, xbw, stf, 1js, 1jt, 1ss, 1st, ht1, ht2 */')
    out.append('static const SeqRecord k_records[24][14] = {')
    for num in sorted(WANTED):
        cells = []
        for name, n in records[num]:
            if name:
                count = len([r for r in arrays[name] if not (r[0] == '0' and r[1] == '0' and r[3] == 'ANIMSEQ_EVENT_NONE')])
                cells.append('{ %s, %d }' % (name, count))
            else:
                cells.append('{ NULL, 0 }')
        out.append('    [%d] = { /* %s */' % (num, WANTED[num]))
        out.append('        ' + ', '.join(cells[:7]) + ',')
        out.append('        ' + ', '.join(cells[7:]) + ' },')
    out.append('};')
    out.append('')
    out.append('''static const char *const k_wclass[14] = { "hth", "1ht", "2ht", "1hs", "2hs", "bow", "xbw",
                                          "stf", "1js", "1jt", "1ss", "1st", "ht1", "ht2" };

const SeqFrame *seq_get(int seqnum, const char *wclass, int *count)
{
    *count = 0;
    if (seqnum <= 0 || seqnum >= 24)
        return NULL;
    for (int w = 0; w < 14; w++)
        if (strcmp(wclass, k_wclass[w]) == 0) {
            *count = k_records[seqnum][w].count;
            return k_records[seqnum][w].frames;
        }
    return NULL;
}''')
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')


main()
