# LAB3D/SDL

LAB3D/SDL is a port of Ken's Labyrinth to modern operating systems, using
OpenGL for graphics output and the SDL library to provide user input, sound
output, threading, and some graphics support functions. Music output is
through Adlib emulation or MIDI (MIDI only on Windows, Linux and other
operating systems with OSS-compatible sound APIs).

This code has been tested on Windows 98, Windows ME, Windows XP,
Windows 10, macOS Mojave, macOS Catalina, SuSE Linux 7.2 and 8.1, Debian Linux 2.2, SunOS 5.8 (Solaris 8),
FreeBSD 4.7, Raspberry Pi OS and Nintendo Switch.

Improvements over the original Ken's Labyrinth:

- Runs natively on 32-bit/64-bit Intel/ARM Windows, macOS, Unix, Nintendo Switch
  or a 68020 or better Commodore Amiga running AmigaOS 3.x.
- Supports big-endian CPUs.
- Uses OpenGL to provide hardware accelerated, anti-aliased graphics with
  trilinear interpolation in true colour (where available).
- Hi-res texture support.
- Multiple simultaneous sound effects.
- Improved General MIDI music.
- Adlib emulation.
- Game controller support.
- Many bug fixes.

# Hardware requirements

LAB3D/SDL requires a machine capable of running Windows or a Unix-like OS
(e.g. Linux) and the Simple DirectMedia Layer with a little-endian CPU, and a
graphics card capable of OpenGL. macOS (both Apple Silicon and Intel) and Nintendo Switch is also supported.

## Recommended system:

- Pentium II or equivalent CPU.
- NVIDIA Riva TNT or better graphics accelerator (with OpenGL drivers).
- 101-key PC keyboard or similar.

## Optional features:

- 16-bit sound card.
- MIDI sound.
- Two-button mouse or better.
- Joystick.

# Software requirements

## Operating system
Windows 95/98/Me/XP, Linux, Solaris, macOS, BSD, ...

## Libraries
OpenGL 1.2, GLU 1.3, SDL 2.0. Slightly older versions of GLU may work.

## Compiler
GCC 2.95.2 or later recommended. Clang will work for macOS. Other compilers will require Makefile changes, but should work.

## Other utilities
Makefiles require GNU Make (or compatible) and sh (or compatible, e.g. bash). Cmake for creating the build files.

# Installation

## Linux

Before the game will run you will need to install the SDL2, SDL2_Image and GLU shared libraries on your system.

Run the following commands:

```
sudo apt update
sudo apt install libsdl2-2.0-0 libsdl2-image-2.0-0 libglu1-mesa
```

## macOS

All required libraries and data are bundled in the app. Open the .dmg file, drag the app to the Applications folder, then launch it.

## Nintendo Switch

To play this on your Nintendo Switch you will need a Switch set up to run homebrew software, then do any
of these three choices:
- Download the latest release of the game from https://github.com/sacredbanana/lab3d-sdl/releases,
- Download the game from the Homebrew Appstore. This is the easiest option and will also install the game for you automatically.
Or
- Compile the game yourself (see the Nintendo Switch compiling instructions above)

Navigate to the Switch folder inside your Nintendo Switch SD card and create a new folder
called Kens-Labyrinth. Inside this folder, transfer Kens-Labyrinth.nro and all of Ken's Labyrinth's data files. (This is the "gamedata" directory if you wish to have the game launcher. Otherwise just copy a single version of Ken's Labyrinth to the directory containing the executable
WITHOUT including the gamedata directory.)

## Amiga (AmigaOS 3.x, 68020+)

The Amiga port is a separate build of the same game: it uses no SDL and no
OpenGL, drawing the labyrinth with its own software renderer and talking to
intuition.library, graphics.library, cybergraphics.library, audio.device and
lowlevel.library directly.

Four executables are supplied, one per CPU class:

Executable | For |
-----------|-----|
`Kens-Labyrinth.020`    | 68020/68EC020 with no FPU (software floating point) |
`Kens-Labyrinth.020fpu` | 68020/68030 with a 68881 or 68882 |
`Kens-Labyrinth.040`    | 68040 |
`Kens-Labyrinth.060`    | 68060 |

Copy the one that matches your machine, together with the `gamedata` drawer,
into a directory of your choice and run it from a Shell or from Workbench.
The ray caster does a lot of floating point work per frame, so the FPU builds
are considerably faster than the plain 020 one - use `.020` only on a machine
that genuinely has no FPU.

### Choosing a screen mode

Every time the game starts it opens an ASL screen mode requester listing all
the modes your system offers - native OCS/ECS/AGA modes and, if you have
CyberGraphX or Picasso96 installed, every RTG mode as well. Whatever you pick
is remembered in `settings.ini`; start with `-keepmode` to skip the requester
and reuse it.

The game always renders into a 360x240 chunky buffer (the resolution the DOS
original used, in VGA Mode X) and the display layer fits that into the mode you
chose:

- A mode of 360x240 or larger shows the whole view.
- A smaller mode, such as the standard 320x256 PAL screen, shows the 320x200
  window the original game used, centred.
- A mode at least 720x480 can pixel-double the view; set *Pixel doubling* in
  the setup menu to force this on or off.

On an RTG screen, 8 bit modes take the chunky pixels directly and 15/16/24/32
bit modes are expanded through a colour table, so any RTG mode works. On a
native screen the renderer converts chunky to planar itself; an 8 bitplane AGA
mode gives the full 256 colour palette, and a shallower ECS/OCS screen still
runs with the palette folded down to the pens available.

RTG is significantly faster than a native screen, because an 8 bit RTG blit is
a straight memory copy while a planar screen needs chunky-to-planar conversion
for every pixel of every frame.

### Controls

Keyboard, mouse and joystick all work and are configurable from
*Setup -> Configure Input*, as on every other platform. The joystick is read
through lowlevel.library on port 1, so a CD32 pad's extra buttons are
available as buttons 0-6 as well as an ordinary one-button stick.

### What is not in the Amiga version

- **Texture filtering.** The software rasteriser point-samples, which is what
  a 68k can afford; the filtering options are hidden from the Amiga setup menu.
- **Hi-res replacement textures.** Same reason.
- **Stereoscopic 3D.** It needs OpenGL framebuffer objects.
- **General MIDI music.** Music is Adlib emulation, or off. The mixing rate is
  chosen from the CPU (11025 Hz on an 020/030, 22050 Hz on an 040, 28 kHz on an
  060); turning music off in the setup menu frees up a lot of CPU on slower
  machines.
- **Compressed demo files.** `-recordx` (uncompressed) and `-play` work;
  gzip-compressed demos are refused with a message rather than misread.

# Program arguments

Command-line parameters for the executable (these override settings chosen in setup):

Argument | Effect |
---------|--------|
-setup		|	Run setup.|
-res `w` `h` `x` `y` |	Use `x` resolution, simulating `x` 2D screen. Note that while `w` and `h` are integers, `x` and `y` can be any floating point numbers >= than 320x200.
-asp `w` `h`	|	Override aspect correction; `w` and `h` multiply the width and height of the 3D viewport; both are floating point numbers greater than 0.1.
-win	|		Run in a window.
-fullscreen	|	Run in fullscreen mode.
-nearest	|	No display filtering.
-trilinear	|	Trilinear display filtering.
-nomusic	|	Disable music.
-gmmusic	|	General MIDI music.
-admusic	|	Adlib emulation music.
-nosound	|	Disable sound effects.
-sound		|	Enable sound effects.
-debug		|	Extended graphics debug output. Only for troubleshooting purposes.
-load `s``	|	Immediately load a savegame slot (1-8)
-record `demo` `s` |	Start recording a demo file starting at savegame slot `s`
-play <demo>	|	Play back a demo file
-keepmode	|	(Amiga) Reuse the saved screen mode instead of showing the requester.
-askmode	|	(Amiga) Always show the screen mode requester.

To activate cheat codes, the last parameter must be either "snausty" (normal
cheat mode) or "cheaton" (cheat codes use [LSHIFT]-[LCTRL] instead of both
shift keys). Note that cheaters never prosper.

Unrecognised options are ignored.

# Compiling from source

## Windows

Install Visual Studio with Visual-C++ and CMake support.

Open a terminal and in the project root run these commands:

For building for Intel processors:
```
mkdir build
cmake ..
cmake --build . --config Release
```

For building for ARM64:
```
mkdir build
cmake -A ARM64 ..
cmake --build . --config Release
```

The built game can be found in dist/windows.

Alternatively you can use Visual Studio Code to build the game:

Close the initial splash screen by clicking the "Continue without code" option.
Click File -> Open -> CMake. Choose the CMakeLists.txt file. The project will then
open and you'll be able to select from the top menu the flavour of build. From there
you can build and or/debug.

## Windows (Legacy)

Run "make -f Makefile.Win32" in the source directory. MinGW 3.1.0 is
recommended for Windows use.

## macOS
If you plan to build a universal macOS app (meaning the same binary can be run on both Apple Silicon and Intel Macs) then you **MUST** get the universal version of the libraries and this is only available on Macports. Homebrew doesn't support universal packages.

- Install ["Macports"](https://www.macports.org/install.php) if you haven't already.

- Run the following command:
```
sudo echo "macosx_deployment_target 12.4" >> /opt/local/etc/macports/macports.conf
sudo port install libsdl2 +universal libsdl2_image +universal libpng +universal webp +universal jpeg +universal tiff +universal zlib +universal
```

- In the project root run the following commands:
```
mkdir build
cd build
```

Now choose a build method:

- Simple build
```
cmake -DCMAKE_PREFIX_PATH=/opt/local ..
cmake --build . --config Release
```

- Build via Xcode (recommended if you want to properly customise the build for code signing etc):
`cmake -G Xcode -DCMAKE_PREFIX_PATH=/opt/local ..`
Add `-DMACOS_DISTRIBUTION=app-store` if building for AppStore distribution.

Then open the project in Xcode and build it or just build it with:
`cmake --build . --config Release`

The app `Kens-Labyrinth` will be copied to dist/macOS. You may move this to your Applications folder.


## Linux/UNIX

Install the prerequisites with the following command:
```
sudo apt update
sudo apt install libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-image-dev libglu1-mesa-dev cmake build-essential
```

In the project root run the following commands:
```
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

The executable `ken` and its dependencies will be copied dist/linux.

## Unix with OSS support (Legacy)

Run `make` in the source directory.

## Unix without OSS support (Legacy)

Run `make -f Makefile.NoMIDI` in the source directory. Note that General MIDI
music is not available if you do this (not much of a loss).

## Amiga

Building uses the same Docker image as AmigaGPT, so no local m68k toolchain is
needed:

```
./build-amiga.sh
```

This produces all four CPU variants in `dist/amiga/` along with a ready-to-copy
`dist/amiga/Kens-Labyrinth/` drawer containing the executables, the game data
and a readme. `CLEAN=1 ./build-amiga.sh` wipes the build directory first,
`DEBUG=1 ./build-amiga.sh` makes a debug build, and naming variants builds only
those:

```
./build-amiga.sh 060
```

Behind the script is `Makefile.Amiga`, which can be used directly with any
installation of bebbo's `m68k-amigaos-gcc`.

## Nintendo Switch

- Install [devkitPro](https://devkitpro.org/wiki/Getting_Started)
- Depending on which environment you have installed devkitPro, you will either need to use the `pacman` or `dkp-pacman` command for the following command:
`sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-freetype switch-zlib switch-glfw switch-glad` and then just hit enter when it prompts you for a selection to install everything
- run `make -f Makefile.Switch` in the source directory.

# FAQ

### Q: Why does LAB3D/SDL crash immediately after starting on my Windows 95/98 machine with a page fault in "OPENGL32.DLL" (and possibly "KERNEL32.DLL")?
A: You are probably running Microsoft's slow, old and buggy software OpenGL
implementation. Install display drivers with OpenGL support; your graphics
card or chipset manufacturer should provide them (e.g. through their web
site).

### Q: Why does LAB3D/SDL run slowly on my machine?
A: There are several possible reasons for this. If you don't have a 3D
accelerator, LAB3D/SDL will run very slowly. Similarly, if your system's
OpenGL implementation is not accelerated, LAB3D/SDL will be unable to make use
of 3D acceleration; updating your display drivers may fix this. Finally, you
may be running LAB3D/SDL at an unnecessarily high graphics resolution; lower
it to something like 800x600.

### Q: Why doesn't LAB3D/SDL have a fast software renderer like the original Ken's
Labyrinth?
A: It does now, on the platform that needs one. Very few computers are sold
nowadays without 3D acceleration, so on the desktop ports a software renderer
would be of no use to anybody. The Amiga is a different matter, and that port
ships one: see `src/amiga/render_soft.c`. It takes the same geometry graphx.c
hands to OpenGL and draws it as vertical textured spans with a
one-entry-per-column depth buffer, which works because the camera only ever
yaws and walls always run the full height of a cell.

### Q: Why doesn't LAB3D/SDL support Direct3D?
A: OpenGL is a more widely supported 3D graphics API than Microsoft's
Direct3D. The only case in which Direct3D support would be of use is for users
of graphics accelerators with Direct3D acceleration but no OpenGL
acceleration. I suggest you pester your graphics card or chipset manufacturer
to produce OpenGL drivers or buy a better supported graphics card. Future
versions of LAB3D/SDL may support Direct3D, but don't count on it.

### Q: Why doesn't LAB3D/SDL have an automap like Doom, Blake Stone or Descent?
A: Because Ken's Labyrinth doesn't have an automap. Besides, you're supposed
to get lost in labyrinths.

# Comments

Technical comments about Ken's Labyrinth and LAB3D/SDL can be found in
comments.txt.

# License

The license can be found in LICENSE.

# Credits

## Ken's Labyrinth

Design, code and Adlib emulation            | Artwork       | Board maps    | Sound effects | Music
------------------------------------------- | ------------- | ------------- | ------------- | -------------
|Ken Silverman<br>http://www.advsys.net/ken | Mikko Iho     | Andrew Cotter | Ken Silverman | Ken Silverman
|                                           | Ken Silverman |               | Andrew Cotter |
|                                           | Andrew Cotter |               |               |

## LAB3D/SDL

Code                                                           | Testing
-------------------------------------------------------------- | -----------------
Jan Lönnberg<br>http://koti.mbnet.fi/lonnberg/                 | Ken Silverman
Katie Stafford<br>https://ktpanda.org/                       | Danny Desse'
Cameron Armstrong (Nightfox)<br>http://minotaurcreative.net/   |
