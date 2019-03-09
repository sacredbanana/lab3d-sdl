<h1>LAB3D/SDL</h1>
<h1>=========</h1>

LAB3D/SDL is a port of Ken's Labyrinth to modern operating systems, using
OpenGL for graphics output and the SDL library to provide user input, sound
output, threading, and some graphics support functions. Music output is
through Adlib emulation or MIDI (MIDI only on Windows, Linux and other
operating systems with OSS-compatible sound APIs).

This code has been tested on Windows 98, Windows ME, Windows XP,
SuSE Linux 7.2 and 8.1, Debian Linux 2.2, SunOS 5.8 (Solaris 8),
FreeBSD 4.7 and Nintendo Switch.

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

Installation instructions can be found in install.txt.

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
Jared Stafford<br>https://jspenguin.org/                       | Danny Desse'
Cameron Armstrong (Nightfox)<br>http://minotaurcreative.net/   |