#ifndef YUKINO_H_
#define YUKINO_H_

/* don't care about c89 */
#include <stdint.h>
#include <stdlib.h>

/* Every window system ever uses something that can fit in 64-bits,
 * or a pointer, but 64-bit everywhere is probably safer for the
 * foreseeable future. */
typedef uint64_t yukino_window_t;

/* Window iterator object -- opaque */
struct yukino_window_iter;
typedef struct yukino_window_iter yukino_window_iter_t;

/* Connection to the display */
struct yukino_connection;
typedef struct yukino_connection yukino_connection_t;

/* negative value == error */
typedef int32_t yukino_result_t;

/* TODO switch everything over to this */
typedef struct yukino_rect {
	int32_t x, y, w, h;
} yukino_rect_t;

enum {
	YUKINO_RESULT_UNSUPPORTED = INT32_MIN,
	YUKINO_RESULT_OUT_OF_MEMORY,
	YUKINO_RESULT_INVALID_PARAM,
	YUKINO_RESULT_FILE_ERROR,

	YUKINO_RESULT_OK = 0,
	/* For iterators -- should also set stuff to NULL? */
	YUKINO_RESULT_DONE,
	/* For querying window at point */
	YUKINO_RESULT_NONE,
};

/* ------------------------------------------------------------------------ */
/* connect to the display */

yukino_result_t yukino_connect(yukino_connection_t **conn);
yukino_result_t yukino_disconnect(yukino_connection_t *conn);

/* ------------------------------------------------------------------------ */
/* act on the display ... not much else is useful here */

yukino_result_t yukino_display_resolution(yukino_connection_t *conn,
		uint32_t *w, uint32_t *h);

/* ------------------------------------------------------------------------ */
/* iterate through windows */

/* On X11, this loops from the backmost window to the frontmost window. */
yukino_result_t yukino_window_iter_start(
		yukino_connection_t *conn,
		const yukino_window_t *win, /* NULL == operate on the root window */
		yukino_window_iter_t **pwi);
yukino_result_t yukino_window_iter(
		yukino_connection_t *conn, yukino_window_iter_t *wi,
		yukino_window_t *pw);
yukino_result_t yukino_window_iter_end(
		yukino_connection_t *conn, yukino_window_iter_t *wi);

/* ------------------------------------------------------------------------ */
/* window utils */

/* XXX have a yukino_rect instead? */
yukino_result_t yukino_window_position(yukino_connection_t *conn,
		yukino_window_t win,
		int32_t *x, int32_t *y,
		uint32_t *w, uint32_t *h);


/* ------------------------------------------------------------------------ */
/* display lock/unlock. this could come in handy if say, we wanted to take
 * a full display yukino, take note of the position of all of the
 * windows, and THEN let the user crop it
 * 
 * BUT if we crash we'd end up leaving x11 in a buggy state ... */

yukino_result_t yukino_lock(yukino_connection_t *conn);
yukino_result_t yukino_unlock(yukino_connection_t *conn);

/* ------------------------------------------------------------------------ */
/* take a yukino */

/* writes a pixel value */
typedef yukino_result_t (*yukino_take_pixel)(void *userdata,
	unsigned char rgb[3]);

yukino_result_t yukino_take(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_take_pixel pixel_func, void *userdata);

typedef yukino_result_t (*yukino_write_cb)(void *userdata,
	const void *bytes, size_t size);

/* for supplying custom yukino_take functions
 * this effectively makes us a very shitty image writing library
 * with absolutely no compression! */
typedef yukino_result_t (*yukino_take_t)(void *conn,
	uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_take_pixel pixel_func, void *userdata);

yukino_result_t yukino_write_ppm(
	uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *write_data,
	yukino_take_t take_cb, void *take_data);

yukino_result_t yukino_write_png(
	uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *write_data,
	yukino_take_t take_cb, void *take_data);

/* .ppm output */
yukino_result_t yukino_take_ppm(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_write_cb write_cb, void *userdata);

/* .png output */
yukino_result_t yukino_take_png(yukino_connection_t *conn,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_write_cb write_cb, void *userdata);

/* wrapper over yukino_window_iter */
yukino_result_t yukino_window_children(yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t **pwin, size_t *pwinsz);
yukino_result_t yukino_window_children_free(yukino_connection_t *conn, yukino_window_t *winlist, size_t winsz);

/* skeeee */
yukino_result_t yukino_query_window_at_point(yukino_connection_t *conn,
	yukino_window_t *pwin, int32_t x, int32_t y);

#endif
