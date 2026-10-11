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

#ifndef STORY_H
#define STORY_H

void storyPlay(void);

// Holiday override (affects island decorations)
//   -1 : automatic (use system date)
//    0 : force no holiday decorations
//  1..4 : force a specific holiday (see island.c: 1=Halloween, 2=St Patrick, 3=Christmas, 4=New Year)
void storySetForcedHoliday(int holiday);

/* Force the night backdrop on (1) or off (0); -1 restores the clock-derived
 * default of 21:00-05:59. Set by the `night` / `day` options. */
void storySetForcedNight(int night);

// Sync the island state (night + holiday) from the current clock.
// This is normally called by storyPlay(), but can also be useful for
// ad-hoc modes that display the island (e.g. `jc_reborn island ads ...`).
void storyUpdateIslandFromDateAndTime(void);

#endif /* STORY_H */
