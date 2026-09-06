/*
 *  Decoders for PCX, TGA, SGI/RGB, PNM and XBM - see simpleimg.h.
 *
 *  Deliberately dependency-free: <stdlib.h> and <string.h> only, so the same
 *  sources can be compiled natively and checked against an independent reader
 *  before being built as a side module.
 */

#include <stdlib.h>
#include <string.h>

#include "simpleimg.h"

void simpleimg_free(unsigned char *buffer)
{
	free(buffer);
}

static int ascii_tolower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int ext_is(const char *ext, const char *name)
{
	size_t i;
	for (i = 0; name[i]; i++)
	{
		if (!ext[i] || ascii_tolower((unsigned char)ext[i]) != name[i])
			return 0;
	}
	return ext[i] == 0;
}

int simpleimg_format_from_ext(const char *ext, SimpleImgFormat *format)
{
	if (!ext || !format)
		return 0;
	if (ext_is(ext, "pcx"))
	{
		*format = SIMPLEIMG_PCX;
		return 1;
	}
	if (ext_is(ext, "tga") || ext_is(ext, "targa") || ext_is(ext, "icb") ||
	    ext_is(ext, "vda") || ext_is(ext, "vst"))
	{
		*format = SIMPLEIMG_TGA;
		return 1;
	}
	if (ext_is(ext, "sgi") || ext_is(ext, "rgb") || ext_is(ext, "rgba") ||
	    ext_is(ext, "bw") || ext_is(ext, "int") || ext_is(ext, "inta"))
	{
		*format = SIMPLEIMG_SGI;
		return 1;
	}
	if (ext_is(ext, "pnm") || ext_is(ext, "ppm") || ext_is(ext, "pgm") || ext_is(ext, "pbm"))
	{
		*format = SIMPLEIMG_PNM;
		return 1;
	}
	if (ext_is(ext, "xbm"))
	{
		*format = SIMPLEIMG_XBM;
		return 1;
	}
	return 0;
}

/* Guards every buffer write: dimensions come straight from the file header, so
 * a corrupt or hostile one must not be able to size an allocation past what the
 * pixel loops then fill. */
static int alloc_rgb(unsigned int w, unsigned int h, unsigned char **out)
{
	size_t bytes;
	if (!w || !h || w > 65535 || h > 65535)
		return SIMPLEIMG_ERR_BITSTREAM;
	bytes = (size_t)w * h * 3;
	*out = (unsigned char *)malloc(bytes);
	if (!*out)
		return SIMPLEIMG_ERR_MEMORY;
	memset(*out, 0, bytes);
	return SIMPLEIMG_OK;
}

/* ------------------------------------------------------------------------- */
/* PCX (ZSoft)                                                               */
/* ------------------------------------------------------------------------- */

/* Unpacks one PCX scanline. The RLE is per scanline in principle but encoders
 * are allowed to let runs cross the boundary, so decoding is driven by the
 * output count and the input position is carried across calls. */
static int pcx_unpack(const unsigned char *data, size_t size, size_t *pos,
                      unsigned char *line, unsigned int line_bytes)
{
	unsigned int done = 0;
	while (done < line_bytes)
	{
		unsigned char b, value;
		unsigned int count;
		if (*pos >= size)
			return SIMPLEIMG_ERR_BITSTREAM;
		b = data[(*pos)++];
		if ((b & 0xC0) == 0xC0)
		{
			count = b & 0x3F;
			if (*pos >= size)
				return SIMPLEIMG_ERR_BITSTREAM;
			value = data[(*pos)++];
		}
		else
		{
			count = 1;
			value = b;
		}
		if (count > line_bytes - done)
			count = line_bytes - done;
		memset(line + done, value, count);
		done += count;
	}
	return SIMPLEIMG_OK;
}

static int decode_pcx(const unsigned char *data, size_t size,
                      unsigned char **out, unsigned int *width, unsigned int *height)
{
	unsigned int w, h, bpp, planes, bytes_per_line, line_bytes, x, y;
	const unsigned char *palette = NULL;
	unsigned char ega[48];
	unsigned char *line = NULL, *rgb = NULL;
	size_t pos = 128;
	int err;

	if (size < 128 || data[0] != 0x0A)
		return SIMPLEIMG_ERR_BITSTREAM;
	if (data[2] != 1)
		return SIMPLEIMG_ERR_VARIANT; /* 0 would be uncompressed, never produced in practice */

	bpp = data[3];
	{
		unsigned int xmin = data[4] | (data[5] << 8);
		unsigned int ymin = data[6] | (data[7] << 8);
		unsigned int xmax = data[8] | (data[9] << 8);
		unsigned int ymax = data[10] | (data[11] << 8);
		if (xmax < xmin || ymax < ymin)
			return SIMPLEIMG_ERR_BITSTREAM;
		w = xmax - xmin + 1;
		h = ymax - ymin + 1;
	}
	memcpy(ega, data + 16, 48);
	planes = data[65];
	bytes_per_line = data[66] | (data[67] << 8);
	if (!bytes_per_line || !planes || planes > 4)
		return SIMPLEIMG_ERR_VARIANT;
	line_bytes = bytes_per_line * planes;

	/* The 256-colour palette, when there is one, is the last 769 bytes of the
	 * file behind a 0x0C marker. */
	if (bpp == 8 && planes == 1 && size >= 769 && data[size - 769] == 0x0C)
		palette = data + size - 768;

	err = alloc_rgb(w, h, &rgb);
	if (err != SIMPLEIMG_OK)
		return err;
	line = (unsigned char *)malloc(line_bytes);
	if (!line)
	{
		free(rgb);
		return SIMPLEIMG_ERR_MEMORY;
	}

	for (y = 0; y < h; y++)
	{
		unsigned char *row = rgb + (size_t)y * w * 3;
		err = pcx_unpack(data, size, &pos, line, line_bytes);
		if (err != SIMPLEIMG_OK)
			goto fail;

		if (bpp == 8 && planes == 3)
		{
			/* Planar within the scanline: all reds, then all greens, then blues */
			for (x = 0; x < w; x++)
			{
				row[x * 3] = line[x];
				row[x * 3 + 1] = line[bytes_per_line + x];
				row[x * 3 + 2] = line[2 * bytes_per_line + x];
			}
		}
		else if (bpp == 8 && planes == 1)
		{
			for (x = 0; x < w; x++)
			{
				unsigned char idx = line[x];
				if (palette)
					memcpy(row + x * 3, palette + idx * 3, 3);
				else
					row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = idx;
			}
		}
		else if (bpp == 1)
		{
			/* One bit per plane: the planes together index the 16-entry EGA
			 * palette carried in the header. */
			for (x = 0; x < w; x++)
			{
				unsigned int p, idx = 0;
				for (p = 0; p < planes; p++)
				{
					const unsigned char *pl = line + p * bytes_per_line;
					if (pl[x >> 3] & (0x80 >> (x & 7)))
						idx |= 1u << p;
				}
				if (planes == 1)
				{
					/* Bilevel: black and white rather than the palette, which
					 * monochrome writers do not fill in. */
					unsigned char v = idx ? 255 : 0;
					row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v;
				}
				else
				{
					memcpy(row + x * 3, ega + (idx & 15) * 3, 3);
				}
			}
		}
		else
		{
			err = SIMPLEIMG_ERR_VARIANT;
			goto fail;
		}
	}

	free(line);
	*out = rgb;
	*width = w;
	*height = h;
	return SIMPLEIMG_OK;

fail:
	free(line);
	free(rgb);
	return err;
}

/* ------------------------------------------------------------------------- */
/* TGA (Truevision)                                                          */
/* ------------------------------------------------------------------------- */

/* Expands one source pixel - BGR, BGRA or grey - to RGB. */
static void tga_pixel_to_rgb(const unsigned char *px, unsigned int depth, unsigned char *dst)
{
	if (depth == 8)
	{
		dst[0] = dst[1] = dst[2] = px[0];
	}
	else if (depth == 16)
	{
		/* 5-5-5 with the top bit as attribute; the 5-bit values are scaled so
		 * that 31 maps to 255 rather than 248. */
		unsigned int v = px[0] | (px[1] << 8);
		unsigned int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
		dst[0] = (unsigned char)((r * 255 + 15) / 31);
		dst[1] = (unsigned char)((g * 255 + 15) / 31);
		dst[2] = (unsigned char)((b * 255 + 15) / 31);
	}
	else
	{
		dst[0] = px[2];
		dst[1] = px[1];
		dst[2] = px[0];
	}
}

static int decode_tga(const unsigned char *data, size_t size,
                      unsigned char **out, unsigned int *width, unsigned int *height)
{
	unsigned int id_len, cmap_type, type, cmap_len, cmap_entry, w, h, depth, descriptor;
	unsigned int bytes_pp, x, y, top_origin, right_origin;
	const unsigned char *cmap = NULL;
	unsigned char *rgb = NULL;
	size_t pos;
	int err, rle;

	if (size < 18)
		return SIMPLEIMG_ERR_BITSTREAM;
	id_len = data[0];
	cmap_type = data[1];
	type = data[2];
	cmap_len = data[5] | (data[6] << 8);
	cmap_entry = data[7];
	w = data[12] | (data[13] << 8);
	h = data[14] | (data[15] << 8);
	depth = data[16];
	descriptor = data[17];
	top_origin = (descriptor & 0x20) != 0;
	right_origin = (descriptor & 0x10) != 0;

	rle = (type == 9 || type == 10 || type == 11);
	if (type != 1 && type != 2 && type != 3 && !rle)
		return SIMPLEIMG_ERR_VARIANT;
	if (depth != 8 && depth != 16 && depth != 24 && depth != 32)
		return SIMPLEIMG_ERR_VARIANT;
	bytes_pp = depth / 8;

	pos = 18 + id_len;
	if (cmap_type)
	{
		unsigned int entry_bytes = (cmap_entry + 7) / 8;
		if (cmap_entry != 15 && cmap_entry != 16 && cmap_entry != 24 && cmap_entry != 32)
			return SIMPLEIMG_ERR_VARIANT;
		cmap = data + pos;
		if (pos + (size_t)cmap_len * entry_bytes > size)
			return SIMPLEIMG_ERR_BITSTREAM;
		pos += (size_t)cmap_len * entry_bytes;
	}

	err = alloc_rgb(w, h, &rgb);
	if (err != SIMPLEIMG_OK)
		return err;

	{
		/* Colour-mapped images index the palette, so the pixel size on disk and
		 * the size of the value it expands to differ; both are needed. */
		const unsigned int mapped = (type == 1 || type == 9);
		const unsigned int cmap_bytes = cmap ? (cmap_entry + 7) / 8 : 0;
		unsigned char run_pixel[4];
		unsigned int run_left = 0, run_is_raw = 0;

		for (y = 0; y < h; y++)
		{
			unsigned int dst_y = top_origin ? y : (h - 1 - y);
			unsigned char *row = rgb + (size_t)dst_y * w * 3;
			for (x = 0; x < w; x++)
			{
				unsigned int dst_x = right_origin ? (w - 1 - x) : x;
				unsigned char px[4];

				if (rle && run_left == 0)
				{
					unsigned char hdr;
					if (pos >= size)
					{
						err = SIMPLEIMG_ERR_BITSTREAM;
						goto fail;
					}
					hdr = data[pos++];
					run_left = (hdr & 0x7F) + 1;
					run_is_raw = (hdr & 0x80) == 0;
					if (!run_is_raw)
					{
						if (pos + bytes_pp > size)
						{
							err = SIMPLEIMG_ERR_BITSTREAM;
							goto fail;
						}
						memcpy(run_pixel, data + pos, bytes_pp);
						pos += bytes_pp;
					}
				}

				if (rle && !run_is_raw)
				{
					memcpy(px, run_pixel, bytes_pp);
				}
				else
				{
					if (pos + bytes_pp > size)
					{
						err = SIMPLEIMG_ERR_BITSTREAM;
						goto fail;
					}
					memcpy(px, data + pos, bytes_pp);
					pos += bytes_pp;
				}
				if (rle)
					run_left--;

				if (mapped)
				{
					unsigned int idx = px[0] | (bytes_pp > 1 ? (px[1] << 8) : 0);
					if (!cmap || idx >= cmap_len)
					{
						err = SIMPLEIMG_ERR_BITSTREAM;
						goto fail;
					}
					tga_pixel_to_rgb(cmap + (size_t)idx * cmap_bytes,
					                 cmap_entry == 15 ? 16 : cmap_entry,
					                 row + dst_x * 3);
				}
				else
				{
					tga_pixel_to_rgb(px, depth, row + dst_x * 3);
				}
			}
		}
	}

	*out = rgb;
	*width = w;
	*height = h;
	return SIMPLEIMG_OK;

fail:
	free(rgb);
	return err;
}

/* ------------------------------------------------------------------------- */
/* SGI / RGB (Silicon Graphics)                                              */
/* ------------------------------------------------------------------------- */

static unsigned int be16(const unsigned char *p) { return (unsigned int)(p[0] << 8) | p[1]; }
static unsigned long be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
	       ((unsigned long)p[2] << 8) | p[3];
}

/* One RLE-compressed SGI scanline: a count byte whose low 7 bits give the
 * length and whose top bit selects literal bytes over a repeated one; a zero
 * count ends the line. */
static int sgi_rle_row(const unsigned char *data, size_t size, size_t offset, size_t length,
                       unsigned char *row, unsigned int xsize)
{
	size_t p = offset, end = offset + length;
	unsigned int done = 0;
	if (end > size || offset > size)
		return SIMPLEIMG_ERR_BITSTREAM;
	while (p < end)
	{
		unsigned char b = data[p++];
		unsigned int count = b & 0x7F;
		if (!count)
			break;
		if (count > xsize - done)
			count = xsize - done;
		if (b & 0x80)
		{
			if (p + count > end)
				return SIMPLEIMG_ERR_BITSTREAM;
			memcpy(row + done, data + p, count);
			p += count;
		}
		else
		{
			if (p >= end)
				return SIMPLEIMG_ERR_BITSTREAM;
			memset(row + done, data[p++], count);
		}
		done += count;
		if (done >= xsize)
			break;
	}
	return SIMPLEIMG_OK;
}

static int decode_sgi(const unsigned char *data, size_t size,
                      unsigned char **out, unsigned int *width, unsigned int *height)
{
	unsigned int storage, bpc, dimension, w, h, channels, c, y, x;
	unsigned char *rgb = NULL, *row = NULL;
	int err;

	if (size < 512 || be16(data) != 474)
		return SIMPLEIMG_ERR_BITSTREAM;
	storage = data[2];
	bpc = data[3];
	dimension = be16(data + 4);
	w = be16(data + 6);
	h = be16(data + 8);
	channels = be16(data + 10);

	if (bpc != 1)
		return SIMPLEIMG_ERR_VARIANT; /* 16-bit-per-channel SGI images */
	if (dimension == 1)
		h = 1;
	if (dimension < 3)
		channels = 1;
	if (channels != 1 && channels != 3 && channels != 4)
		return SIMPLEIMG_ERR_VARIANT;

	err = alloc_rgb(w, h, &rgb);
	if (err != SIMPLEIMG_OK)
		return err;
	row = (unsigned char *)malloc(w);
	if (!row)
	{
		free(rgb);
		return SIMPLEIMG_ERR_MEMORY;
	}

	for (c = 0; c < channels && c < 3; c++)
	{
		for (y = 0; y < h; y++)
		{
			/* SGI stores rows bottom-up. */
			unsigned char *dst = rgb + (size_t)(h - 1 - y) * w * 3;
			unsigned int index = c * h + y;

			memset(row, 0, w);
			if (storage == 0)
			{
				size_t offset = 512 + (size_t)index * w;
				if (offset + w > size)
				{
					err = SIMPLEIMG_ERR_BITSTREAM;
					goto fail;
				}
				memcpy(row, data + offset, w);
			}
			else if (storage == 1)
			{
				size_t table = 512;
				size_t n = (size_t)h * channels;
				size_t off_pos = table + (size_t)index * 4;
				size_t len_pos = table + n * 4 + (size_t)index * 4;
				if (len_pos + 4 > size)
				{
					err = SIMPLEIMG_ERR_BITSTREAM;
					goto fail;
				}
				err = sgi_rle_row(data, size, (size_t)be32(data + off_pos),
				                  (size_t)be32(data + len_pos), row, w);
				if (err != SIMPLEIMG_OK)
					goto fail;
			}
			else
			{
				err = SIMPLEIMG_ERR_VARIANT;
				goto fail;
			}

			if (channels == 1)
			{
				for (x = 0; x < w; x++)
					dst[x * 3] = dst[x * 3 + 1] = dst[x * 3 + 2] = row[x];
			}
			else
			{
				for (x = 0; x < w; x++)
					dst[x * 3 + c] = row[x];
			}
		}
	}

	free(row);
	*out = rgb;
	*width = w;
	*height = h;
	return SIMPLEIMG_OK;

fail:
	free(row);
	free(rgb);
	return err;
}

/* ------------------------------------------------------------------------- */
/* PNM (PBM / PGM / PPM, ASCII and binary)                                   */
/* ------------------------------------------------------------------------- */

static int pnm_is_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/* Reads one header field, skipping whitespace and '#' comments. */
static int pnm_next_int(const unsigned char *data, size_t size, size_t *pos, unsigned long *value)
{
	unsigned long v = 0;
	int digits = 0;
	while (*pos < size)
	{
		unsigned char c = data[*pos];
		if (pnm_is_space(c))
		{
			(*pos)++;
		}
		else if (c == '#')
		{
			while (*pos < size && data[*pos] != '\n')
				(*pos)++;
		}
		else
		{
			break;
		}
	}
	while (*pos < size && data[*pos] >= '0' && data[*pos] <= '9')
	{
		v = v * 10 + (unsigned long)(data[(*pos)++] - '0');
		if (v > 0xFFFFFFFFuL)
			return SIMPLEIMG_ERR_BITSTREAM;
		digits++;
	}
	if (!digits)
		return SIMPLEIMG_ERR_BITSTREAM;
	*value = v;
	return SIMPLEIMG_OK;
}

static int decode_pnm(const unsigned char *data, size_t size,
                      unsigned char **out, unsigned int *width, unsigned int *height)
{
	unsigned long w, h, maxval = 1;
	unsigned int type, x, y, samples, sixteen;
	unsigned char *rgb = NULL;
	size_t pos = 2;
	int err;

	if (size < 3 || data[0] != 'P' || data[1] < '1' || data[1] > '6')
		return SIMPLEIMG_ERR_BITSTREAM;
	type = data[1] - '0';

	if (pnm_next_int(data, size, &pos, &w) != SIMPLEIMG_OK)
		return SIMPLEIMG_ERR_BITSTREAM;
	if (pnm_next_int(data, size, &pos, &h) != SIMPLEIMG_OK)
		return SIMPLEIMG_ERR_BITSTREAM;
	if (type != 1 && type != 4)
	{
		if (pnm_next_int(data, size, &pos, &maxval) != SIMPLEIMG_OK || !maxval || maxval > 65535)
			return SIMPLEIMG_ERR_BITSTREAM;
	}
	/* Binary sample data starts after exactly one whitespace byte. */
	if (type >= 4)
	{
		if (pos >= size || !pnm_is_space(data[pos]))
			return SIMPLEIMG_ERR_BITSTREAM;
		pos++;
	}

	samples = (type == 3 || type == 6) ? 3 : 1;
	sixteen = (maxval > 255);

	err = alloc_rgb((unsigned int)w, (unsigned int)h, &rgb);
	if (err != SIMPLEIMG_OK)
		return err;

	if (type == 4)
	{
		/* Binary bitmap: rows padded to a byte, and a set bit means black. */
		size_t stride = (w + 7) / 8;
		if (pos + stride * h > size)
		{
			err = SIMPLEIMG_ERR_BITSTREAM;
			goto fail;
		}
		for (y = 0; y < h; y++)
		{
			const unsigned char *src = data + pos + (size_t)y * stride;
			unsigned char *row = rgb + (size_t)y * w * 3;
			for (x = 0; x < w; x++)
			{
				unsigned char v = (src[x >> 3] & (0x80 >> (x & 7))) ? 0 : 255;
				row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v;
			}
		}
	}
	else if (type == 5 || type == 6)
	{
		size_t need = (size_t)w * h * samples * (sixteen ? 2 : 1);
		if (pos + need > size)
		{
			err = SIMPLEIMG_ERR_BITSTREAM;
			goto fail;
		}
		for (y = 0; y < h; y++)
		{
			unsigned char *row = rgb + (size_t)y * w * 3;
			for (x = 0; x < w; x++)
			{
				unsigned int s;
				unsigned char v[3];
				for (s = 0; s < samples; s++)
				{
					size_t off = pos + (((size_t)y * w + x) * samples + s) * (sixteen ? 2 : 1);
					unsigned long raw = sixteen ? ((unsigned long)data[off] << 8 | data[off + 1])
					                            : data[off];
					v[s] = (unsigned char)(maxval == 255 ? raw : (raw * 255 + maxval / 2) / maxval);
				}
				if (samples == 1)
					row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v[0];
				else
					memcpy(row + x * 3, v, 3);
			}
		}
	}
	else
	{
		/* ASCII variants: P1 stores one 0/1 per pixel where 1 is black, P2 and
		 * P3 store decimal samples. */
		for (y = 0; y < h; y++)
		{
			unsigned char *row = rgb + (size_t)y * w * 3;
			for (x = 0; x < w; x++)
			{
				unsigned int s;
				unsigned char v[3];
				for (s = 0; s < samples; s++)
				{
					unsigned long raw;
					if (pnm_next_int(data, size, &pos, &raw) != SIMPLEIMG_OK)
					{
						err = SIMPLEIMG_ERR_BITSTREAM;
						goto fail;
					}
					if (type == 1)
						v[s] = raw ? 0 : 255;
					else
						v[s] = (unsigned char)(maxval == 255 ? raw : (raw * 255 + maxval / 2) / maxval);
				}
				if (samples == 1)
					row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v[0];
				else
					memcpy(row + x * 3, v, 3);
			}
		}
	}

	*out = rgb;
	*width = (unsigned int)w;
	*height = (unsigned int)h;
	return SIMPLEIMG_OK;

fail:
	free(rgb);
	return err;
}

/* ------------------------------------------------------------------------- */
/* XBM (X11 bitmap)                                                          */
/* ------------------------------------------------------------------------- */

/* An XBM file is a C fragment: two (sometimes four) #defines and an array of
 * byte literals. Only the trailing "_width"/"_height" of the define names is
 * meaningful, the prefix being the image name, so the parser matches on those
 * suffixes rather than on a fixed name. */
static int xbm_find_define(const unsigned char *data, size_t size, const char *suffix,
                           unsigned long *value)
{
	size_t i, slen = strlen(suffix);
	for (i = 0; i + 7 < size; i++)
	{
		size_t j;
		if (memcmp(data + i, "#define", 7) != 0)
			continue;
		j = i + 7;
		while (j < size && data[j] != '\n')
			j++;
		/* scan back from end of line for the suffix, then take the number */
		{
			size_t line_end = j, k;
			for (k = i + 7; k + slen <= line_end; k++)
			{
				if (memcmp(data + k, suffix, slen) == 0)
				{
					size_t p = k + slen;
					unsigned long v = 0;
					int digits = 0;
					while (p < line_end && pnm_is_space(data[p]))
						p++;
					while (p < line_end && data[p] >= '0' && data[p] <= '9')
					{
						v = v * 10 + (unsigned long)(data[p++] - '0');
						digits++;
					}
					if (digits)
					{
						*value = v;
						return 1;
					}
				}
			}
		}
		i = j;
	}
	return 0;
}

static int decode_xbm(const unsigned char *data, size_t size,
                      unsigned char **out, unsigned int *width, unsigned int *height)
{
	unsigned long w = 0, h = 0;
	unsigned char *rgb = NULL;
	size_t pos, stride, x, y;
	int err;

	if (!xbm_find_define(data, size, "_width", &w) ||
	    !xbm_find_define(data, size, "_height", &h))
		return SIMPLEIMG_ERR_BITSTREAM;

	/* data starts after the opening brace of the array */
	for (pos = 0; pos < size && data[pos] != '{'; pos++)
		;
	if (pos >= size)
		return SIMPLEIMG_ERR_BITSTREAM;
	pos++;

	err = alloc_rgb((unsigned int)w, (unsigned int)h, &rgb);
	if (err != SIMPLEIMG_OK)
		return err;

	stride = (w + 7) / 8;
	for (y = 0; y < h; y++)
	{
		unsigned char *row = rgb + (size_t)y * w * 3;
		for (x = 0; x < stride; x++)
		{
			unsigned long byte = 0;
			int digits = 0;
			/* skip to the next "0x.." literal */
			while (pos < size && data[pos] != 'x' && data[pos] != 'X' && data[pos] != '}')
				pos++;
			if (pos >= size || data[pos] == '}')
			{
				err = SIMPLEIMG_ERR_BITSTREAM;
				goto fail;
			}
			pos++;
			while (pos < size)
			{
				unsigned char c = data[pos];
				int d;
				if (c >= '0' && c <= '9')
					d = c - '0';
				else if (c >= 'a' && c <= 'f')
					d = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F')
					d = c - 'A' + 10;
				else
					break;
				byte = byte * 16 + (unsigned long)d;
				digits++;
				pos++;
			}
			if (!digits)
			{
				err = SIMPLEIMG_ERR_BITSTREAM;
				goto fail;
			}
			/* XBM packs bits least-significant first. Polarity: a set bit is
			 * *white* here, the opposite of PBM. That is what Pillow does in
			 * both directions, and the sample in test_signals was written by
			 * Pillow - its PBM and XBM renderings of the same picture agree
			 * pixel for pixel, with the two files carrying inverted bits. Be
			 * aware that the X11 reading of the format calls a set bit the
			 * foreground, which some tools render as black; no second XBM
			 * decoder was available here to arbitrate. */
			{
				size_t bit;
				for (bit = 0; bit < 8; bit++)
				{
					size_t px = x * 8 + bit;
					unsigned char v;
					if (px >= w)
						break;
					v = (byte & (1uL << bit)) ? 255 : 0;
					row[px * 3] = row[px * 3 + 1] = row[px * 3 + 2] = v;
				}
			}
		}
	}

	*out = rgb;
	*width = (unsigned int)w;
	*height = (unsigned int)h;
	return SIMPLEIMG_OK;

fail:
	free(rgb);
	return err;
}

/* ------------------------------------------------------------------------- */

int simpleimg_decode(SimpleImgFormat format, const unsigned char *data, size_t size,
                     unsigned char **out, unsigned int *width, unsigned int *height)
{
	if (!data || !out || !width || !height)
		return SIMPLEIMG_ERR_BITSTREAM;

	switch (format)
	{
	case SIMPLEIMG_PCX:
		return decode_pcx(data, size, out, width, height);
	case SIMPLEIMG_TGA:
		return decode_tga(data, size, out, width, height);
	case SIMPLEIMG_SGI:
		return decode_sgi(data, size, out, width, height);
	case SIMPLEIMG_PNM:
		return decode_pnm(data, size, out, width, height);
	case SIMPLEIMG_XBM:
		return decode_xbm(data, size, out, width, height);
	}
	return SIMPLEIMG_ERR_VARIANT;
}
