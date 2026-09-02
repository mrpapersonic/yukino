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

#include "yukino_c.h"

#define A32N_MOD 65521U
#define A32N_MAX 5552 /* magic */

static void yukino_adler32_mod(struct yukino_adler32 *a32)
{
	a32->a %= A32N_MOD;
	a32->b %= A32N_MOD;
	a32->n = A32N_MAX;
}

uint32_t yukino_adler32_get(struct yukino_adler32 *a32)
{
	yukino_adler32_mod(a32);
	return (a32->b << 16) | a32->a;
}

void yukino_adler32_init(struct yukino_adler32 *a32)
{
	a32->a = 1;
	a32->b = 0;
	a32->n = A32N_MAX;
}

void yukino_adler32(struct yukino_adler32 *a32, const unsigned char *msg, size_t sz)
{
	while (sz > 0) {
		/* DO IT */
		size_t tsz;

		/* Have to perform a modulo? */
		if (!a32->n)
			yukino_adler32_mod(a32);

		tsz = (sz > a32->n) ? a32->n : sz;

		sz -= tsz;
		a32->n -= tsz;

		while (tsz >= 16) {
			a32->a += msg[0]; a32->b += a32->a;
			a32->a += msg[1]; a32->b += a32->a;
			a32->a += msg[2]; a32->b += a32->a;
			a32->a += msg[3]; a32->b += a32->a;
			a32->a += msg[4]; a32->b += a32->a;
			a32->a += msg[5]; a32->b += a32->a;
			a32->a += msg[6]; a32->b += a32->a;
			a32->a += msg[7]; a32->b += a32->a;
			a32->a += msg[8]; a32->b += a32->a;
			a32->a += msg[9]; a32->b += a32->a;
			a32->a += msg[10]; a32->b += a32->a;
			a32->a += msg[11]; a32->b += a32->a;
			a32->a += msg[12]; a32->b += a32->a;
			a32->a += msg[13]; a32->b += a32->a;
			a32->a += msg[14]; a32->b += a32->a;
			a32->a += msg[15]; a32->b += a32->a;
			msg += 16;
			tsz -= 16;
		}

		while (tsz >= 1) {
			a32->a += msg[0];
			a32->b += a32->a;
			msg++;
			tsz--;
		}
	}
}