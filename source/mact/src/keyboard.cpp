/*
 * keyboard.c
 * MACT library -to- JonoF's Build Port Keyboard Glue
 *
 * by Jonathon Fowler
 *
 * Since we don't have the source to the MACT library I've had to
 * concoct some magic to glue its idea of controllers into that of
 * my Build port.
 *
 */
//-------------------------------------------------------------------------
/*
Duke Nukem Copyright (C) 1996, 2003 3D Realms Entertainment

This file is part of Duke Nukem 3D version 1.5 - Atomic Edition

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
*/
//-------------------------------------------------------------------------

#include "compat.h"

#include "keyboard.h"
#include "control.h"

kb_scancode KB_LastScan;

// translation table for taking key names to scancodes and back again
static struct
{
    const char *key;
    kb_scancode sc;
} sctokeylut[] = {
    { "Escape", 0x1 },     { "1", 0x2 },       { "2", 0x3 },       { "3", 0x4 },       { "4", 0x5 },       { "5", 0x6 },
    { "6", 0x7 },          { "7", 0x8 },       { "8", 0x9 },       { "9", 0xa },       { "0", 0xb },       { "-", 0xc },
    { "=", 0xd },          { "BakSpc", 0xe },  { "Tab", 0xf },     { "Q", 0x10 },      { "W", 0x11 },      { "E", 0x12 },
    { "R", 0x13 },         { "T", 0x14 },      { "Y", 0x15 },      { "U", 0x16 },      { "I", 0x17 },      { "O", 0x18 },
    { "P", 0x19 },         { "[", 0x1a },      { "]", 0x1b },      { "Enter", 0x1c },  { "LCtrl", 0x1d },  { "A", 0x1e },
    { "S", 0x1f },         { "D", 0x20 },      { "F", 0x21 },      { "G", 0x22 },      { "H", 0x23 },      { "J", 0x24 },
    { "K", 0x25 },         { "L", 0x26 },      { ";", 0x27 },      { "'", 0x28 },      { "`", 0x29 },      { "LShift", 0x2a },
    { "Backslash", 0x2b }, { "Z", 0x2c },      { "X", 0x2d },      { "C", 0x2e },      { "V", 0x2f },      { "B", 0x30 },
    { "N", 0x31 },         { "M", 0x32 },      { ",", 0x33 },      { ".", 0x34 },      { "/", 0x35 },      { "RShift", 0x36 },
    { "Kpad*", 0x37 },     { "LAlt", 0x38 },   { "Space", 0x39 },  { "CapLck", 0x3a }, { "F1", 0x3b },     { "F2", 0x3c },
    { "F3", 0x3d },        { "F4", 0x3e },     { "F5", 0x3f },     { "F6", 0x40 },     { "F7", 0x41 },     { "F8", 0x42 },
    { "F9", 0x43 },        { "F10", 0x44 },    { "NumLck", 0x45 }, { "ScrLck", 0x46 }, { "Kpad7", 0x47 },  { "Kpad8", 0x48 },
    { "Kpad9", 0x49 },     { "Kpad-", 0x4a },  { "Kpad4", 0x4b },  { "Kpad5", 0x4c },  { "Kpad6", 0x4d },  { "Kpad+", 0x4e },
    { "Kpad1", 0x4f },     { "Kpad2", 0x50 },  { "Kpad3", 0x51 },  { "Kpad0", 0x52 },  { "Kpad.", 0x53 },  { "F11", 0x57 },
    { "F12", 0x58 },       { "KpdEnt", 0x9c }, { "RCtrl", 0x9d },  { "Kpad/", 0xb5 },  { "RAlt", 0xb8 },   { "PrtScn", 0xb7 },
    { "Pause", 0xc5 },     { "Home", 0xc7 },   { "Up", 0xc8 },     { "PgUp", 0xc9 },   { "Left", 0xcb },   { "Right", 0xcd },
    { "End", 0xcf },       { "Down", 0xd0 },   { "PgDn", 0xd1 },   { "Insert", 0xd2 }, { "Delete", 0xd3 },
};

// this is horrible!
const char *KB_ScanCodeToString(kb_scancode scancode)
{
    for (size_t s = 0; s < ARRAY_SIZE(sctokeylut); s++)
        if (sctokeylut[s].sc == scancode)
            return sctokeylut[s].key;

    return "";
}

kb_scancode KB_StringToScanCode(const char * string)
{
    for (size_t s = 0; s < ARRAY_SIZE(sctokeylut); s++)
        if (!Bstrcasecmp(sctokeylut[s].key, string))
            return sctokeylut[s].sc;

    return 0;
}
