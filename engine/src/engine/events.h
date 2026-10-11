/*
 *  This file is part of 'Johnny Reborn'
 *
 *  An open-source engine for the classic
 *  'Johnny Castaway' screensaver by Sierra.
 *
 *  Copyright (C) 2019 Jeremie GUILLAUME
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef EVENTS_H
#define EVENTS_H

#include "mytypes.h"

extern int evHotKeysEnabled;

/* Frames to run before shutting down cleanly with exit code 0. 0 = unlimited
 * (the shipping behaviour). Set by the `frames <N>` option; exists so an
 * automated test can assert an exit code instead of killing the process. */
extern uint32 evMaxFrames;

/* Start unthrottled, as the <M> hotkey does. Set by the `maxspeed` option so a
 * bounded run can cover many story iterations without minutes of wall clock. */
extern int evStartAtMaxSpeed;

/* Screensaver semantics: quit on mouse movement past a small dead-zone, or on
 * any click. Set by the Windows `/s` switch; off for every ordinary run. */
extern int evScreensaverMode;

void eventsInit(void);
void eventsWaitTick(uint16 delay);

#endif /* EVENTS_H */

