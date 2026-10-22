/*
 * libyukino -- portable screenshots
 * Copyright (C) 2026 Paper
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

extern "C" {
#include "yukino.h"
}

/* NOTE:
 *
 * Haiku does not support multiple monitors. Thus, we don't really
 * have to worry about trying to do that ... */

#include <AppDefs.h>
#include <Bitmap.h>
#include <Screen.h>
#include <List.h>
#include <Roster.h>
#include <InterfaceDefs.h>

/* ---- BEGIN PRIVATE API SHENANIGANS ---- */
/* Highway to the danger zone~~ */

#pragma pack(push, 1)
struct client_window_info {
	char dummy[36];
	/* Window position! */
	int32_t left, top, right, bottom;
	/* Hidden (int for some reason) */
	int32_t hidden;
	/* Minimized? */
	bool mini;
	/* stuff */
	float tab_height, border_size;
};
#pragma pack(pop)

/* Everything from these must be free'd with libc */
client_window_info *get_window_info(int32 token);

namespace BPrivate {

status_t get_window_order(int32 workspace, int32 **_tokens, int32 *_count);

};

/* ---- END PRIVATE API SHENANIGANS ---- */

extern "C" {
/* Define data specific to this connection */
struct yukino_connection_data {
	char dummy;
};
#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"
}

static yukino_result_t yukino_haiku_disconnect(yukino_connection_t *conn)
{
	free(conn);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_haiku_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	display_mode mode;
	BScreen scr;

	if (!scr.IsValid())
		return YUKINO_RESULT_UNSUPPORTED;

	if (scr.GetMode(&mode) != B_OK)
		return YUKINO_RESULT_UNSUPPORTED;

	*w = mode.virtual_width;
	*h = mode.virtual_height;
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* FIXME make this asynchronous -- ask on start and grab it on first iter */

struct yukino_window_iter {
	BMessage reply;

	int32_t *tokens;
	int32_t count;

	int32 index;
};

static yukino_result_t yukino_haiku_window_iter_start(yukino_connection_t *conn,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	struct yukino_window_iter *wi;

	wi = new struct yukino_window_iter;

	BMessenger msger("application/x-vnd.HAIKU-app_server");
	if (!msger.IsValid())
		return YUKINO_RESULT_UNSUPPORTED;

	BMessage req(B_GET_PROPERTY);
	req.AddSpecifier("Window");

	if (BPrivate::get_window_order(current_workspace(), &wi->tokens, &wi->count) != B_OK) {
		delete wi;
		return YUKINO_RESULT_UNSUPPORTED;
	}

	wi->index = wi->count - 1;

	*pwi = wi;
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_haiku_window_iter_unfiltered(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw)
{
	if (wi->index < 0)
		return YUKINO_RESULT_DONE;

	*pw = wi->tokens[wi->index--];
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_haiku_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw)
{
	return yukino_haiku_window_iter_unfiltered(conn, wi, pw);
}

static yukino_result_t yukino_haiku_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	free(wi->tokens);
	delete wi;
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* XXX: Need a "batch" API of sorts */

static yukino_result_t yukino_haiku_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	client_window_info *i = get_window_info((int32_t)(uint32_t)win);

	pr->x = i->left;
	pr->y = i->top;
	pr->w = i->right - i->left;
	pr->h = i->bottom - i->top;

	/* Half-assed */
	if (i->hidden || i->mini) {
		free(i);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	free(i);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_haiku_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	client_window_info *i = get_window_info((int32_t)(uint32_t)win);

	/* Apply border size */
	i->top -= i->border_size;
	i->bottom += i->border_size;
	i->left -= i->border_size;
	i->right += i->border_size;

	i->top -= i->tab_height;

	pr->x = i->left;
	pr->y = i->top;
	pr->w = i->right - i->left;
	pr->h = i->bottom - i->top;

	free(i);

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

struct yukino_screenshot {
	/* This is the result bitmap from haiku_screenshot */
	BBitmap *bmp;

	/* Memory buffer */
	struct yukino_image mbuf;
};

static yukino_result_t yukino_haiku_screenshot(yukino_connection_t *conn,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	BScreen scr;
	BRect rect;
	yukino_screenshot_t *s;
	uint32_t red_mask, green_mask, blue_mask;
	uint8_t bpp;
	yukino_result_t r;

	if ((r = yukino_screenshot_fix_resolution(conn, &w, &h)) < 0)
		return r;

	if (!scr.IsValid())
		return YUKINO_RESULT_UNSUPPORTED;

	rect.left = x;
	rect.right = x + w;
	rect.top = y;
	rect.bottom = y + h;

	s = (yukino_screenshot_t *)malloc(sizeof(*s));
	if (!s)
		return YUKINO_RESULT_UNSUPPORTED;

	/* We want a screenshot with no cursor
	 * ... but Haiku draws it anyway ... */
	if (scr.GetBitmap(&s->bmp, false, &rect) != B_OK) {
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	rect = s->bmp->Bounds();
	void *bits = s->bmp->Bits();

	/* TODO: need an API for paletted images */
	switch (s->bmp->ColorSpace()) {
	case B_RGB15:
		red_mask = 0x7C00;
		green_mask = 0x03E0;
		blue_mask = 0x001F;
		bpp = 15;
		break;
	case B_RGB32:
		red_mask = 0xFF0000;
		green_mask = 0x00FF00;
		blue_mask = 0x0000FF;
		bpp = 32;
		break;
	case B_CMAP8:
	default:
		/* Give up */
		delete s->bmp;
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	yukino_image(&s->mbuf, s->bmp->Bits(), bpp, red_mask, green_mask, blue_mask, YUKINO_IMAGE_ENDIAN_NATIVE, s->bmp->BytesPerRow(), 0, 0, w, h);

	*ps = s;
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_haiku_screenshot_resolution(
	yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h)
{
	return yukino_image_resolution(&s->mbuf, w, h);
}

static yukino_result_t yukino_haiku_screenshot_read(
	yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3])
{
	return yukino_image_read(&s->mbuf, rgb);
}

static yukino_result_t yukino_haiku_screenshot_delete(
	yukino_connection_t *conn, yukino_screenshot_t *s)
{
	delete s->bmp;
	free(s);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_haiku_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	int i;

	conn = (yukino_connection_t *)malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	/* Fill the vtable */
	conn->disconnect = yukino_haiku_disconnect;
	conn->display_resolution = yukino_haiku_display_resolution;

	conn->window_iter_start = yukino_haiku_window_iter_start;
	conn->window_iter = yukino_haiku_window_iter;
	conn->window_iter_end = yukino_haiku_window_iter_end;

	conn->window_position = yukino_haiku_window_position;
	conn->window_decorated_position = yukino_haiku_window_decorated_position;

	conn->lock = NULL;
	conn->unlock = NULL;

	conn->screenshot = yukino_haiku_screenshot;
	conn->screenshot_resolution = yukino_haiku_screenshot_resolution;
	conn->screenshot_read = yukino_haiku_screenshot_read;
	conn->screenshot_delete = yukino_haiku_screenshot_delete;

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
