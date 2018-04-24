//-------------------------------------------------------------------------
/*
Copyright (C) 1997, 2005 - 3D Realms Entertainment

This file is part of Shadow Warrior version 1.2

Shadow Warrior is free software; you can redistribute it and/or
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

Original Source: 1997 - Frank Maddin and Jim Norwood
Prepared for public release: 03/28/2005 - Charlie Wiederhold, 3D Realms
*/
//-------------------------------------------------------------------------

void DoQuakeMatch(short match);
void ProcessQuakeOn(void);
void ProcessQuakeSpot(void);
void QuakeViewChange(PLAYERp pp, int *z_diff, int *x_diff, int *y_diff, short *ang_diff);
void DoQuake(PLAYERp pp);
SWBOOL SetQuake(PLAYERp pp, short tics, short amt);
int SetExpQuake(int16_t Weapon);
int SetGunQuake(int16_t SpriteNum);
int SetPlayerQuake(PLAYERp mpp);
int SetNuclearQuake(int16_t Weapon);
int SetSumoQuake(int16_t SpriteNum);
int SetSumoFartQuake(int16_t SpriteNum);

