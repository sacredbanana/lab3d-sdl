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

- Runs natively on 32-bit/64-bit Windows, Unix or Nintendo Switch.
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

Before the game will run you will need to install SDL2 and SDL2_Image runtime binaries on your system.

Run the following commands:

```
sudo apt update
sudo apt install libsdl2-2.0-0 libsdl2-image-2.0-0
```

## macOS

Before the game will run you will need to install SDL2 and SDL2_Image runtime binaries on your system.

- Install ["Macports"](https://www.macports.org/install.php) or ["Homebrew](https://brew.sh/) if you haven't already. 
- If you plan to build a universal macOS app (meaning the same binary can be run on both Apple Silicon and Intel Macs) then you **MUST** get the universal version of the SDL2 libraries and this is only available on Macports. Homebrew doesn't support universal packages.

- If you're using Macports, run the following command:
```
sudo port install libsdl2 libsdl2_image +universal
```
- Otherwise run this if you are using Homebrew:
```
brew install sdl2 sdl2_image
```

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

To activate cheat codes, the last parameter must be either "snausty" (normal
cheat mode) or "cheaton" (cheat codes use [LSHIFT]-[LCTRL] instead of both
shift keys). Note that cheaters never prosper.

Unrecognised options are ignored.

# Compiling from source

## Windows

Install Visual Studio 2017 or 2019 with Visual-C++ and CMake support.
Close the initial splash screen by clicking the "Continue without code" option.
Click File -> Open -> CMake. Choose the CMakeLists.txt file. The project will then
open and you'll be able to select from the top menu the flavour of build. From there
you can build and or/debug.

## Windows (Legacy)

Run "make -f Makefile.Win32" in the source directory. MinGW 3.1.0 is
recommended for Windows use.

## macOS
If you plan to build a universal macOS app (meaning the same binary can be run on both Apple Silicon and Intel Macs) then you **MUST** get the universal version of the SDL2 libraries and this is only available on Macports. Homebrew doesn't support universal packages.

- Install ["Macports"](https://www.macports.org/install.php) if you haven't already.

- Run the following command:
```
sudo port install libsdl2 libsdl2_image +universal
```

- In the project root run the following commands:
```
mkdir build
cd build
cmake ..
make
```

The app `Kens-Labyrinth` will be copied to the build folder. You may move this to your Applications folder.

### Build universal app for running on both Apple Silicon and Intel

* Install the [CMake GUI](https://cmake.org/download/)
* Create a folder called `build` in the main lab3d-sdl folder
* Open the CMake app
* Set the source code folder to the main lab3d-sdl folder
* Set the build binaries folder to the `build` folder you created
* Click `Configure`
* In the generator selector choose `Xcode` and click `Done`
* Turn off the `ENABLE_HIRES_TEXTURES` option
* Click `Generate`
* Click `Open Project`
* When Xcode opens, in the project explorer panel on the left, click the top item `lab3d-sdl`
* In the `Targets` section, click `Kens-Labyrinth`
* In the `Minimum Deployments` section, change the `macOS` version to the lowest one in the list
* In the bar at the very top of Xcode, if you see `ALL_BUILD`, click on it and choose `Kens-Labyrinth`
* Click the play button to build and run the app

## Linux/UNIX

Install the prerequisites with the following command:
```
sudo apt update
sudo apt install libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-image-dev cmake build-essential
```

In the project root run the following commands:
```
mkdir build
cd build
cmake ..
make
```

The executable `ken` and its dependencies will be copied to the build folder.

## Unix with OSS support (Legacy)

Run `make` in the source directory.

## Unix without OSS support (Legacy)

Run `make -f Makefile.NoMIDI` in the source directory. Note that General MIDI
music is not available if you do this (not much of a loss).

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
A: Very few computers are sold nowadays without 3D acceleration and most games
require 3D acceleration. Therefore, most people who would play LAB3D/SDL
already have a 3D accelerator. To these people, software rendering is not of
any use. The original Ken's Labyrinth software renderer is very fast but
relies heavily on being able to access specific hardware directly (8086 or
compatible with VGA) in a very specific manner (VGA Mode X, maximum resolution
360x240). A rewritten software renderer may appear in future versions.

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
