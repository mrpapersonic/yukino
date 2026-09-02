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

#include "yukino.h"

#include <stdlib.h>
#include <limits.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>

enum {
	ATOM_NET_CLIENT_LIST, /* actually _NET_CLIENT_LIST_STACKING */

	/* array bounds */
	ATOM_MAX_,
};

static struct {
	const char *name;
	size_t len;
} atom_names[ATOM_MAX_] = {
#define NAME(x) { x, sizeof(x) - 1 }
	NAME("_NET_CLIENT_LIST_STACKING"),
#undef NAME
};

/* Define data specific to this connection */
struct yukino_connection_data {
	xcb_connection_t *conn;
	int default_display;
	xcb_screen_t *default_display_screen; /* cache this */

	uint32_t red_mask;
	uint32_t green_mask;
	uint32_t blue_mask;

	uint32_t red_shift;
	uint32_t green_shift;
	uint32_t blue_shift;

	uint32_t red_bits;
	uint32_t green_bits;
	uint32_t blue_bits;

	/* :) */
	xcb_atom_t atoms[ATOM_MAX_];
};
#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"

/* xcb helper functions -- dont mind this */

/* teehee */
static xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen)
{
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; screen--, xcb_screen_next(&iter))
		if (screen == 0)
			return iter.data;

	return NULL;
}

static xcb_visualtype_t *find_visual(xcb_screen_t *screen)
{
	/* Horrible */
	xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);

	for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
		xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
		for (; visual_iter.rem; xcb_visualtype_next(&visual_iter))
			if (screen->root_visual == visual_iter.data->visual_id)
				return visual_iter.data;
	}

	return NULL;
}

static yukino_result_t yukino_xcb_disconnect(yukino_connection_t *conn)
{
	xcb_disconnect(conn->conn_data.conn);
	free(conn);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_display_resolution(yukino_connection_t *conn,
		uint32_t *w, uint32_t *h)
{
	xcb_screen_t *scr = conn->conn_data.default_display_screen;

	/* Huh? */
	if (!scr)
		return YUKINO_RESULT_UNSUPPORTED;

	*w = scr->width_in_pixels;
	*h = scr->height_in_pixels;

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

struct yukino_window_iter {
	enum { WITER_QUERY_TREE, WITER_NET_CLIENT_LIST } type;

	union {
		struct {
			xcb_query_tree_cookie_t cookie;
			xcb_query_tree_reply_t *reply;
		} qtree;
		struct {
			xcb_get_property_cookie_t cookie;
			xcb_get_property_reply_t *reply;
		} nlist;
	} u;

	xcb_window_t *w;
	xcb_get_window_attributes_cookie_t *wacs;
	int wlen;
	int wit;
};

/* Here because we fallback if NET_CLIENT_LIST fails for whatever reason */
static void query_tree(yukino_connection_t *conn, yukino_window_iter_t *wi, const yukino_window_t *win)
{
	wi->type = WITER_QUERY_TREE;

	wi->u.qtree.cookie = xcb_query_tree(conn->conn_data.conn, win ? *win : conn->conn_data.default_display_screen->root);
}

static yukino_result_t yukino_xcb_window_iter_start(
		yukino_connection_t *conn,
		const yukino_window_t *win,
		yukino_window_iter_t **pwi)
{
	yukino_window_iter_t *wi;

	if (!pwi)
		return YUKINO_RESULT_INVALID_PARAM;

	wi = calloc(1, sizeof(*wi));
	if (!wi)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	/* XXX: wouldn't it be better to just list toplevels when `win` is NULL */
	if (!win && conn->conn_data.atoms[ATOM_NET_CLIENT_LIST]) {
		/* Try _NET_CLIENT_LIST */
		wi->type = WITER_NET_CLIENT_LIST;

		wi->u.nlist.cookie = xcb_get_property(conn->conn_data.conn, 0, conn->conn_data.default_display_screen->root, conn->conn_data.atoms[ATOM_NET_CLIENT_LIST], XCB_ATOM_ANY, 0L, UINT_MAX);
	} else {
		query_tree(conn, wi, win);
	}

	*pwi = wi;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_window_iter(
		yukino_connection_t *conn, yukino_window_iter_t *wi,
		yukino_window_t *pw)
{
	if (!wi || !pw)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!wi->w) {
		switch (wi->type) {
		case WITER_NET_CLIENT_LIST:
			wi->u.nlist.reply = xcb_get_property_reply(conn->conn_data.conn, wi->u.nlist.cookie, NULL);
			if (wi->u.nlist.reply) {
				wi->w = xcb_get_property_value(wi->u.nlist.reply);
				wi->wlen = xcb_get_property_value_length(wi->u.nlist.reply) / sizeof(xcb_window_t);
				break;
			}

			query_tree(conn, wi, NULL);
			/* fallthrough */
		case WITER_QUERY_TREE:
			/* Are you, are you */
			wi->u.qtree.reply = xcb_query_tree_reply(conn->conn_data.conn, wi->u.qtree.cookie, NULL);
			if (!wi->u.qtree.reply) /* uh oh */
				return YUKINO_RESULT_UNSUPPORTED;

			wi->w = xcb_query_tree_children(wi->u.qtree.reply);
			wi->wlen = xcb_query_tree_children_length(wi->u.qtree.reply);
			break;
		}

		wi->wit = 0;
		wi->wacs = malloc(sizeof(*wi->wacs) * wi->wlen);
		if (wi->wacs) {
			int i;

			/* Could you pay me in advance? */
			for (i = 0; i < wi->wlen; i++)
				wi->wacs[i] = xcb_get_window_attributes(conn->conn_data.conn, wi->w[i]);
		}
	}

	for (; wi->wit < wi->wlen; wi->wit++) {
		xcb_get_window_attributes_cookie_t wacookie;
		xcb_get_window_attributes_reply_t *wareply;

		wacookie = (wi->wacs) ? wi->wacs[wi->wit]
			: xcb_get_window_attributes(conn->conn_data.conn, wi->w[wi->wit]);

		wareply = xcb_get_window_attributes_reply(conn->conn_data.conn, wacookie, NULL);
		if (!wareply)
			continue; /* ??? */

		if (wareply->map_state != XCB_MAP_STATE_VIEWABLE)
			continue;

		*pw = wi->w[wi->wit++];
		return YUKINO_RESULT_OK;
	}

	return YUKINO_RESULT_DONE;
}

static yukino_result_t yukino_xcb_window_iter_end(
		yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	if (!wi)
		return YUKINO_RESULT_INVALID_PARAM;

	free(wi->wacs);

	switch (wi->type) {
	case WITER_QUERY_TREE:
		free(wi->u.qtree.reply);
		break;
	case WITER_NET_CLIENT_LIST:
		free(wi->u.nlist.reply);
		break;
	}

	free(wi);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* XXX: Need a "batch" API of sorts */

static yukino_result_t yukino_xcb_window_position(
	yukino_connection_t *conn, yukino_window_t win,
	int32_t *x, int32_t *y, uint32_t *w, uint32_t *h)
{
	xcb_get_geometry_cookie_t cookie;
	xcb_get_geometry_reply_t *reply;
	xcb_translate_coordinates_cookie_t trcookie;
	xcb_translate_coordinates_reply_t *trreply;

	cookie = xcb_get_geometry(conn->conn_data.conn, win);
	trcookie = xcb_translate_coordinates(conn->conn_data.conn, win, conn->conn_data.default_display_screen->root, 0, 0);

	reply = xcb_get_geometry_reply(conn->conn_data.conn, cookie, NULL);
	trreply = xcb_translate_coordinates_reply(conn->conn_data.conn, trcookie, NULL);
	if (!reply || !trreply) {
		free(reply);
		free(trreply);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	*x = trreply->dst_x;
	*y = trreply->dst_y;
	*w = reply->width;
	*h = reply->height;

	free(reply);
	free(trreply);

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

static yukino_result_t yukino_xcb_lock(yukino_connection_t *conn)
{
	xcb_grab_server(conn->conn_data.conn);
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_xcb_unlock(yukino_connection_t *conn)
{
	xcb_ungrab_server(conn->conn_data.conn);
	xcb_flush(conn->conn_data.conn);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

static yukino_result_t yukino_xcb_take(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_take_pixel pixel_func, void *userdata)
{
	xcb_image_t *img;

	img = xcb_image_get(conn->conn_data.conn, conn->conn_data.default_display_screen->root,
		x, y, w, h, 0xFFFFFFFF, XCB_IMAGE_FORMAT_Z_PIXMAP);
	if (!img)
		return YUKINO_RESULT_INVALID_PARAM; /* probably */

	/* XXX would be nice to optimize for the common case */
	for (y = 0; y < img->height; y++) {
		for (x = 0; x < img->width; x++) {
			yukino_result_t r;
			uint32_t pxl;
			unsigned char rgb[3];

			pxl = xcb_image_get_pixel(img, x, y);

#define SCALE(x, color) ((((x) & conn->conn_data.color##_mask) >> conn->conn_data.color##_shift) * 255 / ((1 << conn->conn_data.color##_bits) - 1))
			rgb[0] = SCALE(pxl, red);
			rgb[1] = SCALE(pxl, green);
			rgb[2] = SCALE(pxl, blue);
#undef SCALE

			if ((r = pixel_func(userdata, rgb)) < 0) {
				xcb_image_destroy(img);
				return r;
			}
		}
	}

	xcb_image_destroy(img);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_xcb_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	xcb_visualtype_t *vistype;
	xcb_intern_atom_cookie_t atom_cookies[ATOM_MAX_];
	int i;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	conn->conn_data.conn = xcb_connect(NULL, &conn->conn_data.default_display);
	if (!conn->conn_data.conn) {
		free(conn);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	/* Cache this at startup */
	conn->conn_data.default_display_screen = screen_of_display(conn->conn_data.conn, conn->conn_data.default_display);

	for (i = 0; i < ATOM_MAX_; i++)
		atom_cookies[i] = xcb_intern_atom(conn->conn_data.conn, 1, atom_names[i].len, atom_names[i].name);

	/* Also cache this ... */
	vistype = find_visual(conn->conn_data.default_display_screen);

	if (!vistype) {
		xcb_disconnect(conn->conn_data.conn);
		free(conn);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	conn->conn_data.red_mask = vistype->red_mask;
	conn->conn_data.green_mask = vistype->green_mask;
	conn->conn_data.blue_mask = vistype->blue_mask;

	conn->conn_data.red_shift = __builtin_ctz(conn->conn_data.red_mask);
	conn->conn_data.green_shift = __builtin_ctz(conn->conn_data.green_mask);
	conn->conn_data.blue_shift = __builtin_ctz(conn->conn_data.blue_mask);

	conn->conn_data.red_bits = __builtin_popcount(conn->conn_data.red_mask);
	conn->conn_data.green_bits = __builtin_popcount(conn->conn_data.green_mask);
	conn->conn_data.blue_bits = __builtin_popcount(conn->conn_data.blue_mask);

	/* Fill the vtable */
	conn->disconnect = yukino_xcb_disconnect;
	conn->display_resolution = yukino_xcb_display_resolution;

	conn->window_iter_start = yukino_xcb_window_iter_start;
	conn->window_iter = yukino_xcb_window_iter;
	conn->window_iter_end = yukino_xcb_window_iter_end;

	conn->window_position = yukino_xcb_window_position;

	conn->lock = yukino_xcb_lock;
	conn->unlock = yukino_xcb_unlock;

	conn->take = yukino_xcb_take;

	for (i = 0; i < ATOM_MAX_; i++) {
		xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn->conn_data.conn, atom_cookies[i], NULL);

		conn->conn_data.atoms[i] = (reply) ? reply->atom : XCB_ATOM_NONE;
	}

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
