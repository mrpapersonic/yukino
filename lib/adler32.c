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

/*
   To calculate A32N_MAX for a given integer, we have to find the maximum
   number of additions we have that can fit inside the integer. That number
   can be found as such:

	   f(n) = 255n((n+1)/2)+(n+1)(BASE-1)

   Finding out where it overflows is essentially a case of inversing this
   function and plugging in ((2^(BITS))-1). The results of this operation
   truncated to an integer are shown below, for 64-bit and 32-bit respectively.
*/

#ifdef YUKINO_64BIT_ADLER32
# define A32N_MAX UINT32_C(380368439)
# define A32N_MOD UINT64_C(65521)
#else
# define A32N_MAX UINT32_C(5552)
# define A32N_MOD UINT32_C(65521)
#endif

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

void yukino_adler32(
	struct yukino_adler32 *a32, const unsigned char *msg, size_t sz)
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
			a32->a += msg[0];
			a32->b += a32->a;
			a32->a += msg[1];
			a32->b += a32->a;
			a32->a += msg[2];
			a32->b += a32->a;
			a32->a += msg[3];
			a32->b += a32->a;
			a32->a += msg[4];
			a32->b += a32->a;
			a32->a += msg[5];
			a32->b += a32->a;
			a32->a += msg[6];
			a32->b += a32->a;
			a32->a += msg[7];
			a32->b += a32->a;
			a32->a += msg[8];
			a32->b += a32->a;
			a32->a += msg[9];
			a32->b += a32->a;
			a32->a += msg[10];
			a32->b += a32->a;
			a32->a += msg[11];
			a32->b += a32->a;
			a32->a += msg[12];
			a32->b += a32->a;
			a32->a += msg[13];
			a32->b += a32->a;
			a32->a += msg[14];
			a32->b += a32->a;
			a32->a += msg[15];
			a32->b += a32->a;
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
