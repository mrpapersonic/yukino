#include "../yukino.h"

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

	if (x < 10) pl = 1;
	else if (x < 100) pl = 2;
	else if (x < 1000) pl = 3;
	else if (x < 10000) pl = 4;
	else if (x < 100000) pl = 5;
	else if (x < 1000000) pl = 6;
	else if (x < 10000000) pl = 7;
	else if (x < 100000000) pl = 8;
	else if (x < 1000000000) pl = 9;
	else pl = 10;

	p = pl;
	while (p--) {
		s[p] = (x % 10) + '0';
		x /= 10;
	}

	return pl;
}

yukino_result_t yukino_write_ppm(
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_write_cb write_cb, void *userdata,
		yukino_take_t take_cb, void *take_data)
{
	struct cbuserdata ud = {write_cb, userdata};
	char xx[10];
	size_t sz;
	yukino_result_t r;

	if ((r = write_cb(userdata, "P6\n", 3)) < 0)
		return r;

	sz = u32tostr(w, xx);

	if ((r = write_cb(userdata, xx, sz)) < 0)
		return r;

	if ((r = write_cb(userdata, " ", 1)) < 0)
		return r;

	sz = u32tostr(h, xx);

	if ((r = write_cb(userdata, xx, sz)) < 0)
		return r;

	if ((r = write_cb(userdata, "\n255\n", 5)) < 0)
		return r;

	return take_cb(take_data, x, y, w, h, ppm_cb, &ud);
}
