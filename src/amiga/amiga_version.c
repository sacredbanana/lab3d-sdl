/*
 * Version tag and stack size for the AmigaOS executables.
 *
 * Makefile.Amiga compiles this file again on every build, so the date in the
 * version string always matches the link.  It passes AMIGA_VARIANT (020,
 * 020fpu, ...) so each tag names the executable it is in, and AMIGA_DATE in
 * the dd.mm.yyyy form the Version command expects.
 */

#include "buildconfig.h"

#define STR_(x) #x
#define STR(x)  STR_(x)

#if LAB3D_VERSION_PATCH
#define AMIGA_VERSION STR(LAB3D_VERSION_MAJOR) "." STR(LAB3D_VERSION_MINOR) \
                      "." STR(LAB3D_VERSION_PATCH)
#else
#define AMIGA_VERSION STR(LAB3D_VERSION_MAJOR) "." STR(LAB3D_VERSION_MINOR)
#endif

#define AMIGA_STACK 32768

/* "version Kens-Labyrinth.020" finds this by scanning the file for $VER:. */
static const char versiontag[] __attribute__((used)) =
    "$VER: Kens-Labyrinth." AMIGA_VARIANT " " AMIGA_VERSION
    " (" AMIGA_DATE ")";

/* Read from the file by shells that honour stack cookies, before the program
 * starts. */
static const char stackcookie[] __attribute__((used)) =
    "$STACK: " STR(AMIGA_STACK);

/* The OS 3.x Shell ignores the cookie and starts every program on its own
 * stack, 4K by default.  libnix's swapstack.o reads __stack at startup and
 * moves onto a new stack when the one it was given is smaller, which covers
 * that case and a Workbench icon with too small a stack.  Makefile.Amiga
 * links swapstack.o in with -u ___stkinit; nothing references it otherwise. */
unsigned long __stack = AMIGA_STACK;
