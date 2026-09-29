#!/usr/bin/env python3
"""Pull named function definitions out of a C file, by brace matching.

The renderer differential tests compare the live code in
src/amiga/render_soft.c against frozen copies of what it replaced.  Rather
than duplicating the live code here - which would rot the moment anyone edits
it - this lifts the functions straight out of the real source.

Fails loudly if a function cannot be found or its braces do not balance, so a
rename shows up as a build error rather than as a silently passing test.
"""

import re
import sys

# In dependency order: helpers first, then the functions under test.
WANTED = {
    "src/amiga/render_soft.c": [
    "dexp2",
    "dpow2",
    "dscale2",
    "dfrom64",
    "dbelow2",
    "dfits",
    "fxround",
    "fxwrap",
    "udiv64",
    "tcdiv",
    "recip64",
    "fdiv",
    "cdiv",
    "rdivp",
    "draw_span",
    "quad_frame_setup",
    "bitlen64",
    "shr64",
    "ushr64",
    "mulshr",
    "divsh",
    "plane_frac",
    "draw_upright_quad",
    "softtri",
    "R_DrawFloorSprite",
    ],
    "src/graphx.c": [
        "build_tantab",
        "fxmul",
        "fxtrunc",
        "small_tan",
        "slope_recip",
        "ray_slopes",
        "hits_apart",
        "castray",
        "recurseray",
    ],
}

# Tile numbers the ray caster branches on.  Pulled from the real header rather
# than copied, so they cannot drift.
# Blocks of constants and file-local state the lifted functions need, marked
# in the source with rendertest:begin-<tag> / rendertest:end-<tag>.
BLOCKS = {"src/graphx.c": ["fixedpoint"],
          "src/amiga/render_soft.c": ["dbits", "quadcam"]}

TILE_MACROS = ["fountain", "map", "gameover",
               "doorside1", "doorside2", "doorside3", "doorside4", "doorside5",
               "door1", "door2", "door3", "door4", "door5"]


def fpu_only_lines(lines):
    """Which lines sit inside an #ifdef __HAVE_68881__ (or the #else of an
    #ifndef): the FPU builds' code, which these tests do not exercise.  Some
    functions exist in two versions, one for each kind of build; the soft
    float one is the one lifted."""
    out, stack = [], []
    for l in lines:
        t = l.strip()
        if t.startswith("#ifdef") or t.startswith("#ifndef") or t.startswith("#if "):
            fpu = "__HAVE_68881__" in t
            stack.append((fpu, t.startswith("#ifdef") if fpu else None))
        elif t.startswith("#else") and stack:
            fpu, was_ifdef = stack[-1]
            if fpu:
                stack[-1] = (fpu, not was_ifdef)
        elif t.startswith("#endif") and stack:
            stack.pop()
        out.append(any(fpu and inside for fpu, inside in stack))
    return out


def find_definition(lines, name):
    """Return (start, end) line indices of `name`'s definition, inclusive."""
    # The definition's first line has the name followed by '(' and is not a
    # call (column 0) and not a prototype (it or a later line opens a brace
    # before any ';').
    pat = re.compile(r"^(?:static\s+)?[A-Za-z_][A-Za-z0-9_ \t*]*\b"
                     + re.escape(name) + r"\s*\(")
    fpu_only = fpu_only_lines(lines)
    for i, line in enumerate(lines):
        if not pat.match(line) or fpu_only[i]:
            continue
        # Walk forward to the '{' that opens the body, bailing on a ';' first
        # (that would be a prototype, not a definition).
        j, opened = i, False
        while j < len(lines) and j < i + 12:
            if "{" in lines[j]:
                opened = True
                break
            if ";" in lines[j]:
                break
            j += 1
        if not opened:
            continue
        # Brace match from there.
        depth = 0
        for k in range(j, len(lines)):
            depth += lines[k].count("{") - lines[k].count("}")
            if depth == 0:
                return i, k
        raise SystemExit("extract.py: unbalanced braces in %s" % name)
    return None


def lift(src, names):
    lines = open(src).read().split("\n")
    chunks, missing = [], []
    for name in names:
        span = find_definition(lines, name)
        if span is None:
            missing.append(name)
            continue
        chunks.append("\n".join(lines[span[0]:span[1] + 1]))
        chunks.append("")
    if missing:
        raise SystemExit(
            "extract.py: could not find in %s: %s\n"
            "  The code was refactored; update WANTED in this script."
            % (src, ", ".join(missing)))
    return chunks


def lift_block(src, tag):
    lines = open(src).read().split("\n")
    b = e = None
    for i, l in enumerate(lines):
        if "rendertest:begin-" + tag in l: b = i + 1
        if "rendertest:end-" + tag in l:   e = i
    if b is None or e is None or e < b:
        raise SystemExit("extract.py: no rendertest:%s block in %s" % (tag, src))
    return lines[b:e]


def tile_macros(header):
    """Lift the tile number #defines the ray caster branches on."""
    text = open(header).read().split("\n")
    out, missing = [], []
    for name in TILE_MACROS:
        pat = re.compile(r"^#define\s+" + re.escape(name) + r"\s")
        hit = [l for l in text if pat.match(l)]
        if not hit:
            missing.append(name)
        else:
            out.append(hit[0])
    if missing:
        raise SystemExit("extract.py: no #define for %s in %s"
                         % (", ".join(missing), header))
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: extract.py <repo-root> <out-dir>")
    root, outdir = sys.argv[1].rstrip("/"), sys.argv[2].rstrip("/")

    header = ["/* Generated by tools/rendertest/extract.py - do not edit. */", ""]

    body = list(header) + tile_macros(root + "/include/lab3d.h") + [""]
    open(outdir + "/tiles.inc", "w").write("\n".join(body))

    total = 0
    for src, names in WANTED.items():
        stem = src.split("/")[-1].replace(".c", "")
        body = list(header) + ["/* Lifted verbatim from %s */" % src, ""]
        for tag in BLOCKS.get(src, []):
            body += lift_block(root + "/" + src, tag) + [""]
        body += lift(root + "/" + src, names)
        open("%s/%s.inc" % (outdir, stem), "w").write("\n".join(body))
        total += len(names)

    print("extract.py: lifted %d functions and %d tile macros"
          % (total, len(TILE_MACROS)))


if __name__ == "__main__":
    main()
