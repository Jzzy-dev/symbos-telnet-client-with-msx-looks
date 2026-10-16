#!/usr/bin/env python3
"""Build the 6x8 CP437 terminal font for SymTerm.

Letters/symbols come from the public-domain X11 misc-fixed fonts (5x8 or 6x9).
Box-drawing, block and shade characters (CP437 0xB0-0xDF) are hand-drawn for a
6x8 cell so that they tile seamlessly, like on a real PC/MSX text screen.

Output: font.h  ->  unsigned char tr_fontdata[2048]; 8 bytes per glyph,
each byte holds 6 pixels in bits 5..0 (bit 5 = leftmost pixel).
"""
import sys, os

W, H = 6, 8

def load_bdf(path):
    glyphs = {}
    fbb = None
    lines = open(path, encoding='latin-1').read().splitlines()
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith('FONTBOUNDINGBOX'):
            fbb = list(map(int, l.split()[1:]))
        if l.startswith('STARTCHAR'):
            enc = None; bbx = None; bm = []
            i += 1
            while not lines[i].startswith('ENDCHAR'):
                t = lines[i]
                if t.startswith('ENCODING'): enc = int(t.split()[1])
                elif t.startswith('BBX'): bbx = list(map(int, t.split()[1:]))
                elif t == 'BITMAP':
                    i += 1
                    while not lines[i].startswith('ENDCHAR'):
                        bm.append(int(lines[i], 16)); i += 1
                    break
                i += 1
            glyphs[enc] = (bbx, bm)
        i += 1
    return fbb, glyphs

def bdf_cell(fbb, g, mode):
    """Render a BDF glyph into an 8-row list of 6-char strings."""
    fw, fh, fx, fy = fbb
    bbx, bm = g
    w, h, xo, yo = bbx
    nbytes = (w + 7) // 8
    # full font cell as pixel grid (fh rows), baseline at row fh+fy
    cell = [[0]*8 for _ in range(fh)]
    top = (fh + fy) - (h + yo)   # row index of glyph's first bitmap row
    for r, val in enumerate(bm):
        for c in range(w):
            if val & (1 << (nbytes*8 - 1 - c)):
                rr, cc = top + r, c + xo
                if 0 <= rr < fh and 0 <= cc < 8:
                    cell[rr][cc] = 1
    if mode == '6x9':
        # 9 rows -> 8: drop the top row (always blank-ish in misc-fixed 6x9)
        cell = cell[1:]
    rows = []
    for r in range(8):
        rows.append(''.join('#' if cell[r][c] else '.' for c in range(6)))
    return rows

BOX = {
0xB3: ["..#..."]*8,
0xB4: ["..#..."]*3 + ["###..."] + ["..#..."]*4,
0xB5: ["..#..."]*2 + ["###...", "..#...", "###..."] + ["..#..."]*3,
0xB6: [".#.#.."]*3 + ["##.#.."] + [".#.#.."]*4,
0xB7: ["......"]*3 + ["####.."] + [".#.#.."]*4,
0xB8: ["......"]*2 + ["###...", "..#...", "###..."] + ["..#..."]*3,
0xB9: [".#.#.."]*2 + ["##.#..", "...#..", "##.#.."] + [".#.#.."]*3,
0xBA: [".#.#.."]*8,
0xBB: ["......"]*2 + ["####..", "...#..", "##.#.."] + [".#.#.."]*3,
0xBC: [".#.#.."]*2 + ["##.#..", "...#..", "####.."] + ["......"]*3,
0xBD: [".#.#.."]*3 + ["####.."] + ["......"]*4,
0xBE: ["..#..."]*2 + ["###...", "..#...", "###..."] + ["......"]*3,
0xBF: ["......"]*3 + ["###..."] + ["..#..."]*4,
0xC0: ["..#..."]*3 + ["..####"] + ["......"]*4,
0xC1: ["..#..."]*3 + ["######"] + ["......"]*4,
0xC2: ["......"]*3 + ["######"] + ["..#..."]*4,
0xC3: ["..#..."]*3 + ["..####"] + ["..#..."]*4,
0xC4: ["......"]*3 + ["######"] + ["......"]*4,
0xC5: ["..#..."]*3 + ["######"] + ["..#..."]*4,
0xC6: ["..#..."]*2 + ["..####", "..#...", "..####"] + ["..#..."]*3,
0xC7: [".#.#.."]*3 + [".#.###"] + [".#.#.."]*4,
0xC8: [".#.#.."]*2 + [".#.###", ".#....", ".#####"] + ["......"]*3,
0xC9: ["......"]*2 + [".#####", ".#....", ".#.###"] + [".#.#.."]*3,
0xCA: [".#.#.."]*2 + ["##.###", "......", "######"] + ["......"]*3,
0xCB: ["......"]*2 + ["######", "......", "##.###"] + [".#.#.."]*3,
0xCC: [".#.#.."]*2 + [".#.###", ".#....", ".#.###"] + [".#.#.."]*3,
0xCD: ["......"]*2 + ["######", "......", "######"] + ["......"]*3,
0xCE: [".#.#.."]*2 + ["##.###", "......", "##.###"] + [".#.#.."]*3,
0xCF: ["..#..."]*2 + ["######", "......", "######"] + ["......"]*3,
0xD0: [".#.#.."]*3 + ["######"] + ["......"]*4,
0xD1: ["......"]*2 + ["######", "......", "######"] + ["..#..."]*3,
0xD2: ["......"]*3 + ["######"] + [".#.#.."]*4,
0xD3: [".#.#.."]*3 + [".#####"] + ["......"]*4,
0xD4: ["..#..."]*2 + ["..####", "..#...", "..####"] + ["......"]*3,
0xD5: ["......"]*2 + ["..####", "..#...", "..####"] + ["..#..."]*3,
0xD6: ["......"]*3 + [".#####"] + [".#.#.."]*4,
0xD7: [".#.#.."]*3 + ["######"] + [".#.#.."]*4,
0xD8: ["..#..."]*2 + ["######", "..#...", "######"] + ["..#..."]*3,
0xD9: ["..#..."]*3 + ["###..."] + ["......"]*4,
0xDA: ["......"]*3 + ["..####"] + ["..#..."]*4,
0xDB: ["######"]*8,
0xDC: ["......"]*4 + ["######"]*4,
0xDD: ["###..."]*8,
0xDE: ["...###"]*8,
0xDF: ["######"]*4 + ["......"]*4,
}

def shade(fn):
    return [''.join('#' if fn(x, y) else '.' for x in range(6)) for y in range(8)]
BOX[0xB0] = shade(lambda x, y: (y % 2 == 0) and ((x + (y // 2) * 1) % 2 == 0) and ((x + y//2) % 4 in (0, 2)) and ((x//2 + y//2) % 2 == 0))
BOX[0xB0] = shade(lambda x, y: (y % 4 == 0 and x % 2 == 0) or (y % 4 == 2 and x % 2 == 1))
BOX[0xB1] = shade(lambda x, y: (x + y) % 2 == 0)
BOX[0xB2] = shade(lambda x, y: not ((y % 4 == 0 and x % 2 == 0) or (y % 4 == 2 and x % 2 == 1)))
# a few extras that look better drawn for the cell
BOX[0xFE] = ["......", "......", ".####.", ".####.", ".####.", ".####.", "......", "......"]  # ■

CP437_LOW = [0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
             0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
             0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
             0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC]

def cp437_unicode(code):
    if code < 32: return CP437_LOW[code]
    if code == 0x7F: return 0x2302
    return ord(bytes([code]).decode('cp437'))

def build(bdfpath, mode):
    fbb, glyphs = load_bdf(bdfpath)
    font = []
    for code in range(256):
        if code in BOX:
            rows = BOX[code]
        else:
            u = cp437_unicode(code)
            if u in glyphs:
                rows = bdf_cell(fbb, glyphs[u], mode)
            else:
                rows = ["......"]*8
        assert len(rows) == 8 and all(len(r) == 6 for r in rows), hex(code)
        font.append(rows)
    return font

def to_bytes(font):
    out = []
    for rows in font:
        for r in rows:
            v = 0
            for c in range(6):
                if r[c] == '#': v |= 1 << (5 - c)
            out.append(v)
    return out

def preview(font, path, scale=3):
    from PIL import Image
    img = Image.new('RGB', (16*W*scale, 16*H*scale), (0, 0, 0))
    px = img.load()
    for code in range(256):
        gx, gy = (code % 16) * W, (code // 16) * H
        for y in range(8):
            for x in range(6):
                if font[code][y][x] == '#':
                    for a in range(scale):
                        for b in range(scale):
                            px[(gx+x)*scale+a, (gy+y)*scale+b] = (200, 200, 200)
    img.save(path)

if __name__ == '__main__':
    src = sys.argv[1]; mode = sys.argv[2]; out = sys.argv[3]
    font = build(src, mode)
    data = to_bytes(font)
    with open(out, 'w') as f:
        f.write('/* 6x8 CP437 font, generated by tools/gen_font.py.\n'
                ' * Glyphs derived from the public-domain X11 misc-fixed %s font;\n'
                ' * box/block/shade characters drawn for the 6x8 cell. */\n' % mode)
        f.write('unsigned char tr_fontdata[2048] = {\n')
        for i in range(0, 2048, 16):
            f.write('  ' + ','.join('0x%02X' % b for b in data[i:i+16]) + ',\n')
        f.write('};\n\n')
        f.write('/* Unicode code point of each CP437 character (for UTF-8 input). */\n')
        f.write('unsigned short tr_uni[256] = {\n')
        uni = [cp437_unicode(c) for c in range(256)]
        uni[0] = 0
        for i in range(0, 256, 8):
            f.write('  ' + ','.join('0x%04X' % u for u in uni[i:i+8]) + ',\n')
        f.write('};\n')
    if len(sys.argv) > 4:
        preview(font, sys.argv[4])
