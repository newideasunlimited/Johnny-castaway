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

#ifndef DUMP_H
#define DUMP_H

#include "mytypes.h"

void dumpAllResources(void);
/*  generateXpm() was declared here and defined nowhere in the tree - dead in the
 *  strongest sense, since any caller would have failed to link. dumpBmp() and
 *  dumpScr() each carry their own copy of the XPM-writing loop; this looks like
 *  the prototype of a factor-out that never happened. Removed rather than left
 *  as an invitation. */

#endif /* DUMP_H */

