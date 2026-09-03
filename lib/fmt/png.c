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
#include "yukino_c.h"

#ifdef YUKINO_ZLIB
# include <zlib.h>
#endif

struct png {
	/* current chunk crc */
	uint32_t crc;

#ifdef YUKINO_ZLIB
	/* ;) */
	z_stream strm;
#else
	/* calculated size of image data (w * h * 3) */
	uint32_t sz;

	uint16_t j;
#endif

	/* need this because png is stupid */
	uint32_t x, w;

	/* Adler-32 */
	struct yukino_adler32 a32;

	yukino_write_cb write_cb;
	void *userdata;
};

/* byteswap to/from big endian */
static uint16_t png_bswap16be(uint16_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return (px[1]) | ((uint16_t)px[0] << 8);
}

static uint32_t png_bswap32be(uint32_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return (px[3]) | ((uint32_t)px[2] << 8) | ((uint32_t)px[1] << 16)
	       | ((uint32_t)px[0] << 24);
}

static uint16_t png_bswap16le(uint16_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return px[0] | ((uint16_t)px[1] << 8);
}

/* big write function */
static yukino_result_t png_write(struct png *png, const void *bytes, size_t sz)
{
	yukino_result_t r;

	if ((r = png->write_cb(png->userdata, bytes, sz)) < 0)
		return r;

	/* Do the CRC */
	png->crc = yukino_crc32(png->crc, bytes, sz);

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_write_u16be(struct png *png, uint16_t u)
{
	yukino_result_t r;

	u = png_bswap16be(u);

	if ((r = png_write(png, &u, sizeof(u))) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_write_u16le(struct png *png, uint16_t u)
{
	yukino_result_t r;

	u = png_bswap16le(u);

	if ((r = png_write(png, &u, sizeof(u))) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_write_u32be(struct png *png, uint32_t u)
{
	yukino_result_t r;

	u = png_bswap32be(u);

	if ((r = png_write(png, &u, sizeof(u))) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

#ifndef YUKINO_ZLIB
static yukino_result_t png_deflate_header_impl(
	struct png *png, unsigned char b, uint16_t sz)
{
	yukino_result_t r;

	if ((r = png_write(png, &b, 1)) < 0)
		return r;

	if ((r = png_write_u16le(png, sz)) < 0)
		return r;

	if ((r = png_write_u16le(png, ~sz)) < 0)
		return r;

	png->j = sz;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_deflate_header(struct png *png, size_t sz)
{
	return (sz > 65535) ? png_deflate_header_impl(png, 0, 65535)
			    : png_deflate_header_impl(png, 1, sz);
}
#endif

static yukino_result_t png_deflate_write(
	struct png *png, const void *b_, size_t sz)
{
#ifdef YUKINO_ZLIB
	png->strm.next_in = b_;
	png->strm.avail_in = sz;

	if (deflate(&png->strm, Z_NO_FLUSH) < 0)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	return YUKINO_RESULT_OK;
#else
	yukino_result_t r;
	const unsigned char *b = b_;

	/* accumulate adler32 .. only for uncompressed data */

	yukino_adler32(&png->a32, b, sz);

	while (sz > 0) {
		size_t tocpy;

		/* Write deflate header if necessary */
		if (!png->j && ((r = png_deflate_header(png, png->sz)) < 0))
			return r;

		tocpy = sz;
		if (tocpy > png->j)
			tocpy = png->j;

		if ((r = png_write(png, b, tocpy)) < 0)
			return r;

		png->sz -= tocpy;
		png->j -= tocpy;
		sz -= tocpy;
		b += tocpy;
	}

	return YUKINO_RESULT_OK;
#endif
}

static yukino_result_t png_zlib_header(struct png *png, size_t sz)
{
#ifdef YUKINO_ZLIB
	uLong bound;

	png->strm.zalloc = NULL;
	png->strm.zfree  = NULL;
	png->strm.opaque = NULL;

	if (deflateInit(&png->strm, Z_DEFAULT_COMPRESSION) != Z_OK)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	bound = deflateBound(&png->strm, sz);

	png->strm.next_out = malloc(bound);
	if (!png->strm.next_out) {
		deflateEnd(&png->strm);
		return YUKINO_RESULT_OUT_OF_MEMORY;
	}

	png->strm.avail_out = bound;

	return YUKINO_RESULT_OK;
#else
	/* init adler-32 */
	yukino_adler32_init(&png->a32);

	png->j = 0;
	png->sz = sz;

	/* write the zlib header */
	return png_write(png, "\x78\x01", 2);
#endif
}

static yukino_result_t png_zlib_footer(struct png *png)
{
#ifdef YUKINO_ZLIB
	if (deflate(&png->strm, Z_FINISH) != Z_STREAM_END)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	deflateEnd(&png->strm);
	return YUKINO_RESULT_OK;
#else
	return png_write_u32be(png, yukino_adler32_get(&png->a32));
#endif
}

static yukino_result_t png_chunk_head(
	struct png *png, const char id[4], uint32_t sz)
{
	yukino_result_t r;

	if ((r = png_write_u32be(png, sz)) < 0)
		return r;

	/* Reset the CRC */
	png->crc = 0xFFFFFFFF;
	if ((r = png_write(png, id, 4)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_chunk_tail(struct png *png)
{
	yukino_result_t r;

	if ((r = png_write_u32be(png, ~png->crc)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_cb(void *userdata, unsigned char rgb[3])
{
	struct png *ud = userdata;
	yukino_result_t r;

	if (!ud->x && ((r = png_deflate_write(ud, "", 1)) < 0))
		return r;
	ud->x = (ud->x + 1) % ud->w;

	/* Split it */
	if ((r = png_deflate_write(ud, rgb, 3)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

/* based on libpng docs */
yukino_result_t yukino_write_png(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *userdata, yukino_take_t take_cb,
	void *take_data)
{
	static const unsigned char magic[] = {137, 80, 78, 71, 13, 10, 26, 10};
	yukino_result_t r;
	struct png png;

	if ((r = write_cb(userdata, magic, sizeof(magic))) < 0)
		return r;

	png.crc = 0xFFFFFFFF;
	png.write_cb = write_cb;
	png.userdata = userdata;
	png.x = 0;
	png.w = w;

	/* IHDR chunk */
	if ((r = png_chunk_head(&png, "IHDR", 13)) < 0)
		return r;

	png_write_u32be(&png, w);
	png_write_u32be(&png, h);
	png_write(&png, "\x08\x02\x00\x00\x00", 5);

	png_chunk_tail(&png);

	/* Now for the big business -- we have to "fake"
	 * a zlib and just store everything plain. but
	 * we also need the data size *before*, so we gotta
	 * do some calculamalations */
	{
		uint32_t imgsz = (w * h * 3) + h;

#ifdef YUKINO_ZLIB
		/* write the zlib header first */
		png_zlib_header(&png, imgsz);

		/* ... FINALLY */
		take_cb(take_data, x, y, w, h, png_cb, &png);

		png_zlib_footer(&png);

		png_chunk_head(&png, "IDAT", png.strm.total_out);

		png_write(&png, png.strm.next_out - png.strm.total_out, png.strm.total_out);

		png_chunk_tail(&png);
#else
		uint32_t sz;

		/* zlib hdr, uncompressed data, deflate headers, zlib adler32 */
		sz = 2 + imgsz + (5 * ((imgsz + 65534) / 65535)) + 4;

		png_chunk_head(&png, "IDAT", sz);

		/* write the zlib header first */
		png_zlib_header(&png, imgsz);

		/* ... FINALLY */
		take_cb(take_data, x, y, w, h, png_cb, &png);

		png_zlib_footer(&png);

		png_chunk_tail(&png);
#endif
	}

	/* GET ME A CHEESE WITH NOTTIN */
	png_chunk_head(&png, "IEND", 0);

	png_chunk_tail(&png);

	return YUKINO_RESULT_OK;
}
