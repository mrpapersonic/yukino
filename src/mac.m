/*
 * yukino -- portable screenshot utility
 * Copyright (C) 2026 Paper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <SDL3/SDL.h>

/* Doop */
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

/* If we don't do this, the window isnt' actually always on top */
void mac_set_window_always_on_top(SDL_Window *win)
{
	/* Fuck */
	SDL_PropertiesID prop;
	NSWindow *nswin;

	prop = SDL_GetWindowProperties(win);

	nswin = (NSWindow *)SDL_GetPointerProperty(prop, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	if (!nswin)
		return;

	[nswin setLevel:NSStatusWindowLevel];
}
