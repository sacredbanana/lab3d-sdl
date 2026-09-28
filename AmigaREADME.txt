Ken's Labyrinth for AmigaOS 3.x
===============================

This is LAB3D/SDL's Amiga port.  It is the same game as the desktop versions,
but it uses no SDL and no OpenGL: the labyrinth is drawn by a software
renderer, and the display, sound and input go straight through
intuition.library, graphics.library, cybergraphics.library, audio.device or
ahi.device, and lowlevel.library.


Requirements
------------

  * AmigaOS 3.0 or later (V39+).
  * A 68020 or better.  A 68EC020 with no FPU works.
  * About 3 MB of free RAM.
  * asl.library V38 or later, for the screen mode requester.
  * Optional: CyberGraphX or Picasso96, for RTG screen modes.  Without it the
    game runs on a native OCS/ECS/AGA screen instead.
  * Optional: a joystick or CD32 pad in port 1.
  * Optional: ahi.device V4 or later, for 16 bit sound - see Sound below.


Which executable?
-----------------

  Kens-Labyrinth.020      68020/68EC020 with no FPU (software floating point)
  Kens-Labyrinth.020fpu   68020/68030 with a 68881 or 68882
  Kens-Labyrinth.040      68040
  Kens-Labyrinth.060      68060

The renderer and the ray caster work in fixed point, so the FPU builds are no
longer the large step up they once were - what floating point is left runs
once a frame rather than once a pixel.  Pick the build that matches your CPU;
an FPU build will not run at all on a machine that has no FPU.

Keep the gamedata drawer next to whichever executable you use.


Running it
----------

From a Shell:

    Kens-Labyrinth.060

or double-click it from Workbench.  A screen mode requester appears listing
every mode your system offers - native and RTG.  Pick one and the game starts.
Your choice is remembered in settings.ini; add -keepmode to skip the requester
next time, or -askmode to force it back.

The game fills whatever screen mode you pick.  It renders at the screen's
resolution divided by a whole number and scales the result back up, so every
pixel stays the same size: on a 1920x1080 screen it might draw 480x270 and
scale it by four, or draw the full 1920x1080 if the machine is fast enough.
On a widescreen mode the view widens to match, as the desktop version does,
rather than leaving bars at the sides.  Setup -> Render size lists the sizes
your screen allows:

  Automatic       about as much detail as the DOS original, so the
                  renderer's share of the frame costs about what it did.
                  On a native screen bigger than PAL lores it is the same
                  as Off, since filling it would multiply the planar
                  conversion; pick a size by hand to fill it anyway
  full / 1/2 ...  more detail, at a cost that grows with the pixel count -
                  the full 1920x1080 is 24 times the work of 360x240
  Off             the original 360x240 in the middle of the screen, unscaled

The change takes effect at once.  Renders are limited to 2048x1200; a bigger
screen is still filled, from a half size or smaller render.

A small screen, such as the standard 320x256 PAL one, shows the 320x200
window the original game used, as before.

Setup -> View size shrinks the 3D view itself, from 100% down to 40%, into a
box in the middle of the screen with a black border round it - the same idea
as the screen size option in Wolfenstein 3D.  The menus and the status bar
keep their full size, so this works on any screen mode, PAL and NTSC lores
included.  The renderer only draws the box, and only the parts of the screen
that change are sent to the display each frame, so a 50% view is roughly a
quarter of the work.

An 8 bit RTG screen is the fastest option, because the renderer's output is
copied to it as-is.  A native screen needs chunky-to-planar conversion for
every pixel of every frame, which costs real time on a 68020.


Controls
--------

Keyboard, mouse and joystick all work, and all of them can be rebound from
Setup -> Configure Input.  The joystick is read from port 1, and a CD32 pad's
extra buttons show up as buttons 0 to 6.


Sound
-----

Setup -> Sound output picks how the game reaches the speakers:

  Automatic       AHI on a 68040 or better, Paula below that.
  Paula (8 bit)   audio.device, two hardware channels.  Always available.
  AHI (16 bit)    ahi.device, using whatever unit 0 is set to in AHI
                  preferences.  With a sound card this is real 16 bit
                  output.  The actual resolution with other AHI drivers
                  depends on their hardware and configuration.

Automatic selects Paula on 020 and 030 machines to leave more CPU time for the
game.  If you have a sound card in a slower machine, choose AHI explicitly. If
ahi.device cannot be opened the game falls back to Paula and says so.

The mixing rate is chosen from the CPU - 11025 Hz below an 040, 22050 Hz on an
040 and 28000 Hz on an 060 - because the Adlib emulation is synthesised at
that rate and is what costs the time.  The digital sound effects are 11025 Hz
in the game data and are played at that rate whatever the output rate is.


Performance
-----------

If the game is too slow:

  * Use an RTG screen rather than a native one.
  * Turn music off (Setup -> Music).  The Adlib emulation is software FM
    synthesis and it is the single most expensive thing running besides the
    renderer.
  * Shrink the view (Setup -> View size).  This is the biggest saving on a
    slow machine with a native screen: it cuts the drawing and the
    chunky-to-planar conversion together.
  * Pick a smaller Render size, or Automatic.
  * Use a smaller screen mode; 320x200 is a third less work than 360x240.
  * On a native screen, scaling up costs chunky-to-planar time for every
    pixel of the screen, not just of the render.  Render size Off, or a
    lores screen mode, is the fastest way to play there.


Not included
------------

Texture filtering, hi-res replacement textures and the stereoscopic 3D mode
all need hardware the Amiga does not have, and are not present.  Music is
Adlib emulation or nothing; there is no General MIDI output.  Demo files can
be recorded uncompressed (-recordx) and played back, but gzip-compressed
demos are not supported.


Credits
-------

Ken's Labyrinth is by Ken Silverman, published by Epic MegaGames.
LAB3D/SDL is by Jan Lonnberg and contributors.
See the main README for the full credit list and the licence.
