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
#include <SDL3/SDL_main.h>

#include "yukino.h"

#include <stdio.h>

/* ------------------------------------------------------------------------ */
/* grab the whole screen and shove it into a SDL_Surface */

struct sdl_take_pixel {
	SDL_Surface *sur;
	int x, y;
};

static yukino_result_t sdl_take_pixel(void *userdata,
	unsigned char rgb[3])
{
	struct sdl_take_pixel *sur = userdata;
	uint32_t *px;

	if (sur->x == sur->sur->w) {
		sur->y++;
		sur->x = 0;
	}

	px = (uint32_t *)((char *)sur->sur->pixels + (sur->sur->pitch * sur->y)) + sur->x;

	*px = 0xFF000000 | ((uint32_t)rgb[2] << 16) | ((uint32_t)rgb[1] << 8) | rgb[0];

	sur->x++;
	return YUKINO_RESULT_OK;
}

static SDL_Surface *sdl_yukino(yukino_connection_t *conn,
	uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	struct sdl_take_pixel s;

	s.sur = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
	if (!s.sur)
		return NULL;

	s.x = s.y = 0;

	if (yukino_take(conn, x, y, w, h, sdl_take_pixel, &s) < 0) {
		SDL_DestroySurface(s.sur);
		return NULL;
	}

	return s.sur;
}

/* Takes a yukino of the whole display */
static SDL_Surface *sdl_yukino_display(yukino_connection_t *conn)
{
	yukino_result_t r;
	uint32_t w, h;

	if ((r = yukino_display_resolution(conn, &w, &h)) < 0)
		return NULL;

	return sdl_yukino(conn, 0, 0, w, h);
}

/* ------------------------------------------------------------------------ */
/* save a portion of an SDL_Surface into a .png (or, really anything) file */

static yukino_result_t sdl_take_cb(void *conn, uint32_t x, uint32_t y,
	uint32_t w, uint32_t h, yukino_take_pixel pixel_func, void *userdata)
{
	SDL_Surface *sur = conn;
	uint32_t *pixels;
	uint32_t i, j;
	yukino_result_t r;

	/* XXX need to make sure width/height are ok */
	pixels = sur->pixels;

	/* go go go */
	pixels += x;
	pixels = (uint32_t *)((char *)pixels + (y * sur->pitch));

	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			unsigned char rgb[3];

			rgb[2] = pixels[j] >> 16;
			rgb[1] = pixels[j] >> 8;
			rgb[0] = pixels[j];

			if ((r = pixel_func(userdata, rgb)) < 0)
				return r;
		}
		pixels = (uint32_t *)((char *)pixels + sur->pitch);
	}

	return YUKINO_RESULT_OK;
}

static yukino_result_t stdio_write_cb(void *opaque, const void *data, size_t sz)
{
	if (fwrite(data, 1, sz, opaque) != sz)
		return YUKINO_RESULT_FILE_ERROR;
	return YUKINO_RESULT_OK;
}

static void sdl_write_surface_to_png(const char *f, SDL_Surface *sur,
	SDL_FRect *rect)
{
	FILE *fp;

	fp = fopen(f, "wb");
	if (!fp)
		return; /* oops */

	yukino_write_png(rect->x, rect->y, rect->w, rect->h, stdio_write_cb, fp, sdl_take_cb, sur);

	fclose(fp);
}

/* ------------------------------------------------------------------------ */
/* file dialog */

struct dialog_cb {
	/* Contains the full screenshot of the desktop */
	SDL_Surface *sur;

	/* The selection to save */
	SDL_FRect sel;

	char *file;
	int done;
};

static void dialog_cb(void *userdata, const char *const *filelist, int filter)
{
	volatile struct dialog_cb *c = (volatile struct dialog_cb *)userdata;

	if (filelist && *filelist) {
		/* filelist[0] = the file path to save in */
		c->file = strdup(filelist[0]);
	}

	c->done = 1;
}

/* ------------------------------------------------------------------------ */
/* window shit */

/* window lookup ... */
struct window {
	yukino_window_t win;

	int32_t x, y;
	uint32_t w, h;
};

static struct window *windows;
static size_t windows_size;
static size_t windows_alloc;

static void windows_fill(yukino_connection_t *conn)
{
	yukino_window_iter_t *wi;
	yukino_result_t rr;
	yukino_window_t win;

	if ((rr = yukino_window_iter_start(conn, NULL, &wi)) < 0)
		return;

	while ((rr = yukino_window_iter(conn, wi, &win)) == YUKINO_RESULT_OK) {
		int32_t wx, wy;
		uint32_t ww, wh;

		if ((rr = yukino_window_position(conn, win, &wx, &wy, &ww, &wh)) < 0)
			continue; /* ??? */

		/* allocate more space? */
		if (windows_size >= windows_alloc) {
			void *old = windows;

			windows_alloc = (windows_alloc) ? (windows_alloc * 2) : 16;
			windows = realloc(windows, windows_alloc * sizeof(*windows));

			if (!windows) {
				free(old);
				windows = NULL;
				windows_alloc = windows_size = 0;
				return;
			}
		}

		windows[windows_size].x = wx;
		windows[windows_size].y = wy;
		windows[windows_size].w = ww;
		windows[windows_size].h = wh;
		windows[windows_size].win = win;

		windows_size++;
	}

	yukino_window_iter_end(conn, wi);
}

static yukino_result_t windows_query_at_point(int32_t x, int32_t y,
	int32_t *px, int32_t *py, uint32_t *pw, uint32_t *ph)
{
	/* there may be no window under the pointer -- in that case return
	 * YUKINO_RESULT_NONE */
	yukino_result_t r;
	size_t i;

	r = YUKINO_RESULT_NONE;

	for (i = 0; i < windows_size; i++) {
		struct window *win = &windows[i];

		/* check if our coordinates are inside the window */
		if (!((x >= win->x) && (x <= (win->x + win->w)) && (y >= win->y) && (y <= (win->y + win->h))))
			continue; /* ignore */

		*px = win->x;
		*py = win->y;
		*pw = win->w;
		*ph = win->h;

		r = YUKINO_RESULT_OK;
	}

	return r;
}

/* ------------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
	SDL_Surface *sur;
	SDL_Texture *tex;
	SDL_Window *win;
	SDL_Renderer *ren;
	SDL_Event ev;
	SDL_Cursor *cur;
	SDL_FRect sel;
	yukino_connection_t *conn;
	/* Mouse down, drag */
	enum { POINTS_DOWN, POINTS_DRAG, POINTS_MAX_ };
	SDL_FPoint points[POINTS_MAX_];
	int down = 0, drag = 0;

	if (yukino_connect(&conn) < 0)
		return 1; /* oops */

	yukino_lock(conn);

	windows_fill(conn);

	sur = sdl_yukino_display(conn);

	yukino_unlock(conn);

	SDL_CreateWindowAndRenderer("yukino", sur->w, sur->h, SDL_WINDOW_FULLSCREEN, &win, &ren);

	tex = SDL_CreateTextureFromSurface(ren, sur);

	cur = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
	SDL_SetCursor(cur);

	do {
		float mx, my;
		int32_t wx, wy;
		uint32_t ww, wh;

		SDL_GetMouseState(&mx, &my);

		/* Adjust selection */
		if (drag) {
			/* Lol wow SDL has a function for this */
			SDL_GetRectEnclosingPointsFloat(points, POINTS_MAX_, NULL, &sel);
		} else if (windows_query_at_point(mx, my, &wx, &wy, &ww, &wh) == YUKINO_RESULT_OK) {
			sel.x = wx;
			sel.y = wy;
			sel.w = ww;
			sel.h = wh;
		} else {
			/* Otherwise the "selection" is the window beneath the
			 * cursor. */
			sel.x = sel.y = 0;
			sel.w = sur->w;
			sel.h = sur->h;
		}

		/* Blit. */
		SDL_RenderClear(ren);

		/* Apparently not supported everywhere... we'd need to store a separate texture
		 * thats at half visible if we want to support those platforms */
		SDL_SetTextureColorMod(tex, 127, 127, 127);
		SDL_RenderTexture(ren, tex, NULL, NULL);

		SDL_SetTextureColorMod(tex, 255, 255, 255);
		SDL_RenderTexture(ren, tex, &sel, &sel);

		SDL_RenderPresent(ren);

		switch (ev.type) {
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			down = 1;
			points[POINTS_DOWN].x = ev.button.x;
			points[POINTS_DOWN].y = ev.button.y;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (down)
				drag = 1;
			points[POINTS_DRAG].x = ev.motion.x;
			points[POINTS_DRAG].y = ev.motion.y;
			break;
		case SDL_EVENT_KEY_DOWN:
			if (ev.key.key == SDLK_ESCAPE)
				break;
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			goto out;
		}
	} while (SDL_WaitEvent(&ev));

out: ;
	/* We're done here, and hopefully we have a selection (right?)
	 * So, take what we have, and shove it into the png writer */

	/* No longer need any of this */
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_DestroyCursor(cur);
	yukino_disconnect(conn);

	/* Ask the user where to save the damn file */
	volatile struct dialog_cb c;
	c.sur = sur;
	c.sel = sel;
	c.done = 0;
	c.file = NULL;

	static const SDL_DialogFileFilter filters[] = {
		{"PNG (Portable Network Graphics)", "png"},
		{"All files", "*"}
	};

	SDL_ShowSaveFileDialog(dialog_cb, (void *)&c, NULL, filters, SDL_arraysize(filters), NULL);

	/* Busy wait for the file dialog to do its thing */
	while (!c.done) {
		/* Handle events here */
		while (SDL_PollEvent(&ev));

		/* Then sleep... */
		SDL_Delay(10);
	}

	/* Save it */
	if (c.file) {
		sdl_write_surface_to_png(c.file, sur, &sel);
		free(c.file);
	}

	SDL_DestroySurface(sur);
}
