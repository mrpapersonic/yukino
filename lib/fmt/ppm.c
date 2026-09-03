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

struct cbuserdata {
	yukino_write_cb write_cb;
	void *userdata;
};

static yukino_result_t ppm_cb(void *userdata, unsigned char rgb[3])
{
	struct cbuserdata *ud = userdata;

	ud->write_cb(ud->userdata, rgb, 3);

	return YUKINO_RESULT_OK;
}

static size_t u32tostr(uint32_t x, char s[10])
{
	int pl, p;

	if (x < 10)
		pl = 1;
	else if (x < 100)
		pl = 2;
	else if (x < 1000)
		pl = 3;
	else if (x < 10000)
		pl = 4;
	else if (x < 100000)
		pl = 5;
	else if (x < 1000000)
		pl = 6;
	else if (x < 10000000)
		pl = 7;
	else if (x < 100000000)
		pl = 8;
	else if (x < 1000000000)
		pl = 9;
	else
		pl = 10;

	p = pl;
	while (p--) {
		s[p] = (x % 10) + '0';
		x /= 10;
	}

	return pl;
}

static yukino_result_t write_u32str(
	yukino_write_cb write_cb, void *userdata, uint32_t x)
{
	char xx[10];

	return write_cb(userdata, xx, u32tostr(x, xx));
}

yukino_result_t yukino_write_ppm(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
	yukino_write_cb write_cb, void *userdata, yukino_take_t take_cb,
	void *take_data)
{
	struct cbuserdata ud = {write_cb, userdata};
	yukino_result_t r;

	if ((r = write_cb(userdata, "P6\n", 3)) < 0)
		return r;

	if ((r = write_u32str(write_cb, userdata, w)) < 0)
		return r;

	if ((r = write_cb(userdata, " ", 1)) < 0)
		return r;

	if ((r = write_u32str(write_cb, userdata, h)) < 0)
		return r;

	if ((r = write_cb(userdata, "\n255\n", 5)) < 0)
		return r;

	return take_cb(take_data, x, y, w, h, ppm_cb, &ud);
}
