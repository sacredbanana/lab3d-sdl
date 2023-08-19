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
graphics card capable of OpenGL.macOS and Nintendo Switch is also supported.

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

Operating system:	Windows 95/98/Me/XP, Linux, Solaris, macOS,BSD, ...
Libraries:		OpenGL 1.2, GLU 1.3, SDL 2.0. Slightly older versions
			of GLU may work.
Compiler:		GCC 2.95.2 or later recommended. Other compilers will
			require Makefile changes, but should work.
Other utilities:	Makefiles require GNU Make (or compatible) and sh
			(or compatible, e.g. bash). Cmake for creating the build files.
Data files:		
			(No longer needed as a separate download as all files are now included.)
			Ken's Labyrinth v2.1 (Epic Megagames registered),
			Ken's Labyrinth v2.0 (Epic Megagames shareware),
			Ken's Labyrinth v1.1 (Advanced Systems registered) or
			Ken's Labyrinth v1.0 (Advanced Systems shareware).

# Installation

<h1>macOS and Linux systems note:</h1>
Before the game will run you will need to install SDL2 and SDL2_Image runtime binaries on your system.


## Compiling (Windows)

Install Visual Studio 2017 or 2019 with Visual-C++ and CMake support.
Close the initial splash screen by clicking the "Continue without code" option.
Click File -> Open -> CMake. Choose the CMakeLists.txt file. The project will then
open and you'll be able to select from the top menu the flavour of build. From there
you can build and or/debug.

## Compiling (Windows) (Legacy)

Run "make -f Makefile.Win32" in the source directory. MinGW 3.1.0 is
recommended for Windows use.

## Compiling (Linux/UNIX)
Intstall the prerequisited with the following command:
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

## Compiling (Unix with OSS support) (Legacy)
Run `make` in the source directory.

## Compiling (Unix without OSS support) (Legacy)

Run `make -f Makefile.NoMIDI` in the source directory. Note that General MIDI
music is not available if you do this (not much of a loss).

## Compiling (Nintendo Switch)
Install DevKitPro and then run `make -f Makefile.Switch` in the source directory.

# Installation (Nintendo Switch)

To play this on your Nintendo Switch you will need a Switch set up to run homebrew software, then do any
of these three choices:
- Download the latest release of the game from https://github.com/sacredbanana/lab3d-sdl/releases,
- Download the game from the Homebrew Appstore. This is the easiest option and will also install the game for you automatically.
Or
- Compile the game yourself (see the Nintendo Switch compiling instructions above)

Navigate to the Switch folder inside your Nintendo Switch SD card and create a new folder
called Kens-Labyrinth. Inside this folder, transfer Kens-Labyrinth.nro and all of Ken's Labyrinth's data files. (This is the "gamedata" directory
if you wish to have the game launcher. Otherwise just copy a single version of Ken's labyrinth to the directory containing the executable
WITHOUT including the gamedata directory.)

# Setup

Before running the game, it may be a good idea to check the settings by
running the setup routine. If you have a script or batch file named "setup" or
"setup.bat", you can use this to start the setup routine. Otherwise, run the
main program with command-line parameter "-setup".

The setup routine is automatically run if no configuration file is found, so
you can force setup to run the next time you start LAB3D/SDL by deleting
the settings file ("settings.ini").
Execution instructions can be found in run.txt.

Frequently asked questions are answered in faq.txt.

Technical comments about Ken's Labyrinth and LAB3D/SDL can be found in
comments.txt.

The license can be found in LICENSE.

<h2>Credits</h2>

<h3>Ken's Labyrinth</h3>

Design, code and Adlib emulation            | Artwork       | Board maps    | Sound effects | Music
------------------------------------------- | ------------- | ------------- | ------------- | -------------
|Ken Silverman<br>http://www.advsys.net/ken | Mikko Iho     | Andrew Cotter | Ken Silverman | Ken Silverman
|                                           | Ken Silverman |               | Andrew Cotter |
|                                           | Andrew Cotter |               |               |

<h3>LAB3D/SDL</h3>

Code                                                           | Testing
-------------------------------------------------------------- | -----------------
Jan Lönnberg<br>http://koti.mbnet.fi/lonnberg/                 | Ken Silverman
Katie Stafford<br>https://ktpanda.org/                       | Danny Desse'
Cameron Armstrong (Nightfox)<br>http://minotaurcreative.net/   |
