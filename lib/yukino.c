#include "yukino.h"
#include "yukino_c.h"

yukino_result_t yukino_connect(yukino_connection_t **pconn)
{
#ifdef YUKINO_XCB
	if (yukino_xcb_connect(pconn) == YUKINO_RESULT_OK)
		return YUKINO_RESULT_OK;
#endif

	return YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_display_resolution(yukino_connection_t *conn, uint32_t *pw, uint32_t *ph)
{
	return conn->display_resolution
		? conn->display_resolution(conn, pw, ph)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_disconnect(yukino_connection_t *conn)
{
	return conn->disconnect
		? conn->disconnect(conn)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_iter_start(yukino_connection_t *conn, const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	return conn->window_iter_start
		? conn->window_iter_start(conn, win, pwi)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_iter(yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *win)
{
	return conn->window_iter
		? conn->window_iter(conn, wi, win)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_window_iter_end(yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	return conn->window_iter_end
		? conn->window_iter_end(conn, wi)
		: YUKINO_RESULT_UNSUPPORTED;
}

/* helper function -- iterates over everything and puts it into a dynamically allocated list
 * the reason this does not call _start() is so that programs can still benefit from the asynchronous
 * calls that XCB provides */
yukino_result_t yukino_window_children(yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t **pwin, size_t *pwinsz)
{
	yukino_window_t *winlist;
	size_t winlistsz, winlistalloc;
	yukino_result_t r;
	yukino_window_t newwin;

	winlist = NULL;
	winlistalloc = winlistsz = 0;
	while ((r = yukino_window_iter(conn, wi, &newwin)) >= 0) {
		void *old = winlist;

		if (winlistsz >= winlistalloc) {
			winlistalloc = winlistalloc ? (winlistalloc * 2) : 8;
			winlist = realloc(winlist, winlistalloc * sizeof(*winlist));

			if (!winlist) {
				free(old);
				return YUKINO_RESULT_OUT_OF_MEMORY;
			}
		}

		winlist[winlistsz++] = newwin;

		/* finished */
		if (r == YUKINO_RESULT_DONE)
			break;
	}

	*pwin = winlist;
	*pwinsz = winlistsz;

	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_window_children_free(yukino_connection_t *conn, yukino_window_t *winlist, size_t winsz)
{
	free(winlist);
	return YUKINO_RESULT_OK;
	(void)winsz;
}

yukino_result_t yukino_window_position(yukino_connection_t *conn, yukino_window_t win,
	int32_t *x, int32_t *y, uint32_t *w, uint32_t *h)
{
	return conn->window_position
		? conn->window_position(conn, win, x, y, w, h)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_take(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_take_pixel pixel_func, void *userdata)
{
	return conn->take
		? conn->take(conn, x, y, w, h, pixel_func, userdata)
		: YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t take_cb(void *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_take_pixel pixel_func, void *userdata)
{
	return yukino_take(conn, x, y, w, h, pixel_func, userdata);
}

yukino_result_t yukino_take_png(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_write_cb write_cb, void *userdata)
{
	return yukino_write_png(x, y, w, h, write_cb, userdata, take_cb, conn);
}

yukino_result_t yukino_take_ppm(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_write_cb write_cb, void *userdata)
{
	return yukino_write_ppm(x, y, w, h, write_cb, userdata, take_cb, conn);
}

yukino_result_t yukino_lock(yukino_connection_t *conn)
{
	return conn->lock
		? conn->lock(conn)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_unlock(yukino_connection_t *conn)
{
	return conn->unlock
		? conn->unlock(conn)
		: YUKINO_RESULT_UNSUPPORTED;
}

yukino_result_t yukino_query_window_at_point(yukino_connection_t *conn,
	yukino_window_t *pwin, int32_t x, int32_t y)
{
	/* there may be no window under the pointer -- in that case return
	 * YUKINO_RESULT_NONE */
	yukino_window_iter_t *wi;
	yukino_result_t r = YUKINO_RESULT_NONE;
	yukino_result_t rr;
	yukino_window_t win;

	if ((rr = yukino_window_iter_start(conn, NULL, &wi)) < 0)
		return rr;

	while ((rr = yukino_window_iter(conn, wi, &win)) == YUKINO_RESULT_OK) {
		int32_t wx, wy;
		uint32_t ww, wh;

		if ((rr = yukino_window_position(conn, win, &wx, &wy, &ww, &wh)) < 0)
			continue; /* ??? */

		/* check if our coordinates are inside the window */
		if (!((x >= wx) && (x <= (wx + ww)) && (y >= wy) && (y <= (wy + wh))))
			continue; /* ignore */

		r = YUKINO_RESULT_OK;
		*pwin = win;
	}

	if ((rr = yukino_window_iter_end(conn, wi)) < 0)
		return rr;

	return r;
}
