#include "../yukino.h"
#include "../yukino_c.h"

struct png {
	/* current chunk crc */
	uint32_t crc;

	/* calculated size of image data (w * h * 3) */
	uint32_t sz;

	uint16_t j;

	/* need this because png is stupid */
	uint32_t x, w;

	/* Adler-32 */
	struct yukino_adler32 a32;

	yukino_write_cb write_cb;
	void *userdata;
};

/* byteswap to/from big endian */
static uint16_t png_bswap16(uint16_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return (px[1]) | ((uint16_t)px[0] << 8);
}

static uint32_t png_bswap32(uint32_t x)
{
	const unsigned char *px = (const unsigned char *)&x;

	return (px[3]) | ((uint32_t)px[2] << 8) | ((uint32_t)px[1] << 16) | ((uint32_t)px[0] << 24);
}

/* big write function */
static yukino_result_t png_write(struct png *png, const void *bytes, size_t sz)
{
	yukino_result_t r;

	if ((r = png->write_cb(png->userdata, bytes, sz)) < 0)
		return r;

	/* Do the CRC */
	png->crc = yukino_crc32(png->crc, bytes, sz);

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_write_u16(struct png *png, uint16_t u)
{
	yukino_result_t r;

	u = png_bswap16(u);

	if ((r = png_write(png, &u, sizeof(u))) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_write_u32(struct png *png, uint32_t u)
{
	yukino_result_t r;

	u = png_bswap32(u);

	if ((r = png_write(png, &u, sizeof(u))) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_deflate_header_impl(struct png *png, unsigned char b, uint16_t sz)
{
	yukino_result_t r;

	if ((r = png_write(png, &b, 1)) < 0)
		return r;

	if ((r = png_write_u16(png, sz)) < 0)
		return r;

	if ((r = png_write_u16(png, ~sz)) < 0)
		return r;

	png->j = sz;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_deflate_header(struct png *png, size_t sz)
{
	return (sz > 65535)
		? png_deflate_header_impl(png, 0, 65535)
		: png_deflate_header_impl(png, 1, sz);
}

static yukino_result_t png_deflate_write(struct png *png, const void *b_, size_t sz)
{
	yukino_result_t r;
	const unsigned char *b = b_;

	/* accumulate adler32 .. only for uncompressed data */

	yukino_adler32(&png->a32, b, sz);

	while (sz > 0) {
		size_t tocpy;

		/* Write deflate header if necessary */
		if (!png->j && ((r = png_deflate_header(png, png->sz)) < 0))
			return r;

		tocpy = sz;
		if (tocpy > png->j)
			tocpy = png->j;

		if ((r = png_write(png, b, tocpy)) < 0)
			return r;

		png->sz -= tocpy;
		png->j -= tocpy;
		sz -= tocpy;
		b += tocpy;
	}

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_zlib_header(struct png *png, size_t sz)
{
	/* init adler-32 */
	yukino_adler32_init(&png->a32);

	png->j = 0;
	png->sz = sz;

	/* write the zlib header */
	return png_write(png, "\x78\x01", 2);
}

static yukino_result_t png_zlib_footer(struct png *png)
{
	uint32_t a32 = yukino_adler32_get(&png->a32);

	return png_write_u32(png, a32);
}

static yukino_result_t png_chunk_head(struct png *png, const char id[4], uint32_t sz)
{
	yukino_result_t r;

	if ((r = png_write_u32(png, sz)) < 0)
		return r;

	/* Reset the CRC */
	png->crc = 0xFFFFFFFF;
	if ((r = png_write(png, id, 4)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_chunk_tail(struct png *png)
{
	yukino_result_t r;

	if ((r = png_write_u32(png, ~png->crc)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

static yukino_result_t png_cb(void *userdata, unsigned char rgb[3])
{
	struct png *ud = userdata;
	yukino_result_t r;

	if (!ud->x && ((r = png_deflate_write(ud, "", 1)) < 0))
		return r;
	ud->x = (ud->x + 1) % ud->w;

	/* Split it */
	if ((r = png_deflate_write(ud, rgb, 3)) < 0)
		return r;

	return YUKINO_RESULT_OK;
}

/* based on libpng docs */
yukino_result_t yukino_write_png(
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		yukino_write_cb write_cb, void *userdata,
		yukino_take_t take_cb, void *take_data)
{
	static const unsigned char magic[] = {137, 80, 78, 71, 13, 10, 26, 10};
	yukino_result_t r;
	struct png png;

	if ((r = write_cb(userdata, magic, sizeof(magic))) < 0)
		return r;

	png.crc = 0xFFFFFFFF;
	png.write_cb = write_cb;
	png.userdata = userdata;
	png.x = 0;
	png.w = w;

	/* IHDR chunk */
	if ((r = png_chunk_head(&png, "IHDR", 13)) < 0)
		return r;

	png_write_u32(&png, w);
	png_write_u32(&png, h);
	png_write(&png, "\x08\x02\x00\x00\x00", 5);

	png_chunk_tail(&png);

	/* Now for the big business -- we have to "fake"
	 * a zlib and just store everything plain. but
	 * we also need the data size *before*, so we gotta
	 * do some calculamalations */
	{
		uint32_t imgsz, sz;

		imgsz = (w * h * 3) + h;

		/* zlib hdr, uncompressed data, deflate headers, zlib adler32 */
		sz = 2 + imgsz + (5 * ((imgsz + 65534) / 65535)) + 4;

		png_chunk_head(&png, "IDAT", sz);

		/* write the zlib header first */
		png_zlib_header(&png, imgsz);

		/* ... FINALLY */
		take_cb(take_data, x, y, w, h, png_cb, &png);

		png_zlib_footer(&png);

		png_chunk_tail(&png);
	}

	/* GET ME A CHEESE WITH NOTTIN */
	png_chunk_head(&png, "IEND", 0);

	png_chunk_tail(&png);

	return YUKINO_RESULT_OK;
}
