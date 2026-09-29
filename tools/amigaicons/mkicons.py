#!/usr/bin/env python3
"""
Write the Workbench icons (.info files) for the Amiga release.

    python3 tools/amigaicons/mkicons.py [--preview DIR]

Needs only Python 3.  The pictures are the game's own 64x64 sprites, decoded
straight out of gamedata/Ken1.1/WALLS.KZP with the same LZW code as
loadwalls() in src/subs.c, in the palette initgraphics() builds for 1.x.
The results go to icons/amiga/ and are committed, so build-amiga.sh only has
to copy them:

    Kens-Labyrinth.info     drawer   the "KEN'S LABYRINTH" floppy
    Kens-Labyrinth.0xx.info tool     the green monster in front of the red
                                     brick wall, stack 32768
    Kens-Labyrinth.readme.info
                            project  a page of text, default tool MultiView
    Install.info            project  the floppy with a green arrow, default
                                     tool Installer, APPNAME and user level
                                     tool types for the Install script

--preview DIR also writes a PNG of each icon, normal and selected side by
side, for checking the art without booting an Amiga.

Every icon carries two images of the same picture:

  * a classic planar one, 2 bitplanes in the standard four colour Workbench
    palette (grey, black, white, blue), which is all AmigaOS 3.0 and 3.1 can
    show; and
  * an OS 3.5 colour icon ("FORM ICON" appended after the classic data),
    which icon.library V44 and later (3.5, 3.9, 3.2, AmiKit) show instead.

File layout, all big endian (see workbench/workbench.h, intuition/intuition.h
and the icon.library V44 autodoc for the FORM ICON chunks):

  struct DiskObject           78 bytes, pointers are only "present" flags
  struct OldDrawerData        56 bytes, drawers only
  struct Image + planes       normal image
  struct Image + planes       selected image
  default tool                ULONG length incl. NUL, then the string
  tool types                  ULONG (count + 1) * 4, then each string
  struct DrawerData tail      dd_Flags ULONG + dd_ViewModes UWORD, drawers only
  FORM ICON                   FACE, then one IMAG per image (RLE pixels,
                              raw palette)
"""

import os
import struct
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
WALLS = os.path.join(ROOT, "gamedata", "Ken1.1", "WALLS.KZP")
NUMWALLS = 224  # rnumwalls for 1.1, see init.c
OUT_DIR = os.path.join(ROOT, "icons", "amiga")

EXECUTABLES = ["Kens-Labyrinth.020", "Kens-Labyrinth.020fpu",
               "Kens-Labyrinth.040", "Kens-Labyrinth.060"]
STACK = 32768

# Wall numbers (0 based, as loadwalls() counts them) of the sprites used.
WALL_BRICK = 0
WALL_FLOPPY = 12
WALL_MONSTER = 37
TRANSPARENT = 255  # the game's own "no pixel" index

# ----------------------------------------------------------- game graphics

# opaldef[] from include/lab3d.h: 16 hues, each ramped over 16 shades.
OPALDEF = [(0, 30, 63), (28, 34, 60), (0, 50, 20), (15, 60, 30),
           (63, 63, 25), (63, 63, 63), (63, 20, 20), (63, 0, 63),
           (63, 32, 0), (63, 40, 25), (63, 48, 48), (45, 63, 45),
           (0, 0, 63), (63, 40, 63), (63, 30, 20), (63, 63, 63)]


def game_palette():
    pal = []
    for hue in OPALDEF:
        for j in range(1, 17):
            pal.append(tuple(((c * j) // 17) * 255 // 63 for c in hue))
    return pal


def unlzw(buf, strtot):
    """loadwalls()'s decoder, line for line."""
    buf = buf + b"\0\0\0"
    lzw = [i & 255 for i in range(4097)]
    lzw2 = list(range(4097))
    lzw[0] = lzw2[0] = 0
    out = bytearray()
    pos = bit = 0
    cur, goal, bits = 256, 512, 9
    while True:
        dat = ((buf[pos] | (buf[pos + 1] << 8)) >> bit) & ((1 << bits) - 1)
        if bit + bits > 16:
            dat += (buf[pos + 2] & ((1 << ((bit + bits) & 15)) - 1)) << (16 - bit)
        bit += bits
        pos += bit >> 3
        bit &= 7
        lzw2[cur] = dat
        stack = []
        while dat >= 256:
            stack.append(lzw[dat])
            dat = lzw2[dat]
        lzw[cur - 1] = dat
        lzw[cur] = dat
        stack.append(lzw2[dat])
        while stack:
            v = stack.pop()
            if len(out) < 4096:
                out.append(v)
        cur += 1
        if cur == goal:
            bits += 1
            goal <<= 1
        if cur > strtot:
            return bytes(out)


def load_walls():
    """Ken 1.x WALLS.KZP: one header byte per wall, then per wall a LE16
    string count, a LE16 packed length and the packed bytes.  A wall is
    64x64 bytes stored column by column."""
    data = open(WALLS, "rb").read()
    pos = NUMWALLS
    walls = []
    for _ in range(NUMWALLS):
        strtot, comp = struct.unpack_from("<HH", data, pos)
        pos += 4
        body = data[pos:pos + comp]
        pos += comp
        walls.append(unlzw(body, strtot) if strtot > 0 else body[:4096])
    return walls


def wall_rows(wall):
    """Column major wall -> list of 64 rows of palette indices."""
    return [[wall[x * 64 + y] for x in range(64)] for y in range(64)]


def shade(index, steps):
    """The same hue, `steps` shades darker (the palette is 16 x 16 ramps)."""
    return (index & 0xF0) | max(0, (index & 15) - steps)


# ------------------------------------------------------------- the pictures
# Each picture is a list of rows of (r, g, b) tuples, None for transparent.

def paletted(rows, pal):
    return [[None if v == TRANSPARENT else pal[v] for v in row] for row in rows]


def monster_picture(walls, pal):
    """The green monster in a red brick corridor, lit from the middle."""
    brick = wall_rows(walls[WALL_BRICK])
    monster = wall_rows(walls[WALL_MONSTER])
    def solid(x, y):
        return 0 <= x < 64 and 0 <= y < 64 and monster[y][x] != TRANSPARENT

    rows = []
    for y in range(64):
        row = []
        for x in range(64):
            edge = max(abs(x - 31.5), abs(y - 26)) / 32.0
            if y < 50:
                v = shade(brick[y][x], int(edge * edge * 4))
            else:
                # Floor: the orange-brown ramp, darker towards the viewer so
                # the wall appears to stand back, with a shadow underfoot.
                v = 14 * 16 + max(0, 9 - (y - 50) // 3)
                if ((x - 31.5) / 26) ** 2 + ((y - 59) / 4.5) ** 2 < 1:
                    v = shade(v, 4)
            if solid(x, y):
                v = monster[y][x]
            elif any(solid(x + dx, y + dy) for dx, dy in
                     ((1, 0), (-1, 0), (0, 1), (0, -1))):
                v = shade(v, 9)  # dark rim to lift him off the bricks
            if x in (0, 63) or y in (0, 63):
                v = 0x50  # frame: black end of a ramp
            row.append(pal[v])
        rows.append(row)
    return rows


def floppy_picture(walls, pal):
    return paletted(wall_rows(walls[WALL_FLOPPY]), pal)


def install_picture(walls, pal):
    """The floppy with a green arrow pointing down into it."""
    rows = floppy_picture(walls, pal)
    fill, rim = (40, 200, 60), (0, 0, 0)
    cx, top, neck, tip = 47, 22, 44, 62
    def inside(x, y):
        if top <= y < neck:
            return abs(x - cx) <= 4           # shaft
        if neck <= y <= tip:
            return abs(x - cx) <= tip - y     # head, 45 degree sides
        return False
    for y in range(64):
        for x in range(64):
            if inside(x, y):
                rows[y][x] = fill
            elif any(inside(x + dx, y + dy) for dx, dy in
                     ((1, 0), (-1, 0), (0, 1), (0, -1))):
                rows[y][x] = rim
    return rows


def readme_picture():
    """A sheet of paper with a dog ear, a red heading and lines of text."""
    w, h = 44, 54
    paper, edge, fold = (238, 236, 226), (40, 40, 48), (190, 188, 176)
    ink, head = (92, 100, 132), (196, 48, 40)
    rows = [[None] * w for _ in range(h)]
    ear = 11
    for y in range(h):
        for x in range(w):
            if x + (ear - y) > w - 1 and y < ear:
                continue  # cut corner
            border = x in (0, w - 1) or y in (0, h - 1) or \
                x + (ear - y) == w - 1 and y < ear
            rows[y][x] = edge if border else paper
    for y in range(1, ear):
        for x in range(w - ear, w - ear + y):
            rows[y][x] = fold
        rows[y][w - ear - 1 + y] = edge
    for x in range(w - ear, w):
        rows[ear][x] = edge
    rows[0][w - ear] = edge
    for x in range(5, 24):
        rows[6][x] = rows[7][x] = head
    lengths = [34, 30, 33, 0, 32, 34, 27, 33, 0, 31, 34, 22]
    for i, n in enumerate(lengths):
        y = 14 + i * 3
        for x in range(5, 5 + n):
            rows[y][x] = ink
    # Soft shadow down the right and along the bottom.
    out = [[None] * (w + 2) for _ in range(h + 2)]
    for y in range(h):
        for x in range(w):
            if rows[y][x] is not None:
                out[y + 2][x + 2] = (0, 0, 0)
    for y in range(h):
        for x in range(w):
            if rows[y][x] is not None:
                out[y][x] = rows[y][x]
    return out


def selected(picture):
    """The colour icon's selected state: the same picture, darker."""
    return [[None if c is None else tuple(v * 3 // 5 for v in c) for c in row]
            for row in picture]


# ------------------------------------------------------ classic planar image

# The standard OS 2/3 Workbench colours 0-3.
WB_PALETTE = [(170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187)]


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def planar_image(picture):
    """Map onto the four Workbench pens.  An error diffusing dither in RGB
    turns red and green art into noise with these pens, so this sorts
    pixels instead: greens and blues (the monster, the floppy's label, the
    README's text) take the blue ramp black-blue-white, everything else the
    grey ramp black-grey-white, each stepped by brightness through a light
    ordered dither.  Transparent pixels become pen 0, the window background,
    which is also the grey."""
    h, w = len(picture), len(picture[0])
    pens = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            c = picture[y][x]
            if c is None:
                continue
            r, g, b = c
            luma = (r * 299 + g * 587 + b * 114) / 1000
            cool = max(g, b) > r * 1.25 and max(c) - min(c) > 40
            mid, mid_pen = (131, 3) if cool else (170, 0)
            t = (BAYER[y & 3][x & 3] + 0.5) / 16 - 0.5  # -0.5 .. 0.5
            if luma < mid:
                pens[y][x] = mid_pen if luma / mid + t * 0.3 > 0.5 else 1
            else:
                f = (luma - mid) / (255 - mid)
                pens[y][x] = 2 if f + t * 0.3 > 0.5 else mid_pen
    return pens


def image_struct(pens, depth=2):
    """struct Image (20 bytes) followed by its bitplanes."""
    h, w = len(pens), len(pens[0])
    words = (w + 15) // 16
    planes = b""
    for plane in range(depth):
        for row in pens:
            bits = bytearray(words * 2)
            for x, p in enumerate(row):
                if p >> plane & 1:
                    bits[x >> 3] |= 0x80 >> (x & 7)
            planes += bits
    head = struct.pack(">hhhhhIBBI", 0, 0, w, h, depth, 1,
                       (1 << depth) - 1, 0, 0)
    return head + planes


# --------------------------------------------------- OS 3.5 colour icon form

def chunk(cid, body):
    return cid + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def pack_bits(values, depth):
    """The FORM ICON run length scheme: a bit stream (MSB first) of 8 bit
    control codes and `depth` bit values.  Code n < 128 is followed by n+1
    literal values; code n > 128 by one value that repeats 257-n times."""
    out = []  # (value, width) pairs
    i, n = 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            out.append((257 - run, 8))
            out.append((values[i], depth))
            i += run
            continue
        start = i
        while i < n and i - start < 128:
            if i + 2 < n and values[i] == values[i + 1] == values[i + 2]:
                break
            i += 1
        out.append((i - start - 1, 8))
        out.extend((v, depth) for v in values[start:i])
    acc = nbits = 0
    data = bytearray()
    for value, width in out:
        acc = (acc << width) | value
        nbits += width
        while nbits >= 8:
            nbits -= 8
            data.append((acc >> nbits) & 0xFF)
    if nbits:
        data.append((acc << (8 - nbits)) & 0xFF)
    return bytes(data)


def colour_form(pictures):
    """FORM ICON with a FACE chunk and one IMAG per picture."""
    h, w = len(pictures[0]), len(pictures[0][0])
    imags = []
    maxpal = 0
    for pic in pictures:
        colours = []
        lookup = {}
        # Pen 0 is kept free as the transparent colour.
        colours.append((0, 0, 0))
        pixels = []
        for row in pic:
            for c in row:
                if c is None:
                    pixels.append(0)
                    continue
                if c not in lookup:
                    lookup[c] = len(colours)
                    colours.append(c)
                pixels.append(lookup[c])
        if len(colours) > 256:
            sys.exit("icon uses %d colours, at most 256 fit" % len(colours))
        depth = max(1, (len(colours) - 1).bit_length())
        body = pack_bits(pixels, depth)
        pal = b"".join(bytes(c) for c in colours)
        maxpal = max(maxpal, len(pal))
        head = struct.pack(">BBBBBBHH",
                           0,                    # transparent colour
                           len(colours) - 1,     # number of colours - 1
                           1 | 2,                # has transparency, palette
                           1,                    # image data: RLE
                           0,                    # palette: uncompressed
                           depth,
                           len(body) - 1,
                           len(pal) - 1)
        imags.append(chunk(b"IMAG", head + body + pal))
    face = struct.pack(">BBBBH", w - 1, h - 1,
                       1,        # ICON_FRAMELESS: the art draws its own edge
                       0x11,     # 1:1 aspect
                       maxpal - 1)
    body = b"ICON" + chunk(b"FACE", face) + b"".join(imags)
    return b"FORM" + struct.pack(">I", len(body)) + body


# ----------------------------------------------------------- the .info file

WBDRAWER, WBTOOL, WBPROJECT = 2, 3, 4
NO_ICON_POSITION = -0x80000000


def string(s):
    b = s.encode("latin-1") + b"\0"
    return struct.pack(">I", len(b)) + b


def disk_object(kind, picture, stack=0, default_tool=None, tool_types=(),
                pos=None, drawer=None):
    sel = selected(picture)
    h, w = len(picture), len(picture[0])
    x, y = pos if pos else (NO_ICON_POSITION, NO_ICON_POSITION)

    gadget = struct.pack(">IhhhhHHHIIIIIHI",
                         0,            # NextGadget
                         0, 0, w, h,   # LeftEdge, TopEdge, Width, Height
                         4 | 2,        # GFLG_GADGIMAGE | GFLG_GADGHIMAGE
                         1,            # GACT_RELVERIFY
                         1,            # GTYP_BOOLGADGET
                         1, 1,         # GadgetRender, SelectRender present
                         0, 0, 0,      # GadgetText, MutualExclude, SpecialInfo
                         0,            # GadgetID
                         1)            # UserData: WB_DISKREVISION
    head = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi",
        kind, 0,
        1 if default_tool else 0,     # do_DefaultTool
        1 if tool_types else 0,       # do_ToolTypes
        x, y,                         # do_CurrentX/Y
        1 if drawer else 0,           # do_DrawerData
        0,                            # do_ToolWindow
        stack)
    assert len(head) == 78

    out = head
    if drawer:
        left, top, width, height = drawer
        newwindow = struct.pack(">hhhhBBIIIIIIIhhHHH",
                                left, top, width, height, 255, 255,
                                0, 0, 0, 0, 0, 0, 0,
                                90, 40, 0xFFFF, 0xFFFF, 1)  # WBENCHSCREEN
        out += newwindow + struct.pack(">ii", 0, 0)        # dd_CurrentX/Y
    # The classic selected image is the normal one complemented, the way
    # Workbench 1.x-3.1 highlights an icon; darkening it instead leaves
    # nothing but black with only four pens.
    pens = planar_image(picture)
    out += image_struct(pens)
    out += image_struct([[p if c is None else 3 - p
                          for p, c in zip(prow, crow)]
                         for prow, crow in zip(pens, picture)])
    if default_tool:
        out += string(default_tool)
    if tool_types:
        # The count is the size of the NULL terminated pointer array.
        out += struct.pack(">I", (len(tool_types) + 1) * 4)
        for t in tool_types:
            out += string(t)
    if drawer:
        out += struct.pack(">IH", 1, 1)  # DDFLAGS_SHOWICONS, DDVM_BYICON
    return out + colour_form([picture, sel])


# ------------------------------------------------------------------ preview

def write_png(path, picture):
    sel = selected(picture)
    h, w = len(picture), len(picture[0])
    back = (170, 170, 170)
    raw = b""
    for y in range(h):
        line = [c or back for c in picture[y]] + [back] * 4 + \
               [c or back for c in sel[y]]
        raw += b"\0" + b"".join(bytes(c) for c in line)

    def ch(t, x):
        return struct.pack(">I", len(x)) + t + x + \
            struct.pack(">I", zlib.crc32(t + x))
    ihdr = struct.pack(">IIBBBBB", w * 2 + 4, h, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", ihdr) +
                ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


# --------------------------------------------------------------------- main

def main(argv):
    preview = None
    if len(argv) == 3 and argv[1] == "--preview":
        preview = argv[2]
    elif len(argv) != 1:
        sys.exit(__doc__)

    walls = load_walls()
    pal = game_palette()
    monster = monster_picture(walls, pal)
    # The drawer's window holds three columns, 190 pixels apart: the four
    # executables in two rows, the readme top right and the installer below
    # it.  Topaz 8 draws
    # "Kens-Labyrinth.020fpu" 168 pixels wide in one line, centred under its
    # icon, so the first icon starts far enough in for the label to fit;
    # proportional fonts (AmiKit) wrap it at the dot onto a second line,
    # which the 110 pixel row pitch leaves room for.
    def column(i, width):
        return 100 + i * 190 - width // 2

    readme = readme_picture()
    icons = {
        # The drawer icon sits beside the drawer, in the parent directory.
        "Kens-Labyrinth": (floppy_picture(walls, pal),
                           dict(kind=WBDRAWER, drawer=(10, 30, 620, 290))),
        "Kens-Labyrinth.readme": (
            readme, dict(kind=WBPROJECT, stack=4096,
                         default_tool="SYS:Utilities/MultiView",
                         pos=(column(2, len(readme[0])), 14))),
        # Installer runs the project file itself as the script.  It will not
        # start from Workbench without APPNAME; NOVICE installs the detected
        # CPU's build into the default drawer without asking.
        "Install": (
            install_picture(walls, pal),
            dict(kind=WBPROJECT, stack=32768, default_tool="Installer",
                 tool_types=("APPNAME=Ken's Labyrinth", "MINUSER=NOVICE",
                             "DEFUSER=AVERAGE"),
                 pos=(column(2, 64), 120))),
    }
    for i, name in enumerate(EXECUTABLES):
        icons[name] = (monster, dict(kind=WBTOOL, stack=STACK,
                                     pos=(column(i % 2, 64),
                                          10 + (i // 2) * 110)))

    os.makedirs(OUT_DIR, exist_ok=True)
    for name, (picture, opts) in icons.items():
        data = disk_object(picture=picture, **opts)
        with open(os.path.join(OUT_DIR, name + ".info"), "wb") as f:
            f.write(data)
        print("%-28s %5d bytes" % (name + ".info", len(data)))
        if preview:
            os.makedirs(preview, exist_ok=True)
            write_png(os.path.join(preview, name + ".png"), picture)


if __name__ == "__main__":
    main(sys.argv)
