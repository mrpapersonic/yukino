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

#include <limits.h>
#include <stdlib.h>
#include <windows.h>
#include <stdio.h>

typedef HRESULT (*tpDwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);

/* Define data specific to this connection */
struct yukino_connection_data {
	HMODULE dwmapi;
	tpDwmGetWindowAttribute pDwmGetWindowAttribute;
};
#define YUKINO_CONNECTION_DATA 1
#include "../yukino_c.h"

static yukino_result_t yukino_win32_disconnect(yukino_connection_t *conn)
{
	if (conn->conn_data.dwmapi)
		FreeLibrary(conn->conn_data.dwmapi);
	free(conn);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_win32_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	int cw, ch;

	cw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	ch = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	if (cw && ch)
		goto gotit;

	cw = GetSystemMetrics(SM_CXSCREEN);
	ch = GetSystemMetrics(SM_CYSCREEN);
	if (cw && ch)
		goto gotit;

	return YUKINO_RESULT_UNSUPPORTED;

gotit:
	*w = cw;
	*h = ch;

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

#define YUKINO_REALLOC_SIZE (32)
#define YUKINO_REALLOC_ADJUST(x) ((x) ? (x) + YUKINO_REALLOC_SIZE : (YUKINO_REALLOC_SIZE))

struct yukino_window_iter {
	HWND cur;
};

static HWND get_window_visible(HWND hwnd, UINT ucmd)
{
	for (;;) {
		hwnd = GetWindow(hwnd, ucmd);
		if (!hwnd || IsWindowVisible(hwnd))
			return hwnd;
	}

	return hwnd;
}

static yukino_result_t yukino_win32_window_iter_start(yukino_connection_t *conn,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	yukino_window_iter_t *wi;
	HWND wnd, wndlast;

	wnd = GetTopWindow(NULL);
	if (!wnd)
		return YUKINO_RESULT_UNSUPPORTED;

	wndlast = get_window_visible(wnd, GW_HWNDLAST);
	if (!wndlast)
		return YUKINO_RESULT_UNSUPPORTED;

	wi = calloc(1, sizeof(*wi));
	if (!wi)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	wi->cur = wndlast;

	*pwi = wi;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_win32_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw)
{
	if (!wi || !pw)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!wi->cur)
		return YUKINO_RESULT_DONE;

	*pw = (uintptr_t)wi->cur;
	wi->cur = get_window_visible(wi->cur, GW_HWNDPREV);

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_win32_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	free(wi);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* XXX: Need a "batch" API of sorts */

static void RECT_to_yukino_rect(const RECT *r, yukino_rect_t *pr)
{
	pr->x = r->left;
	pr->y = r->top;
	pr->w = r->right - r->left;
	pr->h = r->bottom - r->top;
}

static yukino_result_t yukino_win32_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	RECT r;
	HWND hwnd;
	POINT pttl, ptbr;

	hwnd = (HWND)(uintptr_t)win;

	if (!GetClientRect(hwnd, &r))
		return YUKINO_RESULT_UNSUPPORTED;
	pttl.x = r.left;
	pttl.y = r.top;
	ptbr.x = r.right;
	ptbr.y = r.bottom;
	ClientToScreen(hwnd, &pttl);
	ClientToScreen(hwnd, &ptbr);

	pr->x = pttl.x;
	pr->y = pttl.y;
	pr->w = ptbr.x - pttl.x;
	pr->h = ptbr.y - pttl.y;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_win32_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	RECT r;
	HWND hwnd;

	hwnd = (HWND)(uintptr_t)win;

	if (conn->conn_data.pDwmGetWindowAttribute) {
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
		/* This function gets the real window bounds */
		if (conn->conn_data.pDwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)) == S_OK)
			goto gotit;
	}

	/* Hopefully this is pre-Vista, and we don't have DPI shit to worry about */
	if (GetWindowRect(hwnd, &r))
		goto gotit;

	return YUKINO_RESULT_INVALID_PARAM;

gotit:
	RECT_to_yukino_rect(&r, pr);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

static yukino_result_t yukino_win32_lock(yukino_connection_t *conn)
{
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_win32_unlock(yukino_connection_t *conn)
{
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

static uint32_t read_pixel(
	const uint8_t *p, uint32_t x, uint8_t bpp, unsigned int big_endian)
{
	uint32_t pxl;

	/* shift it to the right place */
	p += (x * bpp) >> 3;

	if (bpp == 4) {
		pxl = ((x & 1) != big_endian) ? (*p & 0xf) : (*p >> 4);
	} else if (big_endian) {
		pxl = 0;

		switch (bpp) {
		case 32: pxl |= *p++; pxl <<= 8;
		case 24: pxl |= *p++; pxl <<= 8;
		case 16: pxl |= *p++; pxl <<= 8;
		case 8: pxl |= *p; break;
		}
	} else {
		pxl = 0;

		switch (bpp) {
		case 32: pxl |= p[3]; pxl <<= 8;
		case 24: pxl |= p[2]; pxl <<= 8;
		case 16: pxl |= p[1]; pxl <<= 8;
		case 8: pxl |= p[0]; break;
		}
	}

	return pxl;
}

struct yukino_screenshot {
	/* This is the result bitmap from win32_screenshot */
	HBITMAP bmp;

	/* Memory buffer */
	struct yukino_image mbuf;
};

static yukino_result_t yukino_win32_screenshot(yukino_connection_t *conn,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	HDC scr, hdcmem;
	yukino_result_t r;
	yukino_screenshot_t *s;
	BITMAPINFO bi;
	void *bits;
	HGDIOBJ old_object;

	if ((r = yukino_screenshot_fix_resolution(conn, &w, &h)) < 0)
		return r;

	s = calloc(1, sizeof(*s));
	if (!s)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	scr = GetDC(NULL);
	if (!scr) {
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	hdcmem = CreateCompatibleDC(scr);
	if (!hdcmem) {
		ReleaseDC(NULL, scr);
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	/* We specifically want Windows to handle conversion to RGB
	 * for us, as on older systems you have palettes etc. which
	 * are quite annoying to deal with ourselves */
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -(int32_t)h; /* fucking Windows */
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bi.bmiHeader.biCompression = BI_RGB;

	s->bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!s->bmp) {
		DeleteDC(hdcmem);
		ReleaseDC(NULL, scr);
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	old_object = SelectObject(hdcmem, s->bmp);
	BitBlt(hdcmem, 0, 0, w, h, scr, x, y, SRCCOPY);
	SelectObject(hdcmem, old_object);

	DeleteDC(hdcmem);
	ReleaseDC(NULL, scr);

	/* Bitmaps are in BGR order and already clipped */
	yukino_image(&s->mbuf, bits, 24, 0x0000FF, 0x00FF00, 0xFF0000, YUKINO_IMAGE_ENDIAN_BIG,
		(((w * 24) + 31) & ~31) >> 3, 0, 0, w, h);

	*ps = s;
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_win32_screenshot_resolution(
	yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h)
{
	return yukino_image_resolution(&s->mbuf, w, h);
}

static yukino_result_t yukino_win32_screenshot_read(
	yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3])
{
	return yukino_image_read(&s->mbuf, rgb);
}

static yukino_result_t yukino_win32_screenshot_delete(
	yukino_connection_t *conn, yukino_screenshot_t *s)
{
	DeleteObject(s->bmp);
	free(s);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_win32_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	int i;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	/* wew */
	conn->conn_data.dwmapi = LoadLibraryA("DWMAPI.DLL");
	conn->conn_data.pDwmGetWindowAttribute = conn->conn_data.dwmapi ? (tpDwmGetWindowAttribute)GetProcAddress(conn->conn_data.dwmapi, "DwmGetWindowAttribute") : NULL;

	/* Fill the vtable */
	conn->disconnect = yukino_win32_disconnect;
	conn->display_resolution = yukino_win32_display_resolution;

	conn->window_iter_start = yukino_win32_window_iter_start;
	conn->window_iter = yukino_win32_window_iter;
	conn->window_iter_end = yukino_win32_window_iter_end;

	conn->window_position = yukino_win32_window_position;
	conn->window_decorated_position = yukino_win32_window_decorated_position;

	conn->lock = yukino_win32_lock;
	conn->unlock = yukino_win32_unlock;

	conn->screenshot = yukino_win32_screenshot;
	conn->screenshot_resolution = yukino_win32_screenshot_resolution;
	conn->screenshot_read = yukino_win32_screenshot_read;
	conn->screenshot_delete = yukino_win32_screenshot_delete;

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
