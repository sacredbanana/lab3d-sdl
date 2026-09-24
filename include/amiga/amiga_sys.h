#ifndef LAB3D_AMIGA_SYS_H
#define LAB3D_AMIGA_SYS_H

/*
 * All the AmigaOS headers the port needs, in one place.
 *
 * This must be included *before* lab3d.h in the Amiga back ends.  lab3d.h
 * defines texture names as bare macros - `target`, `green`, `menu`, `clock`
 * and friends - which would otherwise rewrite identifiers inside the system
 * headers and produce a wall of nonsense errors.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/devices.h>
#include <exec/execbase.h>

#include <dos/dos.h>

#include <devices/timer.h>
#include <devices/audio.h>
#include <devices/inputevent.h>

#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <graphics/videocontrol.h>

#include <intuition/intuition.h>
#include <intuition/screens.h>

#include <libraries/asl.h>
#include <libraries/lowlevel.h>
#include <libraries/cybergraphics.h>

#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/timer.h>
#include <proto/asl.h>
#include <proto/lowlevel.h>
#include <proto/cybergraphics.h>

#endif /* LAB3D_AMIGA_SYS_H */
