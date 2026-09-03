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

struct bmp {
	yukino_write_cb write_cb;
	void *userdata;

	uint32_t x, w;
};

static uint16_t bmp_bswap16(uint16_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return px[0] | ((uint16_t)px[1] << 8);
}

static uint32_t bmp_bswap32(uint32_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16) | ((uint32_t)px[3] << 24);
}

static yukino_result_t bmp_write(struct bmp *bmp, const void *x, size_t sz)
{
	return bmp->write_cb(bmp->userdata, x, sz);
}

static yukino_result_t bmp_write_u16(struct bmp *bmp, uint16_t x)
{
	x = bmp_bswap16(x);
	return bmp_write(bmp, &x, sizeof(x));
}

static yukino_result_t bmp_write_u32(struct bmp *bmp, uint32_t x)
{
	x = bmp_bswap32(x);
	return bmp_write(bmp, &x, sizeof(x));
}

static yukino_result_t bmp_write_i32(struct bmp *bmp, int32_t x)
{
	return bmp_write_u32(bmp, *(uint32_t *)&x);
}

static yukino_result_t bmp_cb(void *userdata, unsigned char rgb[3])
{
	struct bmp *bmp = userdata;
	yukino_result_t r;
	unsigned char tmp;

	/* swap it around */
	tmp = rgb[0];
	rgb[0] = rgb[2];
	rgb[2] = tmp;

	/* 24-bit */
	if ((r = bmp_write(bmp, rgb, 3)) < 0)
		return r;

	if (++bmp->x == bmp->w) {
		/* pad */
		bmp_write(bmp, "\0\0\0", bmp->w % 4);
		bmp->x = 0;
	}


	return YUKINO_RESULT_OK;
}

yukino_result_t yukino_write_bmp(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *userdata, yukino_take_t take_cb,
	void *take_data)
{
	struct bmp bmp;
	yukino_result_t r;

	bmp.write_cb = write_cb;
	bmp.userdata = userdata;
	bmp.x = 0;
	bmp.w = w;

	/* write out the header :) */
	if ((r = bmp_write(&bmp, "BM", 2)) < 0)
		return r;

	if ((r = bmp_write_u32(&bmp, 14 + 40 + (w * h * 3))))
		return r;

	if ((r = bmp_write(&bmp, "\0\0\0\0\x36\0\0\0\x28\0\0", 12)) < 0)
		return r;

	if ((r = bmp_write_u32(&bmp, w)) < 0)
		return r;

	if ((r = bmp_write_i32(&bmp, -(int32_t)h)) < 0)
		return r;

	/* 24bpp */
	if ((r = bmp_write(&bmp, "\1\0\x18\0\0\0\0\0\0\0\0", 12)) < 0)
		return r;

	if ((r = bmp_write_u32(&bmp, w)) < 0)
		return r;

	if ((r = bmp_write_i32(&bmp, -(int32_t)h)) < 0)
		return r;

	if ((r = bmp_write(&bmp, "\0\0\0\0\0\0\0", 8)) < 0)
		return r;

	/* done with the hdr */

	return take_cb(take_data, x, y, w, h, bmp_cb, &bmp);
}
