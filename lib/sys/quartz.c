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

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

#include "../yukino_c.h"

static yukino_result_t quartz_cgerror_to_yukino(CGError err)
{
	switch (err) {
	case kCGErrorIllegalArgument:
	case kCGErrorInvalidConnection:
	case kCGErrorInvalidContext:
	case kCGErrorInvalidOperation:
	case kCGErrorTypeCheck:
		return YUKINO_RESULT_INVALID_PARAM;
	case kCGErrorNoneAvailable:
		return YUKINO_RESULT_NONE;
	case kCGErrorFailure:
	case kCGErrorNotImplemented:
		return YUKINO_RESULT_UNSUPPORTED;
	case kCGErrorRangeCheck:
		/* FIXME make this better */
		return YUKINO_RESULT_UNSUPPORTED;
	case kCGErrorSuccess:
		return YUKINO_RESULT_OK;
	}

	/* Whatever */
	return YUKINO_RESULT_UNSUPPORTED;
}

static yukino_result_t yukino_quartz_disconnect(yukino_connection_t *conn)
{
	free(conn);
	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_quartz_display_resolution(
	yukino_connection_t *conn, uint32_t *w, uint32_t *h)
{
	/* Ughhhghghh */
	CGError err;
	uint32_t i, cnt;
	CGDirectDisplayID *disp;
	CGFloat x0, x1, y0, y1;

	err = CGGetActiveDisplayList(0, NULL, &cnt);
	if (err != kCGErrorSuccess)
		return quartz_cgerror_to_yukino(err);
	if (!cnt)
		return YUKINO_RESULT_NONE;

	disp = malloc(sizeof(*disp) * cnt);
	if (!disp)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	err = CGGetActiveDisplayList(cnt, disp, &cnt);
	if (err != kCGErrorSuccess) {
		free(disp);
		return quartz_cgerror_to_yukino(err);
	}

	/* FUCK */
	x0 = y0 = FLT_MAX;
	x1 = y1 = FLT_MIN;

	/* Now we can iterate */
	for (i = 0; i < cnt; i++) {
		CGRect r;

		/* XXX Hi-DPI monitors make this annoying, because of the
		 * way Apple decided to go about this
		 *
		 * For now I will just ignore they exist, because I don't
		 * have anything that MacOS will recognize as Hi-DPI, but
		 * it should be addressed eventually... */
		r = CGDisplayBounds(disp[i]);

		if (r.origin.x < x0)
			x0 = r.origin.x;
		if (r.origin.y < y0)
			y0 = r.origin.y;
		if ((r.origin.x + r.size.width) > x1)
			x1 = (r.origin.x + r.size.width);
		if ((r.origin.y + r.size.height) > y1)
			y1 = (r.origin.y + r.size.height);
	}

	/* Hope it works */
	*w = x1 - x0;
	*h = y1 - y0;

	free(disp);

	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

#define YUKINO_REALLOC_SIZE (32)
#define YUKINO_REALLOC_ADJUST(x) ((x) ? (x) + YUKINO_REALLOC_SIZE : (YUKINO_REALLOC_SIZE))

struct yukino_window_iter {
	/* Array of all windows, from front to back.
	 * (This means we have to iterate backwards) */
	CFArrayRef arr;

	/* counts from CFArrayGetCount(arr) to 1, and a value of
	 * 0 means we're done iterating.
	 *
	 * This means we actually grab the value at (i - 1), since
	 * getting i would mean a buffer overread. */
	CFIndex i;
};

static yukino_result_t yukino_quartz_window_iter_start(yukino_connection_t *conn,
	const yukino_window_t *win, yukino_window_iter_t **pwi)
{
	yukino_window_iter_t *wi;

	wi = calloc(1, sizeof(*wi));
	if (!wi)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	wi->arr = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
	if (!wi->arr) {
		free(wi);
		/* ??? */
		return YUKINO_RESULT_UNSUPPORTED;
	}

	wi->i = CFArrayGetCount(wi->arr);

	*pwi = wi;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_quartz_window_iter(
	yukino_connection_t *conn, yukino_window_iter_t *wi, yukino_window_t *pw)
{
	CFDictionaryRef dict;
	CFNumberRef num;
	SInt64 v;

	if (!wi || !pw)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!wi->i)
		return YUKINO_RESULT_DONE;

	dict = CFArrayGetValueAtIndex(wi->arr, wi->i - 1);
	if (!dict)
		return YUKINO_RESULT_INVALID_PARAM; /* What? */

	/* TODO need generic "server error" result */
	num = CFDictionaryGetValue(dict, kCGWindowNumber);
	if (!num)
		return YUKINO_RESULT_INVALID_PARAM;

	if (!CFNumberGetValue(num, kCFNumberSInt64Type, &v))
		return YUKINO_RESULT_INVALID_PARAM;

	/* ;) */
	*pw = (UInt64)v;

	wi->i--;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_quartz_window_iter_end(
	yukino_connection_t *conn, yukino_window_iter_t *wi)
{
	CFRelease(wi->arr);
	free(wi);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */
/* XXX: Need a "batch" API of sorts */

/*
This enables broken alpha handling that doesn't work.
It's only here so it can be implement properly once I give enough of a damn.
This is important for, for example, the Dock, which is just one giant window
across the whole screen. Thank you Apple.

#define QUARTZ_BROKEN_ALPHA_HANDLING 1
*/

/* Annoying
 * This also receives the w,h in case imaging it fails for whatever reason */
static yukino_result_t quartz_window_position(yukino_window_t win, yukino_rect_t *pr)
{
	CFArrayRef arr;
	CFIndex i, cnt;

	arr = CGWindowListCopyWindowInfo(kCGWindowListExcludeDesktopElements | kCGWindowListOptionIncludingWindow | kCGWindowListOptionOnScreenOnly, win);
	if (!arr)
		return YUKINO_RESULT_INVALID_PARAM; /* Poop */

	cnt = CFArrayGetCount(arr);

	for (i = 0; i < cnt; i++) {
		CFDictionaryRef dict, rect_dict;
		CFNumberRef num;
		CGRect rect;
		CFStringRef str;
		SInt64 v;

		/* Filter to the one thats actually our window */
		dict = CFArrayGetValueAtIndex(arr, i);
		if (!dict)
			break;

		/* XXX: Put this shit in a separate function, its reused above */
		num = CFDictionaryGetValue(dict, kCGWindowNumber);
		if (!num)
			break;

		if (!CFNumberGetValue(num, kCFNumberSInt64Type, &v))
			break;

		if (v != win)
			continue;

#ifndef QUARTZ_BROKEN_ALPHA_HANDLING
		/* Ignore the dock */
		str = CFDictionaryGetValue(dict, kCGWindowOwnerName);
		if (CFStringCompare(str, CFSTR("Dock"), 0) == kCFCompareEqualTo)
			continue;
#endif

		/* Ok, get the bounds */
		rect_dict = CFDictionaryGetValue(dict, kCGWindowBounds);
		if (!rect_dict)
			break;

		/* What the fuck, Apple? */
		if (!CGRectMakeWithDictionaryRepresentation(rect_dict, &rect))
			break;

		pr->x = rect.origin.x;
		pr->y = rect.origin.y;
		pr->w = rect.size.width;
		pr->h = rect.size.height;

		CFRelease(arr);
		return YUKINO_RESULT_OK;
	}

	CFRelease(arr);
	return YUKINO_RESULT_INVALID_PARAM;

}

#ifdef QUARTZ_BROKEN_ALPHA_HANDLING
/* So many lies */
static yukino_result_t quartz_fix_window_size(yukino_window_t win, yukino_rect_t *pr)
{
	CGImageRef img;
	int32_t x0, x1, y0, y1;
	uint32_t x, y, w, h;
	CGImageAlphaInfo alpha;
	CGDataProviderRef data_prov;
	CFDataRef dataref;
	bool first;
	const void *data;
	size_t bpc, bpr;
	uint8_t bpp;

	/* ok... */
	if (!CGRequestScreenCaptureAccess())
		return YUKINO_RESULT_UNSUPPORTED;

	img = CGWindowListCreateImage(CGRectNull, 0, win, kCGWindowImageNominalResolution | kCGWindowImageBoundsIgnoreFraming);
	if (!img)
		return YUKINO_RESULT_UNSUPPORTED;

	alpha = CGImageGetAlphaInfo(img);
	switch (alpha) {
	case kCGImageAlphaOnly:
	case kCGImageAlphaNone:
	case kCGImageAlphaNoneSkipFirst:
		CFRelease(img);
		return YUKINO_RESULT_OK;
	case kCGImageAlphaFirst:
	case kCGImageAlphaPremultipliedFirst:
		first = true;
		break;
	case kCGImageAlphaLast:
	case kCGImageAlphaPremultipliedLast:
		first = false;
		break;
	}

	/* Poop... */
	x0 = w = CGImageGetWidth(img);
	y0 = h = CGImageGetHeight(img);
	x1 = y1 = 0;

	bpc = CGImageGetBitsPerComponent(img);
	bpr = CGImageGetBytesPerRow(img);

	bpp = bpc * 4;

	data_prov = CGImageGetDataProvider(img);
	dataref = CGDataProviderCopyData(data_prov);
	data = CFDataGetBytePtr(dataref);

	/* Loop over the resulting image. */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			/* GUH!? */
			unsigned char *pxl = (unsigned char *)data + ((bpp * x) >> 3);

			/* So many fucking factors to take into account, fml
			 * Why did Apple decide it was a good idea to make it
			 * this way? */
			if (1) {
				uint32_t d;

				d = 0;
				memcpy(&d, pxl, (bpp + 7) / 8);

				/* Skip if this pixel is not transparent */
				if (d) continue;

				/* SEiruewirweyru8 */
				if (x < x0)
					x0 = x;
				if (y < y0)
					y0 = y;
				if (x > x1)
					x1 = x;
				if (y > y1)
					y1 = y;
			} else {
				/* FIXME Implement */
				CFRelease(img);
				CFRelease(dataref);
				return YUKINO_RESULT_UNSUPPORTED;
			}
		}

		data = (char *)data + bpr;
	}

	pr->h = y1 - y0;
	pr->w = x1 - x0;

	CFRelease(img);
	CFRelease(dataref);
	return YUKINO_RESULT_OK;
}
#endif

static yukino_result_t yukino_quartz_window_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	yukino_result_t r;

	/* Invalid window ID? */
	if ((CGWindowID)win != win)
		return YUKINO_RESULT_INVALID_PARAM;

	if ((r = quartz_window_position(win, pr)) < 0)
		return r;

#ifdef QUARTZ_BROKEN_ALPHA_HANDLING
	if ((r = quartz_fix_window_size(win, pr)) < 0)
		return r;
#endif

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_quartz_window_decorated_position(
	yukino_connection_t *conn, yukino_window_t win, yukino_rect_t *pr)
{
	/* Not sure if this is possible...? */
	return YUKINO_RESULT_UNSUPPORTED;
}

/* ------------------------------------------------------------------------ */

struct yukino_screenshot {
	CFDataRef data; /* Must be released */

	/* Memory buffer */
	struct yukino_image mbuf;
};

static yukino_result_t yukino_quartz_screenshot(yukino_connection_t *conn,
	yukino_screenshot_t **ps, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	CGRect bounds;
	CGImageRef img, img_orig;
	CGDataProviderRef data_prov;
	yukino_result_t r;
	uint8_t bpc;
	size_t stride;
	yukino_screenshot_t *s;

	if (!CGRequestScreenCaptureAccess())
		return YUKINO_RESULT_UNSUPPORTED;

	if ((r = yukino_screenshot_fix_resolution(conn, &w, &h)) < 0)
		return r;

	s = malloc(sizeof(*s));
	if (!s)
		return r;

	bounds.origin.x = x;
	bounds.origin.y = y;
	bounds.size.width = w;
	bounds.size.height = h;

	/* FIXME test on hi-dpi */
	img = CGWindowListCreateImage(bounds, kCGWindowListOptionAll, kCGNullWindowID, kCGWindowImageNominalResolution);
	if (!img) {
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	w = CGImageGetWidth(img);
	h = CGImageGetHeight(img);

	bpc = CGImageGetBitsPerComponent(img);

	stride = CGImageGetBytesPerRow(img);

	data_prov = CGImageGetDataProvider(img);
	s->data = CGDataProviderCopyData(data_prov);
	CFRelease(img);
	/* Error if data ptr is invalid */
	if (!s->data) {
		free(s);
		return YUKINO_RESULT_UNSUPPORTED;
	}

	/* poop in the butt */
	yukino_image(&s->mbuf, CFDataGetBytePtr(s->data), bpc << 2,
		((1 << bpc) - 1) << (bpc * 2),
		((1 << bpc) - 1) << (bpc),
		((1 << bpc) - 1) << (0),
		YUKINO_IMAGE_ENDIAN_NATIVE, stride,
		0, 0, w, h);

	*ps = s;

	return YUKINO_RESULT_OK;
}

static yukino_result_t yukino_quartz_screenshot_resolution(
	yukino_connection_t *conn, yukino_screenshot_t *s, uint32_t *w, uint32_t *h)
{
	return yukino_image_resolution(&s->mbuf, w, h);
}

static yukino_result_t yukino_quartz_screenshot_read(
	yukino_connection_t *conn, yukino_screenshot_t *s, unsigned char rgb[3])
{
	return yukino_image_read(&s->mbuf, rgb);
}

static yukino_result_t yukino_quartz_screenshot_delete(
	yukino_connection_t *conn, yukino_screenshot_t *s)
{
	CFRelease(s->data);
	free(s);
	return YUKINO_RESULT_OK;
}

/* ------------------------------------------------------------------------ */

yukino_result_t yukino_quartz_connect(yukino_connection_t **pconn)
{
	yukino_connection_t *conn;
	int i;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return YUKINO_RESULT_OUT_OF_MEMORY;

	/* Fill the vtable */
	conn->disconnect = yukino_quartz_disconnect;
	conn->display_resolution = yukino_quartz_display_resolution;

	conn->window_iter_start = yukino_quartz_window_iter_start;
	conn->window_iter = yukino_quartz_window_iter;
	conn->window_iter_end = yukino_quartz_window_iter_end;

	conn->window_position = yukino_quartz_window_position;
	conn->window_decorated_position = yukino_quartz_window_decorated_position;

	conn->lock = NULL;
	conn->unlock = NULL;

	conn->screenshot = yukino_quartz_screenshot;
	conn->screenshot_resolution = yukino_quartz_screenshot_resolution;
	conn->screenshot_read = yukino_quartz_screenshot_read;
	conn->screenshot_delete = yukino_quartz_screenshot_delete;

	*pconn = conn;
	return YUKINO_RESULT_OK;
}
