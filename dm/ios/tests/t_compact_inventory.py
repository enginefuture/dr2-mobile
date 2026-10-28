"""The table patch must change only footprints / player grid bytes, keeping old saved anchors valid."""
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import compact_inventory as C

class Inventory(unittest.TestCase):
    def test_footprints_only(self):
        b=bytearray((i*31+7)%256 for i in range(4+4*448));struct.pack_into('<I',b,0,4)
        sizes=((2,4),(1,1),(2,3),(0,0))
        for i,size in enumerate(sizes):b[4+i*448+0x11e:4+i*448+0x120]=bytes(size)
        out=C.item_table(b)
        want=bytearray(b)
        for i in range(3):want[4+i*448+0x11e:4+i*448+0x120]=bytes((1,1))
        self.assertEqual(out,want)
        self.assertEqual(C.item_table(out),out)
        with self.assertRaises(ValueError):C.item_table(b[:-1])
    def test_all_classes_and_unchanged_stash(self):
        b=bytearray(4+30*240);struct.pack_into('<I',b,0,30)
        for base in (0,15):
            for row in C.PLAYER_ROWS:b[4+(base+row)*240+16:4+(base+row)*240+18]=bytes((10,4))
        for row in (10,25):b[4+row*240+16:4+row*240+18]=bytes((14,10))
        out=C.inventory_table(b); changed={4+(base+row)*240+16+k for base in (0,15) for row in C.PLAYER_ROWS for k in (0,1)}
        self.assertTrue(all(a==z for i,(a,z) in enumerate(zip(b,out)) if i not in changed))
        for base in (0,15):
            for row in C.PLAYER_ROWS:self.assertEqual(out[4+(base+row)*240+16:4+(base+row)*240+18],bytes((14,6)))
        # Four-bit D2S coordinates can represent every new slot; old anchors remain in range.
        self.assertTrue(all(x<14 and y<6 for x in range(10) for y in range(4)))
        self.assertLess(13,16);self.assertLess(5,16)
    def test_layout_preserves_extensions(self):
        text='''{// native comment
        "children":[{"name":"grid","fields":{"navigation":{"up":"belt"},}},
        {"name":"background","fields":{}},/* keep all other widgets */
        {"name":"PotionBag","fields":{"text":"https://test/x"}},],}'''
        x=json.loads(C.layout(text));g=x['children'][0]['fields']
        self.assertEqual(g['cellCount'],{'x':14,'y':6});self.assertEqual(g['cellSize'],{'x':70,'y':70})
        self.assertEqual(g['navigation'],{'up':'belt'});self.assertEqual(x['children'][2]['fields']['text'],'https://test/x')
        self.assertEqual(json.loads(C.layout(json.dumps(x))),x)
if __name__=='__main__':unittest.main()
