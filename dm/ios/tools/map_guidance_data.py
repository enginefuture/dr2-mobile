"""Map names, warp links and waypoint classes from the overlay's own game version."""
from annot_strings import excel


def build(romfs):
    lines = ['# DM_MAP_GUIDANCE 1']
    for row in excel(romfs, 'levels.txt'):
        if not row.get('Id', '').isdigit():
            continue
        key = row.get('LevelName', '')
        if not key or '\t' in key or '\n' in key:
            continue
        fields = ['L', row['Id'], key]
        for i in range(8):
            fields.extend((row.get('Vis%d' % i) or '0', row.get('Warp%d' % i) or '-1'))
        lines.append('\t'.join(fields))
    for row in excel(romfs, 'objects.txt'):
        if row.get('OperateFn') == '23' and row.get('*ID', '').isdigit():
            lines.append('W\t' + row['*ID'])
    return '\n'.join(lines) + '\n'
