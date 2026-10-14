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

#include "uuid.h"

/* version 4 UUID */
yukino_result_t yukino_uuidgen(struct yukino_uuid *uuid)
{
	yukino_result_t r;

	if ((r = yukino_random(uuid, sizeof(*uuid))) < 0)
		return r;

	/* set version halfbyte to 4 */
	uuid->c[6] &= 0x0F;
	uuid->c[6] |= 0x40;

	/* effectively limits high halfbyte to 8,9,a,b */
	uuid->c[10] &= 0xBF;

	return YUKINO_RESULT_OK;
}
