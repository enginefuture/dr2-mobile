"""84 cells and one-cell item footprints; stats, equipment slots and saved anchors stay intact."""
import json
import os
import re
import struct
import touch_layout
import wide_stash

COLS, ROWS, CELL = 14, 6, 70
PLAYER_ROWS = (0, 1, 2, 3, 4, 12, 13, 14)
INV = 'controller/console/switch/playerinventoryexpansionlayouthd.json'
ART_PATH = 'data/hd/global/ui/controller/panel/2compact/inventory84.sprite'


def item_table(data):
    out = bytearray(data)
    count = struct.unpack_from('<I', out)[0]
    if len(out) != 4 + count * 448:
        raise ValueError('compact inventory: unsupported item table size')
    for i in range(count):
        p = 4 + i * 448 + 0x11e
        w, h = out[p:p + 2]
        if w and h:
            if w > 8 or h > 8:
                raise ValueError('compact inventory: invalid original item footprint')
            out[p:p + 2] = b'\x01\x01'
    return bytes(out)


def inventory_table(data):
    out = bytearray(data)
    if len(out) != 4 + 30 * 240 or struct.unpack_from('<I', out)[0] != 30:
        raise ValueError('compact inventory: unsupported inventory table')
    for base in (0, 15):
        for row in PLAYER_ROWS:
            p = 4 + (base + row) * 240 + 16
            if tuple(out[p:p + 2]) not in ((10, 4), (COLS, ROWS)):
                raise ValueError('compact inventory: unexpected player grid')
            out[p:p + 2] = bytes((COLS, ROWS))
    return bytes(out)


def layout(text):
    clean, i, quoted = [], 0, False
    while i < len(text):
        c = text[i]
        if quoted:
            clean.append(c)
            if c == '\\' and i + 1 < len(text):
                i += 1
                clean.append(text[i])
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
            clean.append(c)
        elif text.startswith('//', i):
            end = text.find('\n', i)
            i = len(text) if end < 0 else end
            continue
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('unterminated layout comment')
            i = end + 2
            continue
        else:
            clean.append(c)
        i += 1
    obj = json.loads(re.sub(r',\s*(?=[}\]])', '', ''.join(clean)))
    grid = next(c for c in obj['children'] if c.get('name') == 'grid')
    grid.setdefault('fields', {}).update(rect={'x': 216, 'y': 865}, cellCount={'x': COLS, 'y': ROWS}, cellSize={'x': CELL, 'y': CELL})
    bg = next(c for c in obj['children'] if c.get('name') == 'background')
    bg.setdefault('fields', {})['filename'] = 'Controller/Panel/2compact/Inventory84'
    return json.dumps(obj, ensure_ascii=False, indent=2)


def art(romfs):
    import gen_widestash_art as G
    from PIL import Image
    lib = G.lib()
    _, src = wide_stash._entry(romfs, '/ui/controller/panel/inventorypanel/v2/inventorybg_classic.sprite')
    im = G.decode(lib, src)
    if im.size != (507, 700):
        raise ValueError('compact inventory: original background changed')
    tile = im.crop((9, 425, 58, 474)).resize((35, 35), Image.Resampling.LANCZOS)
    for y in range(ROWS):
        for x in range(COLS):
            im.paste(tile, (9 + 35 * x, 411 + 35 * y))
    return G.pack(lib, src, G.bc3(im), 507, 700, 507)


def wide_art(romfs):
    import gen_widestash_art as G
    from PIL import Image
    lib = G.lib()
    _, src = wide_stash._entry(romfs, '/ui/controller/panel/inventorypanel/v2/inventorybg_classic.sprite')
    original = G.decode(lib, src)
    if original.size != (507, 700):
        raise ValueError('compact inventory: original background changed')
    # Root coordinates -604..2148. Keep the native equipment art, shift it left and down;
    # use the free right column for 14x6 original-style cells, each 112 layout units.
    im = Image.new('RGBA', (1376, 700))
    im.paste(original.crop((0, 0, 507, 411)), (0, 100))
    tile = original.crop((9, 425, 58, 474)).resize((56, 56), Image.Resampling.LANCZOS)
    for y in range(6):
        for x in range(14):
            im.paste(tile, (541 + 56*x, 159 + 56*y))
    im.paste(original.crop((159, 627, 352, 689)), (817, 540))
    return G.pack(lib, src, G.bc3(im), 1376, 700, 1376)


def build(romfs, outdir, repl):
    os.makedirs(outdir, exist_ok=True)
    out = {}
    for sub in ('', 'base/'):
        for name in ('weapons.bin', 'armor.bin', 'misc.bin', 'inventory.bin'):
            path, raw = wide_stash._entry(romfs, '/global/excel/' + sub + name)
            prev = repl.get(path)
            data = open(prev, 'rb').read() if prev else raw
            result = inventory_table(data) if name == 'inventory.bin' else item_table(data)
            file = os.path.join(outdir, 'compact_' + sub.replace('/', '_') + name)
            with open(file, 'wb') as f:
                f.write(result)
            out[path] = file
    file = os.path.join(outdir, 'inventory84.sprite')
    with open(file, 'wb') as f:
        f.write(art(romfs))
    out['+' + ART_PATH] = file
    file = os.path.join(outdir, 'inventory84wide.sprite')
    with open(file, 'wb') as f:
        f.write(wide_art(romfs))
    out['+' + ART_PATH.replace('inventory84.sprite', 'inventory84wide.sprite')] = file
    path, original = touch_layout.read_layout(romfs, INV)
    prev = repl.get(path.lower())
    text = open(prev, encoding='utf-8').read() if prev else original
    file = os.path.join(outdir, 'compact_inventory.json')
    with open(file, 'w', encoding='utf-8') as f:
        f.write(layout(text))
    out[path.lower()] = file
    return out


def footprints(romfs):
    rows, index = [], 0
    for name in ('weapons.bin', 'armor.bin', 'misc.bin'):
        _, data = wide_stash._entry(romfs, '/global/excel/' + name)
        n = struct.unpack_from('<I', data)[0]
        if len(data) != 4 + n * 448:
            raise ValueError('unsupported footprint metadata')
        for i in range(n):
            w, h = data[4+i*448+0x11e:4+i*448+0x120]
            if w and h:
                rows.append('%d\t%d\t%d' % (index+i, w, h))
        index += n
    return '\n'.join(rows)+'\n'
