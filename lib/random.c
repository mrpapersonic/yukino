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

#ifdef YUKINO_WIN32
# include <windows.h>
#else
# include <fcntl.h>
# include <unistd.h>
#endif

/* Ugh, this is nasty. Ideally we would have something that
 * initializes a random number generator, then we can just
 * read from that at any time. Oops. */
yukino_result_t yukino_random(void *x, size_t n)
{
#ifdef YUKINO_WIN32
	typedef BOOLEAN (*tpRtlGenRandom)(PVOID, ULONG);
	HMODULE mod;
	BOOLEAN res;
	tpRtlGenRandom pRtlGenRandom;

	/* XXX fallback to poopy rand() */
	mod = LoadLibraryA("ADVAPI32.DLL");
	if (!mod)
		return YUKINO_RESULT_UNSUPPORTED;

	pRtlGenRandom = (tpRtlGenRandom)GetProcAddress(mod, "SystemFunction036");
	if (!pRtlGenRandom) {
		FreeLibrary(mod);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	res = pRtlGenRandom(x, n);

	FreeLibrary(mod);

	if (!res)
		return YUKINO_RESULT_UNSUPPORTED;

	return YUKINO_RESULT_OK;
#else
	int fd;
	ssize_t nr;

	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return YUKINO_RESULT_UNSUPPORTED;

	nr = read(fd, x, n);
	close(fd);
	if (nr != n)
		return YUKINO_RESULT_UNSUPPORTED;

	return YUKINO_RESULT_OK;
#endif
}
