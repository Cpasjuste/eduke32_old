//-------------------------------------------------------------------------
/*
Copyright (C) 1996, 2003 - 3D Realms Entertainment

This file is NOT part of Duke Nukem 3D version 1.5 - Atomic Edition
However, it is either an older version of a file that is, or is
some test code written during the development of Duke Nukem 3D.
This file is provided purely for educational interest.

Duke Nukem 3D is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Prepared for public release: 03/21/2003 - Charlie Wiederhold, 3D Realms
*/
//-------------------------------------------------------------------------

//****************************************************************************
//
// Private header for CONTROL.C
//
//****************************************************************************

#pragma once

#ifndef control_private_h_
#define control_private_h_

#include "compat.h"
#include "keyboard.h"

#ifdef __cplusplus
extern "C" {
#endif


//****************************************************************************
//
// DEFINES
//
//****************************************************************************

#define AXISUNDEFINED   0x7f
#define BUTTONUNDEFINED 0x7f
#define KEYUNDEFINED    0x7f

#define THRESHOLD        (0x200 * 32767 / 10000)
#define MINTHRESHOLD     (0x80 * 32767 / 10000)

#define DEFAULTMOUSESENSITIVITY 4 // 0x7000+MINIMUMMOUSESENSITIVITY

#define INSTANT_ONOFF       0
#define TOGGLE_ONOFF        1

#define MAXCONTROLVALUE  0x7fff

// Number of Mouse buttons

//#define MAXMOUSEBUTTONS 10

// Number of JOY buttons
// KEEPINSYNC duke3d/src/gamedefs.h, build/src/sdlayer.cpp
#define MAXJOYBUTTONS 32
#define MAXJOYBUTTONSANDHATS (MAXJOYBUTTONS+4)

// Number of JOY axes
// KEEPINSYNC duke3d/src/gamedefs.h, build/src/sdlayer.cpp
#define MAXJOYAXES 9
#define MAXJOYDIGITAL (MAXJOYAXES*2)

// NORMAL axis scale
#define NORMALAXISSCALE 65536

#define BUTTONSET(x, value) (CONTROL_ButtonState |= ((uint64_t)value << ((uint64_t)(x))))
#define BUTTONCLEAR(x) (CONTROL_ButtonState &= ~((uint64_t)1 << ((uint64_t)(x))))
#define BUTTONHELDSET(x, value) (CONTROL_ButtonHeldState |= (uint64_t)(value << ((uint64_t)(x))))
#define LIMITCONTROL(x) (*x = clamp(*x, -MAXCONTROLVALUE, MAXCONTROLVALUE))

//****************************************************************************
//
// TYPEDEFS
//
//****************************************************************************

typedef enum
{
    motion_Left = -1,
    motion_Up = -1,
    motion_None = 0,
    motion_Right = 1,
    motion_Down = 1
} motion;


typedef struct
{
    int32_t joyMinX;
    int32_t joyMinY;
    int32_t threshMinX;
    int32_t threshMinY;
    int32_t threshMaxX;
    int32_t threshMaxY;
    int32_t joyMaxX;
    int32_t joyMaxY;
    int32_t joyMultXL;
    int32_t joyMultYL;
    int32_t joyMultXH;
    int32_t joyMultYH;
} JoystickDef;

//   int32_t ThrottleMin;
//   int32_t RudderMin;
//   int32_t ThrottlethreshMin;
//   int32_t RudderthreshMin;
//   int32_t ThrottlethreshMax;
//   int32_t RudderthreshMax;
//   int32_t ThrottleMax;
//   int32_t RudderMax;
//   int32_t ThrottleMultL;
//   int32_t RudderMultL;
//   int32_t ThrottleMultH;
//   int32_t RudderMultH;


typedef struct
{
    uint8_t active;
    uint8_t used;
    uint8_t toggle;
    uint8_t buttonheld;
    int32_t cleared;
} controlflags;

typedef struct
{
    kb_scancode keyPrimary;
    kb_scancode keySecondary;
} controlkeymaptype;

typedef struct
{
    uint8_t singleclicked;
    uint8_t doubleclicked;
    uint16_t extra;
} controlbuttontype;

typedef struct
{
    uint8_t analogmap;
    uint8_t minmap;
    uint8_t maxmap;
    uint8_t extra;
} controlaxismaptype;

typedef struct
{
    int32_t analog;
    int8_t digital;
    int8_t digitalClearedN, digitalClearedP;
} controlaxistype;


//***************************************************************************
//
// PROTOTYPES
//
//***************************************************************************

#ifdef __cplusplus
}
#endif
#endif
