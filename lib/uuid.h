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

#ifndef YUKINO_UUID_H_
#define YUKINO_UUID_H_

#include "yukino.h"

struct yukino_uuid {
	unsigned char c[16];
};

/* creates a UUID */
yukino_result_t yukino_uuidgen(struct yukino_uuid *uuid);
/* creates a printf string from a uuid */
#define YUKINO_UUID_PRINTF_FORMAT_DELIM(delim) \
	"%02x%02x%02x%02x" delim "%02x%02x" delim "%02x%02x" delim \
	"%02x%02x" delim "%02x%02x%02x%02x%02x%02x"
/* Default to a dash, like typical UUIDs */
#define YUKINO_UUID_PRINTF_FORMAT YUKINO_UUID_PRINTF_FORMAT_DELIM("-")
#define YUKINO_UUID_PRINTF_PARAM(uuid) \
	(uuid)->c[0], (uuid)->c[1], (uuid)->c[2], (uuid)->c[3], (uuid)->c[4], \
		(uuid)->c[5], (uuid)->c[6], (uuid)->c[7], (uuid)->c[8], (uuid)->c[9], \
		(uuid)->c[10], (uuid)->c[11], (uuid)->c[12], (uuid)->c[13], \
		(uuid)->c[14], (uuid)->c[15]
#define YUKINO_UUID_SCANF_FORMAT \
	"%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"
#define YUKINO_UUID_SCANF_PARAM(uuid) \
	&(uuid)->c[0], &(uuid)->c[1], &(uuid)->c[2], &(uuid)->c[3], &(uuid)->c[4], \
		&(uuid)->c[5], &(uuid)->c[6], &(uuid)->c[7], &(uuid)->c[8], \
		&(uuid)->c[9], &(uuid)->c[10], &(uuid)->c[11], &(uuid)->c[12], \
		&(uuid)->c[13], &(uuid)->c[14], &(uuid)->c[15]

#endif /* YUKINO_UUID_H_ */
