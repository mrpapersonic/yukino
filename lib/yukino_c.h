#ifndef YUKINO_C_H_
#define YUKINO_C_H_

#include "yukino.h"

/* privates */
struct yukino_connection {
	yukino_result_t (*disconnect)(yukino_connection_t *conn);
	yukino_result_t (*display_resolution)(yukino_connection_t *conn, uint32_t *w, uint32_t *h);

	yukino_result_t (*window_iter_start)(
			yukino_connection_t *conn,
			const yukino_window_t *win,
			yukino_window_iter_t **pwi);
	yukino_result_t (*window_iter)(
			yukino_connection_t *conn, yukino_window_iter_t *wi,
			yukino_window_t *pw);
	yukino_result_t (*window_iter_end)(
			yukino_connection_t *conn, yukino_window_iter_t *wi);

	yukino_result_t (*window_position)(yukino_connection_t *conn,
			yukino_window_t win,
			int32_t *x, int32_t *y,
			uint32_t *w, uint32_t *h);

	yukino_result_t (*take)(yukino_connection_t *conn,
			uint32_t x, uint32_t y, uint32_t w, uint32_t h,
			yukino_take_pixel pixel_func, void *userdata);

	yukino_result_t (*lock)(yukino_connection_t *conn);
	yukino_result_t (*unlock)(yukino_connection_t *conn);

#ifdef YUKINO_CONNECTION_DATA
	/* this is here for individual backends to define before including
	 * yukino_c.h */
	struct yukino_connection_data conn_data;
#endif
};

/* CRC32 with the PNG polynomial */
uint32_t yukino_crc32(uint32_t crc, const unsigned char *msg, size_t sz);

/* Adler-32. This requires a special getter function that crc32
 * does not, due to the way it operates. */
struct yukino_adler32 {
	uint32_t a, b;

	uint32_t n;
};

void yukino_adler32_init(struct yukino_adler32 *a32);
void yukino_adler32(struct yukino_adler32 *a32, const unsigned char *msg, size_t sz);
uint32_t yukino_adler32_get(struct yukino_adler32 *a32);

#ifdef YUKINO_XCB
/* initializes a connection */
yukino_result_t yukino_xcb_connect(yukino_connection_t **pconn);
#endif

#endif /* YUKINO_C_H_ */
