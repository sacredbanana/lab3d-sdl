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
    "lerp64",
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


def fpu_state(lines):
    """Which kind of build each line is compiled in: True inside an
    #ifdef __HAVE_68881__ (the FPU builds), False inside the #ifndef or the
    #else (the builds without), None where both see it.  Some functions exist
    in two versions, one for each kind of build; both are lifted, each under
    its own guard, so the harness built with -D__HAVE_68881__ tests the FPU
    one and the plain build tests the other."""
    out, stack = [], []
    for n, l in enumerate(lines):
        t = l.strip()
        if t.startswith("#if"):
            if "__HAVE_68881__" not in t:
                stack.append(None)
            elif re.match(r"#ifdef\s+__HAVE_68881__\s*($|/[*/])", t) or \
                 re.match(r"#if\s+defined\s*\(?\s*__HAVE_68881__\s*\)?\s*($|/[*/])", t):
                stack.append(True)
            elif re.match(r"#ifndef\s+__HAVE_68881__\s*($|/[*/])", t) or \
                 re.match(r"#if\s+!\s*defined\s*\(?\s*__HAVE_68881__\s*\)?\s*($|/[*/])", t):
                stack.append(False)
            else:
                raise SystemExit("extract.py: line %d: cannot tell which builds "
                                 "compile this: %s" % (n + 1, t))
        elif t.startswith("#elif") and stack and stack[-1] is not None:
            raise SystemExit("extract.py: line %d: #elif in an __HAVE_68881__ "
                             "conditional is not supported" % (n + 1))
        elif t.startswith("#else") and stack and stack[-1] is not None:
            stack[-1] = not stack[-1]
        elif t.startswith("#endif") and stack:
            stack.pop()
        known = [s for s in stack if s is not None]
        if len(set(known)) > 1:
            raise SystemExit("extract.py: line %d: nested __HAVE_68881__ "
                             "conditionals that no build compiles" % (n + 1))
        out.append(known[0] if known else None)
    return out


def guarded(text, state):
    """Wrap lifted text in the conditional it sat under in the source."""
    if state is None:
        return text
    return ["#ifdef __HAVE_68881__" if state else "#ifndef __HAVE_68881__"] \
        + text + ["#endif"]


def find_definitions(lines, name):
    """Return [(start, end, state)] for each of `name`'s definitions: the
    line span, inclusive, and fpu_state() of its first line."""
    # The definition's first line has the name followed by '(' and is not a
    # call (column 0) and not a prototype (it or a later line opens a brace
    # before any ';').
    pat = re.compile(r"^(?:static\s+)?[A-Za-z_][A-Za-z0-9_ \t*]*\b"
                     + re.escape(name) + r"\s*\(")
    state = fpu_state(lines)
    found = []
    for i, line in enumerate(lines):
        if not pat.match(line):
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
                found.append((i, k, state[i]))
                break
        else:
            raise SystemExit("extract.py: unbalanced braces in %s" % name)
    # One definition for every build, or one for each kind: anything else
    # would lift two copies into the same compile.
    states = [f[2] for f in found]
    if len(states) != len(set(states)) or (None in states and len(states) > 1):
        raise SystemExit("extract.py: %s is defined %d times, and some build "
                         "would see more than one" % (name, len(found)))
    return found


def lift(src, names):
    lines = open(src).read().split("\n")
    chunks, missing = [], []
    for name in names:
        spans = find_definitions(lines, name)
        if not spans:
            missing.append(name)
            continue
        for b, e, state in spans:
            chunks += guarded(lines[b:e + 1], state)
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
    return guarded(lines[b:e], fpu_state(lines)[b - 1])


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
