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

#include <stdio.h>
#include <time.h>

/* This crap was all lifted from my librandom library  --paper */

/* This doesn't depend on anything, I guess it's okay to compile it everywhere */
#define RANDOM_UNIX 1

#ifdef YUKINO_WIN32
# define RANDOM_WIN32 1
#endif

/* TODO cmake check for arc4random */
#if defined(__MACH__) && defined(__APPLE__)
# define RANDOM_ARC4RANDOM
#endif

#ifdef RANDOM_WIN32
# include <windows.h>
# include <wincrypt.h>
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*(x)))
#define MIN(x,y) ((x)<(y)?(x):(y))

/* NOTE: I am not sure if other platforms provide real APIs for good
 * randomness. Macintosh provides SetRandomSeed and RandomBits, but I
 * am skeptical of its use as a good random number generator. */

#ifdef RANDOM_UNIX

struct random_unix {
	FILE *f;
};

static void random_unix_close(struct yukino_random *r)
{
	struct random_unix *ru = r->userdata;

	if (ru->f)
		fclose(ru->f);

	free(ru);
}

static size_t random_unix_read(struct yukino_random *r, void *ptr, size_t s)
{
	struct random_unix *ru = r->userdata;

	return fread(ptr, s, 1, ru->f);
}

static int random_unix_init(struct yukino_random *r)
{
	/* hmmm. */
	static const char *names[] = {
		"/dev/random",
		"/dev/urandom",
	};
	struct random_unix *ru = malloc(sizeof(*ru));
	size_t i;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		ru->f = fopen(names[i], "rb");
		if (ru->f)
			goto gotdevice;
	}

	free(ru);
	return -1;

gotdevice:

	r->userdata = ru;
	r->close = random_unix_close;
	r->read = random_unix_read;

	return 0;
}

#endif

/* ------------------------------------------------------------------------ */

#ifdef RANDOM_ARC4RANDOM

static size_t random_arc4random_read(struct yukino_random *r, void *ptr, size_t s)
{
	size_t l = s;

	while (l > 0) {
		size_t amt;
		uint32_t x;

		amt = MIN(sizeof(x), l);
		x = arc4random();

		memcpy(ptr, &x, amt);

		ptr += amt;
		l -= amt;
	}

	return s;
}

static void random_arc4random_close(struct yukino_random *r)
{
	/* nothing */
}

static int random_arc4random_init(struct yukino_random *r)
{
	/* always successful */
	r->read = random_arc4random_read;
	r->close = random_arc4random_close;
	return 0;
}

#endif

/* ------------------------------------------------------------------------ */

#ifdef RANDOM_WIN32

typedef BOOL (*pADVAPI32_CryptReleaseContext)(HCRYPTPROV hProv, DWORD dwFlags);
typedef BOOL (*pADVAPI32_CryptGenRandom)(HCRYPTPROV hProv, DWORD dwLen, BYTE *pbBuffer);
typedef BOOL (*pADVAPI32_CryptAcquireContextA)(HCRYPTPROV *phProv, LPCSTR szContainer, LPCSTR szProvider, DWORD dwProvType,DWORD dwFlags);

struct random_win32 {
	HCRYPTPROV c;

	HMODULE advapi32;
	pADVAPI32_CryptReleaseContext ADVAPI32_CryptReleaseContext;
	pADVAPI32_CryptGenRandom ADVAPI32_CryptGenRandom;
	pADVAPI32_CryptAcquireContextA ADVAPI32_CryptAcquireContextA;
};

static void random_win32_close(struct yukino_random *r)
{
	struct random_win32 *rw = r->userdata;

	rw->ADVAPI32_CryptReleaseContext(rw->c, 0);
	FreeLibrary(rw->advapi32);

	free(rw);
}

static size_t random_win32_read(struct yukino_random *r, void *ptr, size_t s)
{
	struct random_win32 *rw = r->userdata;

	if (!rw->ADVAPI32_CryptGenRandom(rw->c, s, ptr))
		return 0;

	return s;
}

static int random_win32_init(struct yukino_random *r)
{
	HCRYPTPROV c;
	struct random_win32 *rw;

	rw = malloc(sizeof(*rw));
	if (!rw)
		return -2;

	rw->advapi32 = LoadLibraryA("ADVAPI32.DLL");
	if (!rw->advapi32)
		return -3;

#define LOADFUNC(x) \
	rw->ADVAPI32_##x = (pADVAPI32_##x)GetProcAddress(rw->advapi32, #x); \
	if (!rw->ADVAPI32_##x) \
		return -4

	LOADFUNC(CryptAcquireContextA);
	LOADFUNC(CryptGenRandom);
	LOADFUNC(CryptReleaseContext);

	if (!rw->ADVAPI32_CryptAcquireContextA(&c, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return -1;

	rw->c = c;

	r->userdata = rw;
	r->close = random_win32_close;
	r->read = random_win32_read;
	return 0;
}

#endif

/* ------------------------------------------------------------------------ */
/* Crappy fallback RNG */

/* Generate an okay-ish XORshift seed */
static uint32_t gen_xorshift_seed(void)
{
	/* Ehhh */
	return time(NULL);
}

static uint32_t xorshift(uint32_t x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static void random_xorshift_close(struct yukino_random *r)
{
	/* nop */
}

static size_t random_xorshift_read(struct yukino_random *r, void *ptr, size_t s)
{
	uint32_t x = (uintptr_t)r->userdata;

	while (s > 0) {
		size_t n;

		n = MIN(s, 4);

		memcpy(ptr, &x, n);
		ptr = (char *)ptr + n;
		s -= n;

		x = xorshift(x);
	}

	r->userdata = (void *)(uintptr_t)x;
}

static int random_xorshift_init(struct yukino_random *r)
{
	/* Seed */
	r->userdata = (void *)(uintptr_t)gen_xorshift_seed();
	r->close = random_xorshift_close;
	r->read = random_xorshift_read;

	return 0;
}

/* ------------------------------------------------------------------------ */
/* platform-independent routines */

yukino_result_t yukino_random_init(struct yukino_random *r)
{
	int i;

	int (*funcs[])(struct yukino_random *r) = {
#ifdef RANDOM_WIN32
		random_win32_init,
#endif
#ifdef RANDOM_ARC4RANDOM
		random_arc4random_init,
#endif
#ifdef RANDOM_UNIX
		random_unix_init,
#endif
		/* Works everywhere but is subpar at best */
		random_xorshift_init,
	};

	for (i = 0; funcs[i]; i++)
		if (!funcs[i](r))
			return 0;

	return -1;
}

void yukino_random_close(struct yukino_random *r)
{
	r->close(r);
}

size_t yukino_random_read(struct yukino_random *r, void *ptr, size_t s)
{
	return r->read(r, ptr, s);
}

/* ------------------------------------------------------------------------ */
/* Reimplementing this function as a wrapper ... */

int yukino_random(void *x, size_t sz)
{
	yukino_result_t r;
	struct yukino_random ra;
	size_t s;

	if ((r = yukino_random_init(&ra)) < 0)
		return r;

	s = yukino_random_read(&ra, x, sz);
	yukino_random_close(&ra);

	if (s != sz)
		return -1;

	return 0;
}
