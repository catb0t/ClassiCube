#include "Bitmap.h"
#include "Platform.h"
#include "PackedCol.h"
#include "ExtMath.h"
#include "Deflate.h"
#include "Logger.h"
#include "Stream.h"
#include "Errors.h"
#include "Utils.h"

BitmapCol BitmapCol_Scale(BitmapCol value, float t) {
	value.R = (uint8_t)(value.R * t);
	value.G = (uint8_t)(value.G * t);
	value.B = (uint8_t)(value.B * t);
	return value;
}

void Bitmap_CopyBlock(uint32_t srcX, uint32_t srcY, uint32_t dstX, uint32_t dstY, Bitmap* src, Bitmap* dst, uint32_t size) {
	uint32_t x, y;
	for (y = 0; y < size; y++) {
		BitmapCol* srcRow = Bitmap_GetRow(src, srcY + y) + srcX;
		BitmapCol* dstRow = Bitmap_GetRow(dst, dstY + y) + dstX;
		for (x = 0; x < size; x++) { dstRow[x] = srcRow[x]; }
	}
}

void Bitmap_Allocate(Bitmap* bmp, uint32_t width, uint32_t height) {
	bmp->Width = width; bmp->Height = height;
	bmp->Scan0 = Mem_Alloc((uint32_t) (width * height), 4, "bitmap data");
}

void Bitmap_AllocateClearedPow2(Bitmap* bmp, uint32_t width, uint32_t height) {
	width  = Math_NextPowOf2U( width);
	height = Math_NextPowOf2U( height);

	bmp->Width = width; bmp->Height = height;
	bmp->Scan0 = Mem_AllocCleared((uint32_t) (width * height), 4, "bitmap data");
}


/*########################################################################################################################*
*------------------------------------------------------PNG decoder--------------------------------------------------------*
*#########################################################################################################################*/
#define PNG_SIG_SIZE 8
#define PNG_IHDR_SIZE 13
#define PNG_RGB_MASK 0xFFFFFFUL
#define PNG_PALETTE 256
#define PNG_FourCC(a, b, c, d) (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d)

enum PngCol {
	PNG_COL_GRAYSCALE = 0, PNG_COL_RGB = 2, PNG_COL_INDEXED = 3,
	PNG_COL_GRAYSCALE_A = 4, PNG_COL_RGB_A = 6
};

enum PngFilter {
	PNG_FILTER_NONE, PNG_FILTER_SUB, PNG_FILTER_UP, PNG_FILTER_AVERAGE, PNG_FILTER_PAETH
};

static uint8_t png_sig[PNG_SIG_SIZE] = { 137, 80, 78, 71, 13, 10, 26, 10 };

bool Png_Detect(const uint8_t* data, uint32_t len) {
	uint32_t i;
	if (len < PNG_SIG_SIZE) return false;

	for (i = 0; i < PNG_SIG_SIZE; i++) {
		if (data[i] != png_sig[i]) return false;
	}
	return true;
}

static void Png_Reconstruct(uint8_t type, uint8_t bytesPerPixel, uint8_t* line, uint8_t* prior, uint32_t lineLen) {
	uint32_t i, j;
	switch (type) {
	case PNG_FILTER_NONE:
		return;

	case PNG_FILTER_SUB:
		for (i = bytesPerPixel, j = 0; i < lineLen; i++, j++) {
			line[i] = (uint8_t) (line[i] + line[j]);
		}
		return;

	case PNG_FILTER_UP:
		for (i = 0; i < lineLen; i++) {
			line[i] = (uint8_t) (line[i] + prior[i]);
		}
		return;

	case PNG_FILTER_AVERAGE:
		for (i = 0; i < bytesPerPixel; i++) {
			line[i] = (uint8_t) (line[i] + (prior[i] >> 1));
		}
		for (j = 0; i < lineLen; i++, j++) {
			line[i] = (uint8_t) (line[i] + ((prior[i] + line[j]) >> 1));
		}
		return;

	case PNG_FILTER_PAETH:
		/* TODO: verify this is right */
		for (i = 0; i < bytesPerPixel; i++) {
			line[i] = (uint8_t) (line[i] + prior[i]);
		}
		for (j = 0; i < lineLen; i++, j++) {
			uint8_t a = line[j], b = prior[i], c = prior[j];
			uint32_t p = (uint32_t) (((uint32_t) (a + b)) - c);
			uint32_t pa = Math_AbsU(p - a);
			uint32_t pb = Math_AbsU(p - b);
			uint32_t pc = Math_AbsU(p - c);

			if (pa <= pb && pa <= pc) { line[i] = (uint8_t) (line[i] + a); }
			else if (pb <= pc) {        line[i] = (uint8_t) (line[i] + b); }
			else {                      line[i] = (uint8_t) (line[i] + c); }
		}
		return;

	default:
		Logger_Abort("PNG scanline has invalid filter type");
		return;
	}
}

#define Bitmap_Set(dst, r,g,b,a) dst.B = b; dst.G = g; dst.R = r; dst.A = a;

#define PNG_Do_Grayscale(dstI, src, scale)  rgb = (uint8_t) ((src) * scale); Bitmap_Set(dst[dstI], rgb, rgb, rgb, 255);
#define PNG_Do_Grayscale_8(dstI, srcI)      rgb = (uint8_t) (src[srcI]);     Bitmap_Set(dst[dstI], rgb, rgb, rgb, 255);
#define PNG_Do_Grayscale_A__8(dstI, srcI)   rgb = (uint8_t) (src[srcI]);     Bitmap_Set(dst[dstI], rgb, rgb, rgb, src[srcI + 1]);
#define PNG_Do_RGB__8(dstI, srcI)           Bitmap_Set(dst[dstI], src[srcI], src[srcI + 1], src[srcI + 2], 255);
#define PNG_Do_RGB_A__8(dstI, srcI)         Bitmap_Set(dst[dstI], src[srcI], src[srcI + 1], src[srcI + 2], src[srcI + 3]);

#define PNG_Mask_1(i) (7 - (i & 7))
#define PNG_Mask_2(i) ((3 - (i & 3)) * 2)
#define PNG_Mask_4(i) ((1 - (i & 1)) * 4)
#define PNG_Get__1(i) ((src[i >> 3] >> PNG_Mask_1(i)) & 1)
#define PNG_Get__2(i) ((src[i >> 2] >> PNG_Mask_2(i)) & 3)

static void Png_Expand_GRAYSCALE_1(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i; uint8_t rgb; /* NOTE: not optimised*/
	for (i = 0; i < width; i++) { PNG_Do_Grayscale(i, PNG_Get__1(i), 255); }
}

static void Png_Expand_GRAYSCALE_2(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i; uint8_t rgb; /* NOTE: not optimised */
	for (i = 0; i < width; i++) { PNG_Do_Grayscale(i, PNG_Get__2(i), 85); }
}

static void Png_Expand_GRAYSCALE_4(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i, j; uint8_t rgb;

	for (i = 0, j = 0; i < (width & ~0x1U); i += 2, j++) {
		PNG_Do_Grayscale(i, src[j] >> 4, 17); PNG_Do_Grayscale(i + 1, src[j] & 0x0F, 17);
	}
	for (; i < width; i++) {
		PNG_Do_Grayscale(i, (src[j] >> PNG_Mask_4(i)) & 15, 17);
	}
}

static void Png_Expand_GRAYSCALE_8(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i; uint8_t rgb;

	for (i = 0; i < (width & ~0x3U); i += 4) {
		PNG_Do_Grayscale_8(i    , i    ); PNG_Do_Grayscale_8(i + 1, i + 1);
		PNG_Do_Grayscale_8(i + 2, i + 2); PNG_Do_Grayscale_8(i + 3, i + 3);
	}
	for (; i < width; i++) { PNG_Do_Grayscale_8(i, i); }
}

static void Png_Expand_GRAYSCALE_16(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i; uint8_t rgb; /* NOTE: not optimised */
	for (i = 0; i < width; i++) {
		rgb = src[i * 2]; Bitmap_Set(dst[i], rgb, rgb, rgb, 255);
	}
}

static void Png_Expand_RGB_8(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i, j;

	for (i = 0, j = 0; i < (width & ~0x03U); i += 4, j += 12) {
		PNG_Do_RGB__8(i    , j    ); PNG_Do_RGB__8(i + 1, j + 3);
		PNG_Do_RGB__8(i + 2, j + 6); PNG_Do_RGB__8(i + 3, j + 9);
	}
	for (; i < width; i++, j += 3) { PNG_Do_RGB__8(i, j); }
}

static void Png_Expand_RGB_16(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i, j; /* NOTE: not optimised */
	for (i = 0, j = 0; i < width; i++, j += 6) {
		Bitmap_Set(dst[i], src[j], src[j + 2], src[j + 4], 255);
	}
}

static void Png_Expand_INDEXED_1(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
	uint32_t i; /* NOTE: not optimised */
	for (i = 0; i < width; i++) { dst[i] = palette[PNG_Get__1(i)]; }
}

static void Png_Expand_INDEXED_2(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
	uint32_t i; /* NOTE: not optimised */
	for (i = 0; i < width; i++) { dst[i] = palette[PNG_Get__2(i)]; }
}

static void Png_Expand_INDEXED_4(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
	(void) palette;
  uint32_t i, j; uint8_t cur;

	for (i = 0, j = 0; i < (width & ~0x1U); i += 2, j++) {
		cur = src[j];
		dst[i] = palette[cur >> 4]; dst[i + 1] = palette[cur & 0x0F];
	}
	for (; i < width; i++) {
		dst[i] = palette[(src[j] >> PNG_Mask_4(i)) & 15];
	}
}

static void Png_Expand_INDEXED_8(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
	(void) palette;
  uint32_t i;

	for (i = 0; i < (width & ~0x3U); i += 4) {
		dst[i]     = palette[src[i]];     dst[i + 1] = palette[src[i + 1]];
		dst[i + 2] = palette[src[i + 2]]; dst[i + 3] = palette[src[i + 3]];
	}
	for (; i < width; i++) { dst[i] = palette[src[i]]; }
}

static void Png_Expand_GRAYSCALE_A_8(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i, j; uint8_t rgb;

	for (i = 0, j = 0; i < (width & ~0x3U); i += 4, j += 8) {
		PNG_Do_Grayscale_A__8(i    , j    ); PNG_Do_Grayscale_A__8(i + 1, j + 2);
		PNG_Do_Grayscale_A__8(i + 2, j + 4); PNG_Do_Grayscale_A__8(i + 3, j + 6);
	}
	for (; i < width; i++, j += 2) { PNG_Do_Grayscale_A__8(i, j); }
}

static void Png_Expand_GRAYSCALE_A_16(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
	uint32_t i; uint8_t rgb; /* NOTE: not optimised*/
	for (i = 0; i < width; i++) {
		rgb = src[i * 4]; Bitmap_Set(dst[i], rgb, rgb, rgb, src[i * 4 + 2]);
	}
}

static void Png_Expand_RGB_A_8(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i, j;

	for (i = 0, j = 0; i < (width & ~0x3U); i += 4, j += 16) {
		PNG_Do_RGB_A__8(i    , j    ); PNG_Do_RGB_A__8(i + 1, j + 4 );
		PNG_Do_RGB_A__8(i + 2, j + 8); PNG_Do_RGB_A__8(i + 3, j + 12);
	}
	for (; i < width; i++, j += 4) { PNG_Do_RGB_A__8(i, j); }
}

static void Png_Expand_RGB_A_16(uint32_t width, BitmapCol* palette, uint8_t* src, BitmapCol* dst) {
  (void) palette;
  uint32_t i, j; /* NOTE: not optimised*/
	for (i = 0, j = 0; i < width; i++, j += 8) {
		Bitmap_Set(dst[i], src[j], src[j + 2], src[j + 4], src[j + 6]);
	}
}

Png_RowExpander Png_GetExpander(uint8_t col, uint8_t bitsPerSample) {
	switch (col) {
	case PNG_COL_GRAYSCALE:
		switch (bitsPerSample) {
		case 1:  return Png_Expand_GRAYSCALE_1;
		case 2:  return Png_Expand_GRAYSCALE_2;
		case 4:  return Png_Expand_GRAYSCALE_4;
		case 8:  return Png_Expand_GRAYSCALE_8;
		case 16: return Png_Expand_GRAYSCALE_16;
		}
		return NULL;

	case PNG_COL_RGB:
		switch (bitsPerSample) {
		case 8:  return Png_Expand_RGB_8;
		case 16: return Png_Expand_RGB_16;
		}
		return NULL;

	case PNG_COL_INDEXED:
		switch (bitsPerSample) {
		case 1: return Png_Expand_INDEXED_1;
		case 2: return Png_Expand_INDEXED_2;
		case 4: return Png_Expand_INDEXED_4;
		case 8: return Png_Expand_INDEXED_8;
		}
		return NULL;

	case PNG_COL_GRAYSCALE_A:
		switch (bitsPerSample) {
		case 8:  return Png_Expand_GRAYSCALE_A_8;
		case 16: return Png_Expand_GRAYSCALE_A_16;
		}
		return NULL;

	case PNG_COL_RGB_A:
		switch (bitsPerSample) {
		case 8:  return Png_Expand_RGB_A_8;
		case 16: return Png_Expand_RGB_A_16;
		}
		return NULL;
	}
	return NULL;
}

static void Png_ComputeTransparency(Bitmap* bmp, BitmapCol col) {
  uint32_t trnsRGB = (uint32_t) (col.B | (col.G << 8) | (col.R << 16)); /* TODO: Remove this!! */
	uint32_t x, y, width = bmp->Width, height = bmp->Height;

	for (y = 0; y < height; y++) {
		uint32_t* row = Bitmap_RawRow(bmp, y);
		for (x = 0; x < width; x++) {
			uint32_t rgb = row[x] & PNG_RGB_MASK;
			row[x] = (rgb == trnsRGB) ? trnsRGB : row[x];
		}
	}
}

/* Most bits per sample is 16. Most samples per pixel is 4. Add 1 for filter byte. */
/* Need to store both current and prior row, per PNG specification. */
#define PNG_BUFFER_SIZE ((PNG_MAX_DIMS * 2 * 4 + 1) * 2)

/* TODO: Test a lot of .png files and ensure output is right */
ReturnCode Png_Decode(Bitmap* bmp, struct Stream* stream) {
	uint8_t tmp[PNG_PALETTE * 3];
	uint32_t dataSize, fourCC;
	ReturnCode res;

	/* header variables */
	static uint32_t samplesPerPixel[7] = { 1, 0, 3, 1, 2, 0, 4 };
	uint8_t col, bitsPerSample, bytesPerPixel;
	Png_RowExpander rowExpander;
	uint32_t scanlineSize, scanlineBytes;

	/* palette data */
	BitmapCol black = BITMAPCOL_CONST(0, 0, 0, 255);
	BitmapCol transparentCol;
	BitmapCol palette[PNG_PALETTE];
	uint32_t i;

	/* idat state */
	uint32_t curY = 0, begY, rowY, endY;
	uint8_t buffer[PNG_BUFFER_SIZE];
	uint32_t bufferRows, bufferLen;
	uint32_t bufferIdx, read, left;

	/* idat decompressor */
	struct InflateState inflate;
	struct Stream compStream, datStream;
	struct ZLibHeader zlibHeader;

	bmp->Width = 0; bmp->Height = 0;
	bmp->Scan0 = NULL;

	res = Stream_Read(stream, tmp, PNG_SIG_SIZE);
	if (res) return res;
	if (!Png_Detect(tmp, PNG_SIG_SIZE)) return PNG_ERR_INVALID_SIG;

	transparentCol = black;
	for (i = 0; i < PNG_PALETTE; i++) { palette[i] = black; }

	Inflate_MakeStream(&compStream, &inflate, stream);
	ZLibHeader_Init(&zlibHeader);

	for (;;) {
		res = Stream_Read(stream, tmp, 8);
		if (res) return res;
		dataSize = Stream_GetU32_BE(&tmp[0]);
		fourCC   = Stream_GetU32_BE(&tmp[4]);

		switch (fourCC) {
		case PNG_FourCC('I','H','D','R'): {
			if (dataSize != PNG_IHDR_SIZE) return PNG_ERR_INVALID_HDR_SIZE;
			res = Stream_Read(stream, tmp, PNG_IHDR_SIZE);
			if (res) return res;

			bmp->Width  = Stream_GetU32_BE(&tmp[0]);
			bmp->Height = Stream_GetU32_BE(&tmp[4]);
			if (bmp->Width  > PNG_MAX_DIMS) return PNG_ERR_TOO_WIDE;
			if (bmp->Height > PNG_MAX_DIMS) return PNG_ERR_TOO_TALL;

			bmp->Scan0 = Mem_Alloc(bmp->Width * bmp->Height, 4, "PNG bitmap data");
			bitsPerSample = tmp[8]; col = tmp[9];
			rowExpander = Png_GetExpander(col, bitsPerSample);
			if (rowExpander == NULL) return PNG_ERR_INVALID_COL_BPP;

			if (tmp[10] != 0) return PNG_ERR_COMP_METHOD;
			if (tmp[11] != 0) return PNG_ERR_FILTER;
			if (tmp[12] != 0) return PNG_ERR_INTERLACED;

			bytesPerPixel = (uint8_t) ((samplesPerPixel[col] * bitsPerSample) + 7) >> 3;
			scanlineSize  = ((samplesPerPixel[col] * bitsPerSample * bmp->Width) + 7) >> 3;
			scanlineBytes = scanlineSize + 1; /* Add 1 byte for filter byte of each scanline */

			Mem_Set(buffer, 0, scanlineBytes); /* Prior row should be 0 per PNG spec */
			bufferIdx  = scanlineBytes;
			bufferRows = PNG_BUFFER_SIZE / scanlineBytes;
			bufferLen  = bufferRows * scanlineBytes;
		} break;

		case PNG_FourCC('P','L','T','E'): {
			if (dataSize > PNG_PALETTE * 3) return PNG_ERR_PAL_SIZE;
			if ((dataSize % 3) != 0)        return PNG_ERR_PAL_SIZE;
			res = Stream_Read(stream, tmp, dataSize);
			if (res) return res;

			for (i = 0; i < dataSize; i += 3) {
				palette[i / 3].R = tmp[i];
				palette[i / 3].G = tmp[i + 1];
				palette[i / 3].B = tmp[i + 2];
			}
		} break;

		case PNG_FourCC('t','R','N','S'): {
			if (col == PNG_COL_GRAYSCALE) {
				if (dataSize != 2) return PNG_ERR_TRANS_COUNT;
				res = Stream_Read(stream, tmp, dataSize);
				if (res) return res;

				/* RGB is 16 bits big endian, ignore least significant 8 bits */
				transparentCol.R = tmp[0]; transparentCol.G = tmp[0];
				transparentCol.B = tmp[0]; transparentCol.A = 0;
			} else if (col == PNG_COL_INDEXED) {
				if (dataSize > PNG_PALETTE) return PNG_ERR_TRANS_COUNT;
				res = Stream_Read(stream, tmp, dataSize);
				if (res) return res;

				/* set alpha component of palette */
				for (i = 0; i < dataSize; i++) {
					palette[i].A = tmp[i];
				}
			} else if (col == PNG_COL_RGB) {
				if (dataSize != 6) return PNG_ERR_TRANS_COUNT;
				res = Stream_Read(stream, tmp, dataSize);
				if (res) return res;

				/* R,G,B is 16 bits big endian, ignore least significant 8 bits */
				transparentCol.R = tmp[0]; transparentCol.G = tmp[2];
				transparentCol.B = tmp[4]; transparentCol.A = 0;
			} else {
				return PNG_ERR_TRANS_INVALID;
			}
		} break;

		case PNG_FourCC('I','D','A','T'): {
			Stream_ReadonlyPortion(&datStream, stream, dataSize);
			inflate.Source = &datStream;

			/* TODO: This assumes zlib header will be in 1 IDAT chunk */
			while (!zlibHeader.Done) {
				if ((res = ZLibHeader_Read(&datStream, &zlibHeader))) return res;
			}
			if (!bmp->Scan0) return PNG_ERR_NO_DATA;

			while (curY < bmp->Height) {
				/* Need to leave one row in buffer untouched for storing prior scanline. Illustrated example of process:
				*          |=====|        #-----|        |-----|        #-----|        |-----|
				* initial  #-----| read 3 |-----| read 3 |-----| read 1 |-----| read 3 |-----| etc
				*  state   |-----| -----> |-----| -----> |=====| -----> |-----| -----> |=====|
				*          |-----|        |=====|        #-----|        |=====|        #-----|
				*
				* (==== is prior scanline, # is current index in buffer)
				* Having initial state this way allows doing two 'read 3' first time (assuming large idat chunks)
				*/

				begY = bufferIdx / scanlineBytes;
				left = bufferLen - bufferIdx;
				/* if row is at 0, last row in buffer is prior row */
				/* hence subtract a row, as don't want to overwrite it */
				if (begY == 0) left -= scanlineBytes;

				res = compStream.Read(&compStream, &buffer[bufferIdx], left, &read);
				if (res) return res;
				if (!read) break;

				bufferIdx += read;
				endY = bufferIdx / scanlineBytes;
				/* reached end of buffer, cycle back to start */
				if (bufferIdx == bufferLen) bufferIdx = 0;

				for (rowY = begY; rowY < endY; rowY++, curY++) {
					uint32_t priorY = rowY == 0 ? bufferRows : rowY;
					uint8_t* prior    = &buffer[(priorY - 1) * scanlineBytes];
					uint8_t* scanline = &buffer[rowY         * scanlineBytes];

					Png_Reconstruct(scanline[0], bytesPerPixel, &scanline[1], &prior[1], scanlineSize);
					rowExpander(bmp->Width, palette, &scanline[1], Bitmap_GetRow(bmp, curY));
				}
			}
		} break;

		case PNG_FourCC('I','E','N','D'): {
			if (dataSize) return PNG_ERR_INVALID_END_SIZE;
			if (!transparentCol.A) Png_ComputeTransparency(bmp, transparentCol);
			return bmp->Scan0 ? 0 : PNG_ERR_NO_DATA;
		} break;

		default:
			if ((res = stream->Skip(stream, dataSize))) return res;
			break;
		}

		if ((res = stream->Skip(stream, 4))) return res; /* Skip CRC32 */
	}
}


/*########################################################################################################################*
*------------------------------------------------------PNG encoder--------------------------------------------------------*
*#########################################################################################################################*/
static void Png_Filter(uint8_t filter, const uint8_t* cur, const uint8_t* prior, uint8_t* best, uint32_t lineLen, uint32_t bpp) {
	/* 3 bytes per pixel constant */
	uint8_t a, b, c;
	uint32_t i, p, pa, pb, pc;

	switch (filter) {
	case PNG_FILTER_SUB:
		for (i = 0; i < bpp; i++) { best[i] = cur[i]; }

		for (; i < lineLen; i++) {
			best[i] = (uint8_t) (cur[i] - cur[i - bpp]);
		}
		break;

	case PNG_FILTER_UP:
		for (i = 0; i < lineLen; i++) {
			best[i] = (uint8_t) (cur[i] - prior[i]);
		}
		break;

	case PNG_FILTER_AVERAGE:
		for (i = 0; i < bpp; i++) { best[i] = (uint8_t) (cur[i] - (prior[i] >> 1)); }

		for (; i < lineLen; i++) {
			best[i] = (uint8_t) (cur[i] - ((prior[i] + cur[i - bpp]) >> 1));
		}
		break;

	case PNG_FILTER_PAETH:
		for (i = 0; i < bpp; i++) { best[i] = (uint8_t) (cur[i] - prior[i]); }

		for (; i < lineLen; i++) {
			a = cur[i - bpp]; b = prior[i]; c = prior[i - bpp];
			p = (uint32_t) ((uint32_t) (a + b) - c);

			pa = Math_AbsU(p - a);
			pb = Math_AbsU(p - b);
			pc = Math_AbsU(p - c);

			if (pa <= pb && pa <= pc) { best[i] = (uint8_t) (cur[i] - a); }
			else if (pb <= pc)        { best[i] = (uint8_t) (cur[i] - b); }
			else                      { best[i] = (uint8_t) (cur[i] - c); }
		}
		break;
	}
}

static void Png_MakeRow(const BitmapCol* src, uint8_t* dst, uint32_t lineLen, bool alpha) {
	uint8_t* end = dst + lineLen;

	if (alpha) {
		for (; dst < end; src++, dst += 4) {
			dst[0] = src->R; dst[1] = src->G; dst[2] = src->B; dst[3] = src->A;
		}
	} else {
		for (; dst < end; src++, dst += 3) {
			dst[0] = src->R; dst[1] = src->G; dst[2] = src->B;
		}
	}
}

static void Png_EncodeRow(const uint8_t* cur, const uint8_t* prior, uint8_t* best, uint32_t lineLen, bool alpha) {
	uint8_t* dst;
	uint32_t x, estimate, bestEstimate = Int32_MaxValue;
  uint8_t bestFilter, filter;

	dst = best + 1;
	/* NOTE: Waste of time trying the PNG_NONE filter */
	for (filter = PNG_FILTER_SUB; filter <= PNG_FILTER_PAETH; filter++) {
		Png_Filter(filter, cur, prior, dst, lineLen, alpha ? 4 : 3);

		/* Estimate how well this filtered line will compress, based on */
		/* smallest sum of magnitude of each byte (signed) in the line */
		/* (see note in PNG specification, 12.8 "Filter selection" ) */
		estimate = 0;
		for (x = 0; x < lineLen; x++) {
			estimate += dst[x];
		}

		if (estimate > bestEstimate) continue;
		bestEstimate = estimate;
		bestFilter   = filter;
	}

	/* The bytes in dst are from last filter run (paeth) */
	/* However, we want dst to be bytes from the best filter */
	if (bestFilter != PNG_FILTER_PAETH) {
		Png_Filter(bestFilter, cur, prior, dst, lineLen, alpha ? 4 : 3);
	}

	best[0] = bestFilter;
}

static uint32_t Png_SelectRow(Bitmap* bmp, uint32_t y) { (void) bmp; return y; }

ReturnCode Png_Encode(Bitmap* bmp, struct Stream* stream, Png_RowSelector selectRow, bool alpha) {
	uint8_t tmp[32];
	/* TODO: This should be * 4 for alpha (should switch to mem_alloc though) */
	uint8_t prevLine[PNG_MAX_DIMS * 3], curLine[PNG_MAX_DIMS * 3];
	uint8_t bestLine[PNG_MAX_DIMS * 3 + 1];

	struct ZLibState zlState;
	struct Stream chunk, zlStream;
	uint32_t stream_end, stream_beg;
	uint32_t y, lineSize;
	ReturnCode res;

	/* stream may not start at 0 (e.g. when making default.zip) */
	if ((res = stream->Position(stream, &stream_beg))) return res;

	if (!selectRow) selectRow = Png_SelectRow;
	if ((res = Stream_Write(stream, png_sig, PNG_SIG_SIZE))) return res;
	Stream_WriteonlyCrc32(&chunk, stream);

	/* Write header chunk */
	Stream_SetU32_BE(&tmp[0], PNG_IHDR_SIZE);
	Stream_SetU32_BE(&tmp[4], PNG_FourCC('I','H','D','R'));
	{
		Stream_SetU32_BE(&tmp[8],  bmp->Width);
		Stream_SetU32_BE(&tmp[12], bmp->Height);
		tmp[16] = 8;           /* bits per sample */
		tmp[17] = alpha ? PNG_COL_RGB_A : PNG_COL_RGB;
		tmp[18] = 0;           /* DEFLATE compression method */
		tmp[19] = 0;           /* ADAPTIVE filter method */
		tmp[20] = 0;           /* Not using interlacing */
	}
	Stream_SetU32_BE(&tmp[21], Utils_CRC32(&tmp[4], 17));

	/* Write PNG body */
	Stream_SetU32_BE(&tmp[25], 0); /* size of IDAT, filled in later */
	if ((res = Stream_Write(stream, tmp, 29))) return res;
	Stream_SetU32_BE(&tmp[0], PNG_FourCC('I','D','A','T'));
	if ((res = Stream_Write(&chunk, tmp, 4))) return res;

	ZLib_MakeStream(&zlStream, &zlState, &chunk);
	lineSize = bmp->Width * (alpha ? 4 : 3);
	Mem_Set(prevLine, 0, lineSize);

	for (y = 0; y < bmp->Height; y++) {
		uint32_t row = selectRow(bmp, y);
		BitmapCol* src = Bitmap_GetRow(bmp, row);
		uint8_t* prev  = (y & 1) == 0 ? prevLine : curLine;
		uint8_t* cur   = (y & 1) == 0 ? curLine  : prevLine;

		Png_MakeRow(src, cur, lineSize, alpha);
		Png_EncodeRow(cur, prev, bestLine, lineSize, alpha);

		/* +1 for filter byte */
		if ((res = Stream_Write(&zlStream, bestLine, lineSize + 1))) return res;
	}
	if ((res = zlStream.Close(&zlStream))) return res;
	Stream_SetU32_BE(&tmp[0], chunk.Meta.CRC32.CRC32 ^ 0xFFFFFFFFUL);

	/* Write end chunk */
	Stream_SetU32_BE(&tmp[4],  0);
	Stream_SetU32_BE(&tmp[8],  PNG_FourCC('I','E','N','D'));
	Stream_SetU32_BE(&tmp[12], 0xAE426082UL); /* CRC32 of IEND */
	if ((res = Stream_Write(stream, tmp, 16))) return res;

	/* Come back to fixup size of data in data chunk */
	if ((res = stream->Length(stream, &stream_end)))   return res;
	if ((res = stream->Seek(stream, stream_beg + 33))) return res;

	Stream_SetU32_BE(&tmp[0], (stream_end - stream_beg) - 57);
	if ((res = Stream_Write(stream, tmp, 4))) return res;
	return stream->Seek(stream, stream_end);
}
