/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// d3dx8tex.cpp - texture, surface and shader helpers of the D3DX8 replacement.
//
// Surfaces are converted through a 32-bit ARGB intermediate image.  Supported formats:
// A8R8G8B8, X8R8G8B8, R8G8B8, R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, X4R4G4B4, R3G3B2, A8,
// L8, A8L8, A4L4, P8 (with palette) and DXT1-5 (decode and encode).

#include "d3dx8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

typedef unsigned int TexPixel;	// 0xAARRGGBB

// ------------------------------------------------------------------------------------------------
// TexPixel formats
// ------------------------------------------------------------------------------------------------

static bool IsDXT(D3DFORMAT f)
{
	return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}

static UINT BytesPerPixel(D3DFORMAT f)
{
	switch (f)
	{
		case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
		case D3DFMT_R8G8B8: return 3;
		case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
		case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: case D3DFMT_A8R3G3B2: return 2;
		case D3DFMT_R3G3B2: case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_A4L4: case D3DFMT_P8: return 1;
		default: return 0;
	}
}

static inline TexPixel Expand(unsigned v, int bits)
{
	// replicate the high bits so 0 -> 0 and max -> 255
	unsigned x = v << (8 - bits);
	return x | (x >> bits);
}

static TexPixel DecodePixel(const unsigned char *p, D3DFORMAT f, const PALETTEENTRY *pal)
{
	unsigned v;
	switch (f)
	{
		case D3DFMT_A8R8G8B8: return p[0] | (p[1] << 8) | (p[2] << 16) | ((TexPixel)p[3] << 24);
		case D3DFMT_X8R8G8B8: return p[0] | (p[1] << 8) | (p[2] << 16) | 0xFF000000u;
		case D3DFMT_R8G8B8:   return p[0] | (p[1] << 8) | (p[2] << 16) | 0xFF000000u;
		case D3DFMT_R5G6B5:
			v = p[0] | (p[1] << 8);
			return 0xFF000000u | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
		case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
			v = p[0] | (p[1] << 8);
			return ((f == D3DFMT_X1R5G5B5 || (v & 0x8000)) ? 0xFF000000u : 0) |
				(Expand((v >> 10) & 31, 5) << 16) | (Expand((v >> 5) & 31, 5) << 8) | Expand(v & 31, 5);
		case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
			v = p[0] | (p[1] << 8);
			return ((f == D3DFMT_X4R4G4B4) ? 0xFF000000u : (Expand(v >> 12, 4) << 24)) |
				(Expand((v >> 8) & 15, 4) << 16) | (Expand((v >> 4) & 15, 4) << 8) | Expand(v & 15, 4);
		case D3DFMT_R3G3B2:
			v = p[0];
			return 0xFF000000u | (Expand(v >> 5, 3) << 16) | (Expand((v >> 2) & 7, 3) << 8) | Expand(v & 3, 2);
		case D3DFMT_A8R3G3B2:
			v = p[0] | (p[1] << 8);
			return ((TexPixel)(v >> 8) << 24) | (Expand((v >> 5) & 7, 3) << 16) | (Expand((v >> 2) & 7, 3) << 8) | Expand(v & 3, 2);
		case D3DFMT_A8:  return (TexPixel)p[0] << 24;
		case D3DFMT_L8:  return 0xFF000000u | (p[0] << 16) | (p[0] << 8) | p[0];
		case D3DFMT_A8L8: return ((TexPixel)p[1] << 24) | (p[0] << 16) | (p[0] << 8) | p[0];
		case D3DFMT_A4L4:
			v = Expand(p[0] & 15, 4);
			return (Expand(p[0] >> 4, 4) << 24) | (v << 16) | (v << 8) | v;
		case D3DFMT_P8:
			if (pal) {
				const PALETTEENTRY &e = pal[p[0]];
				return ((TexPixel)e.peFlags << 24) | (e.peRed << 16) | (e.peGreen << 8) | e.peBlue;
			}
			return 0xFF000000u | (p[0] << 16) | (p[0] << 8) | p[0];
		default: return 0;
	}
}

static void EncodePixel(unsigned char *p, D3DFORMAT f, TexPixel c)
{
	unsigned a = c >> 24, r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
	unsigned v;
	switch (f)
	{
		case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
			p[0] = (unsigned char)b; p[1] = (unsigned char)g; p[2] = (unsigned char)r;
			p[3] = (unsigned char)(f == D3DFMT_X8R8G8B8 ? 255 : a); break;
		case D3DFMT_R8G8B8: p[0] = (unsigned char)b; p[1] = (unsigned char)g; p[2] = (unsigned char)r; break;
		case D3DFMT_R5G6B5: v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); break;
		case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
			v = ((f == D3DFMT_X1R5G5B5 || a >= 128) ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
			p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); break;
		case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
			v = ((f == D3DFMT_X4R4G4B4 ? 15 : (a >> 4)) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
			p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); break;
		case D3DFMT_R3G3B2: p[0] = (unsigned char)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)); break;
		case D3DFMT_A8R3G3B2: v = (a << 8) | ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6); p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); break;
		case D3DFMT_A8: p[0] = (unsigned char)a; break;
		case D3DFMT_L8: p[0] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8); break;
		case D3DFMT_A8L8: p[0] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8); p[1] = (unsigned char)a; break;
		case D3DFMT_A4L4: p[0] = (unsigned char)(((a >> 4) << 4) | (((r * 77 + g * 150 + b * 29) >> 8) >> 4)); break;
		default: break;
	}
}

// ---- DXT ---------------------------------------------------------------------------------------

static TexPixel From565(unsigned v)
{
	return 0xFF000000u | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
}

static unsigned To565(TexPixel c)
{
	return ((((c >> 16) & 255) >> 3) << 11) | ((((c >> 8) & 255) >> 2) << 5) | ((c & 255) >> 3);
}

static TexPixel Mix(TexPixel a, TexPixel b, int wa, int wb, int div)
{
	TexPixel r = 0;
	for (int s = 0; s < 24; s += 8)
		r |= ((((a >> s) & 255) * wa + ((b >> s) & 255) * wb) / div) << s;
	return r | 0xFF000000u;
}

static void DecodeColorBlock(const unsigned char *blk, TexPixel out[16], bool dxt1)
{
	unsigned c0 = blk[0] | (blk[1] << 8), c1 = blk[2] | (blk[3] << 8);
	TexPixel pal[4];
	pal[0] = From565(c0);
	pal[1] = From565(c1);
	if (!dxt1 || c0 > c1) {
		pal[2] = Mix(pal[0], pal[1], 2, 1, 3);
		pal[3] = Mix(pal[0], pal[1], 1, 2, 3);
	} else {
		pal[2] = Mix(pal[0], pal[1], 1, 1, 2);
		pal[3] = 0;	// transparent black
	}
	unsigned bits = blk[4] | (blk[5] << 8) | (blk[6] << 16) | ((unsigned)blk[7] << 24);
	for (int i = 0; i < 16; ++i)
		out[i] = pal[(bits >> (i * 2)) & 3];
}

static void DecodeDXTBlock(const unsigned char *blk, D3DFORMAT f, TexPixel out[16])
{
	if (f == D3DFMT_DXT1) {
		DecodeColorBlock(blk, out, true);
		return;
	}
	DecodeColorBlock(blk + 8, out, false);
	if (f == D3DFMT_DXT2 || f == D3DFMT_DXT3) {
		for (int i = 0; i < 16; ++i) {
			unsigned a = (blk[i / 2] >> ((i & 1) * 4)) & 15;
			out[i] = (out[i] & 0x00FFFFFFu) | (Expand(a, 4) << 24);
		}
	} else {
		unsigned a0 = blk[0], a1 = blk[1];
		unsigned pal[8];
		pal[0] = a0; pal[1] = a1;
		if (a0 > a1) {
			for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
		} else {
			for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;
			pal[6] = 0; pal[7] = 255;
		}
		unsigned long long bits = 0;
		for (int i = 0; i < 6; ++i) bits |= (unsigned long long)blk[2 + i] << (8 * i);
		for (int i = 0; i < 16; ++i)
			out[i] = (out[i] & 0x00FFFFFFu) | ((TexPixel)pal[(bits >> (3 * i)) & 7] << 24);
	}
}

static int ColorDist(TexPixel a, TexPixel b)
{
	int dr = (int)((a >> 16) & 255) - (int)((b >> 16) & 255);
	int dg = (int)((a >> 8) & 255) - (int)((b >> 8) & 255);
	int db = (int)(a & 255) - (int)(b & 255);
	return dr * dr * 2 + dg * dg * 4 + db * db;
}

static void EncodeColorBlock(const TexPixel in[16], unsigned char *blk, bool dxt1)
{
	// Endpoints: the two pixels farthest apart along the luminance axis (simple and robust).
	bool transparent = false;
	int minL = 1 << 30, maxL = -1;
	TexPixel lo = 0, hi = 0;
	for (int i = 0; i < 16; ++i) {
		if (dxt1 && (in[i] >> 24) < 128) { transparent = true; continue; }
		int l = (int)((in[i] >> 16) & 255) * 2 + (int)((in[i] >> 8) & 255) * 4 + (int)(in[i] & 255);
		if (l < minL) { minL = l; lo = in[i]; }
		if (l > maxL) { maxL = l; hi = in[i]; }
	}
	unsigned c0 = To565(hi), c1 = To565(lo);
	TexPixel pal[4];
	if (dxt1 && transparent) {
		if (c0 > c1) { unsigned t = c0; c0 = c1; c1 = t; }		// c0 <= c1 selects the 3 colour mode
		pal[0] = From565(c0); pal[1] = From565(c1); pal[2] = Mix(pal[0], pal[1], 1, 1, 2); pal[3] = 0;
	} else {
		if (c0 < c1) { unsigned t = c0; c0 = c1; c1 = t; }
		if (c0 == c1 && dxt1) {
			// 4 colour mode requires c0 > c1; a flat block decodes the same either way
		}
		pal[0] = From565(c0); pal[1] = From565(c1);
		pal[2] = Mix(pal[0], pal[1], 2, 1, 3); pal[3] = Mix(pal[0], pal[1], 1, 2, 3);
	}
	unsigned bits = 0;
	for (int i = 0; i < 16; ++i) {
		int best = 0;
		if (dxt1 && transparent && (in[i] >> 24) < 128) {
			best = 3;
		} else {
			int bestD = 1 << 30;
			int n = (dxt1 && transparent) ? 3 : 4;
			for (int k = 0; k < n; ++k) {
				int d = ColorDist(in[i], pal[k]);
				if (d < bestD) { bestD = d; best = k; }
			}
		}
		bits |= (unsigned)best << (i * 2);
	}
	blk[0] = (unsigned char)c0; blk[1] = (unsigned char)(c0 >> 8);
	blk[2] = (unsigned char)c1; blk[3] = (unsigned char)(c1 >> 8);
	blk[4] = (unsigned char)bits; blk[5] = (unsigned char)(bits >> 8);
	blk[6] = (unsigned char)(bits >> 16); blk[7] = (unsigned char)(bits >> 24);
}

static void EncodeDXTBlock(const TexPixel in[16], D3DFORMAT f, unsigned char *blk)
{
	if (f == D3DFMT_DXT1) {
		EncodeColorBlock(in, blk, true);
		return;
	}
	if (f == D3DFMT_DXT2 || f == D3DFMT_DXT3) {
		memset(blk, 0, 8);
		for (int i = 0; i < 16; ++i)
			blk[i / 2] |= (unsigned char)(((in[i] >> 28) & 15) << ((i & 1) * 4));
	} else {
		unsigned a0 = 0, a1 = 255;
		for (int i = 0; i < 16; ++i) {
			unsigned a = in[i] >> 24;
			if (a > a0) a0 = a;
			if (a < a1) a1 = a;
		}
		if (a0 == a1) { if (a0 < 255) ++a0; else --a1; }	// a0 > a1 selects the 8 value mode
		unsigned pal[8];
		pal[0] = a0; pal[1] = a1;
		for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
		unsigned long long bits = 0;
		for (int i = 0; i < 16; ++i) {
			unsigned a = in[i] >> 24;
			int best = 0, bestD = 1 << 30;
			for (int k = 0; k < 8; ++k) {
				int d = (int)a - (int)pal[k];
				d *= d;
				if (d < bestD) { bestD = d; best = k; }
			}
			bits |= (unsigned long long)best << (3 * i);
		}
		blk[0] = (unsigned char)a0; blk[1] = (unsigned char)a1;
		for (int i = 0; i < 6; ++i) blk[2 + i] = (unsigned char)(bits >> (8 * i));
	}
	EncodeColorBlock(in, blk + 8, false);
}

// ------------------------------------------------------------------------------------------------
// TexImage <-> locked surface
// ------------------------------------------------------------------------------------------------

struct TexImage
{
	UINT w, h;
	std::vector<TexPixel> px;
	TexImage(UINT ww = 0, UINT hh = 0) : w(ww), h(hh), px((size_t)ww * hh) {}
	TexPixel &at(UINT x, UINT y) { return px[(size_t)y * w + x]; }
	TexPixel at(UINT x, UINT y) const { return px[(size_t)y * w + x]; }
};

static bool ReadLocked(const D3DLOCKED_RECT &lr, D3DFORMAT f, UINT w, UINT h, const PALETTEENTRY *pal, TexImage &img)
{
	img = TexImage(w, h);
	const unsigned char *base = (const unsigned char *)lr.pBits;
	if (IsDXT(f)) {
		UINT blockBytes = (f == D3DFMT_DXT1) ? 8 : 16;
		for (UINT by = 0; by < (h + 3) / 4; ++by)
			for (UINT bx = 0; bx < (w + 3) / 4; ++bx) {
				TexPixel blk[16];
				DecodeDXTBlock(base + by * lr.Pitch + bx * blockBytes, f, blk);
				for (UINT j = 0; j < 4; ++j)
					for (UINT i = 0; i < 4; ++i)
						if (bx * 4 + i < w && by * 4 + j < h)
							img.at(bx * 4 + i, by * 4 + j) = blk[j * 4 + i];
			}
		return true;
	}
	UINT bpp = BytesPerPixel(f);
	if (!bpp)
		return false;
	for (UINT y = 0; y < h; ++y)
		for (UINT x = 0; x < w; ++x)
			img.at(x, y) = DecodePixel(base + y * lr.Pitch + x * bpp, f, pal);
	return true;
}

static bool WriteLocked(D3DLOCKED_RECT &lr, D3DFORMAT f, const TexImage &img)
{
	unsigned char *base = (unsigned char *)lr.pBits;
	if (IsDXT(f)) {
		UINT blockBytes = (f == D3DFMT_DXT1) ? 8 : 16;
		for (UINT by = 0; by < (img.h + 3) / 4; ++by)
			for (UINT bx = 0; bx < (img.w + 3) / 4; ++bx) {
				TexPixel blk[16];
				for (UINT j = 0; j < 4; ++j)
					for (UINT i = 0; i < 4; ++i) {
						UINT x = bx * 4 + i, y = by * 4 + j;
						if (x >= img.w) x = img.w - 1;
						if (y >= img.h) y = img.h - 1;
						blk[j * 4 + i] = img.at(x, y);
					}
				EncodeDXTBlock(blk, f, base + by * lr.Pitch + bx * blockBytes);
			}
		return true;
	}
	UINT bpp = BytesPerPixel(f);
	if (!bpp || f == D3DFMT_P8)
		return false;
	for (UINT y = 0; y < img.h; ++y)
		for (UINT x = 0; x < img.w; ++x)
			EncodePixel(base + y * lr.Pitch + x * bpp, f, img.at(x, y));
	return true;
}

static TexImage Resample(const TexImage &src, UINT w, UINT h, DWORD filter)
{
	if (src.w == w && src.h == h)
		return src;
	TexImage dst(w, h);
	DWORD kind = filter & 0xFFFF;
	bool point = (kind == D3DX_FILTER_NONE || kind == D3DX_FILTER_POINT);
	if (!point && src.w == w * 2 && src.h == h * 2) {
		// 2x2 box filter (mip map generation)
		for (UINT y = 0; y < h; ++y)
			for (UINT x = 0; x < w; ++x) {
				TexPixel p[4] = { src.at(2 * x, 2 * y), src.at(2 * x + 1, 2 * y), src.at(2 * x, 2 * y + 1), src.at(2 * x + 1, 2 * y + 1) };
				TexPixel r = 0;
				for (int s = 0; s < 32; s += 8)
					r |= ((((p[0] >> s) & 255) + ((p[1] >> s) & 255) + ((p[2] >> s) & 255) + ((p[3] >> s) & 255) + 2) / 4) << s;
				dst.at(x, y) = r;
			}
		return dst;
	}
	for (UINT y = 0; y < h; ++y)
		for (UINT x = 0; x < w; ++x) {
			if (point) {
				dst.at(x, y) = src.at(x * src.w / w, y * src.h / h);
				continue;
			}
			// bilinear
			float fx = ((x + 0.5f) * src.w / w) - 0.5f, fy = ((y + 0.5f) * src.h / h) - 0.5f;
			if (fx < 0) fx = 0;
			if (fy < 0) fy = 0;
			UINT x0 = (UINT)fx, y0 = (UINT)fy;
			UINT x1 = (x0 + 1 < src.w) ? x0 + 1 : x0, y1 = (y0 + 1 < src.h) ? y0 + 1 : y0;
			float ax = fx - x0, ay = fy - y0;
			TexPixel r = 0;
			for (int s = 0; s < 32; s += 8) {
				float c = ((src.at(x0, y0) >> s) & 255) * (1 - ax) * (1 - ay) + ((src.at(x1, y0) >> s) & 255) * ax * (1 - ay) +
					((src.at(x0, y1) >> s) & 255) * (1 - ax) * ay + ((src.at(x1, y1) >> s) & 255) * ax * ay;
				r |= ((TexPixel)(c + 0.5f) & 255) << s;
			}
			dst.at(x, y) = r;
		}
	return dst;
}

static RECT FullRect(const D3DSURFACE_DESC &d)
{
	RECT r = { 0, 0, (LONG)d.Width, (LONG)d.Height };
	return r;
}

HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8 dst, CONST PALETTEENTRY *dstPal, CONST RECT *dstRect,
	LPDIRECT3DSURFACE8 src, CONST PALETTEENTRY *srcPal, CONST RECT *srcRect, DWORD filter, D3DCOLOR colorKey)
{
	(void)dstPal;
	if (!dst || !src)
		return D3DERR_INVALIDCALL;
	D3DSURFACE_DESC sd, dd;
	src->GetDesc(&sd);
	dst->GetDesc(&dd);
	RECT sr = srcRect ? *srcRect : FullRect(sd);
	RECT dr = dstRect ? *dstRect : FullRect(dd);
	UINT sw = sr.right - sr.left, sh = sr.bottom - sr.top, dw = dr.right - dr.left, dh = dr.bottom - dr.top;

	// Compressed formats are locked on 4x4 block boundaries.
	if (IsDXT(sd.Format) && ((sr.left | sr.top) & 3))
		return D3DERR_INVALIDCALL;
	if (IsDXT(dd.Format) && ((dr.left | dr.top) & 3))
		return D3DERR_INVALIDCALL;

	D3DLOCKED_RECT sl, dl;
	if (FAILED(src->LockRect(&sl, &sr, D3DLOCK_READONLY)))
		return D3DERR_INVALIDCALL;

	// Fast path: identical format and size, no colour key.
	if (sd.Format == dd.Format && sw == dw && sh == dh && colorKey == 0 && sd.Format != D3DFMT_P8) {
		if (FAILED(dst->LockRect(&dl, &dr, 0))) {
			src->UnlockRect();
			return D3DERR_INVALIDCALL;
		}
		UINT rows, rowBytes;
		if (IsDXT(sd.Format)) {
			rows = (sh + 3) / 4;
			rowBytes = ((sw + 3) / 4) * (sd.Format == D3DFMT_DXT1 ? 8 : 16);
		} else {
			rows = sh;
			rowBytes = sw * BytesPerPixel(sd.Format);
		}
		for (UINT y = 0; y < rows; ++y)
			memcpy((char *)dl.pBits + y * dl.Pitch, (const char *)sl.pBits + y * sl.Pitch, rowBytes);
		dst->UnlockRect();
		src->UnlockRect();
		return D3D_OK;
	}

	TexImage img;
	bool ok = ReadLocked(sl, sd.Format, sw, sh, srcPal, img);
	src->UnlockRect();
	if (!ok)
		return D3DERR_INVALIDCALL;

	if (colorKey != 0)
		for (size_t i = 0; i < img.px.size(); ++i)
			if (img.px[i] == colorKey)
				img.px[i] = 0;

	img = Resample(img, dw, dh, filter);

	if (FAILED(dst->LockRect(&dl, &dr, 0)))
		return D3DERR_INVALIDCALL;
	ok = WriteLocked(dl, dd.Format, img);
	dst->UnlockRect();
	return ok ? D3D_OK : D3DERR_INVALIDCALL;
}

// ------------------------------------------------------------------------------------------------
// Texture creation and mip map generation
// ------------------------------------------------------------------------------------------------

static UINT FullMipCount(UINT w, UINT h, UINT d = 1)
{
	UINT n = 1;
	while (w > 1 || h > 1 || d > 1) {
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
		d = d > 1 ? d / 2 : 1;
		++n;
	}
	return n;
}

static UINT NextPow2(UINT v)
{
	UINT p = 1;
	while (p < v) p <<= 1;
	return p;
}

/// Adjust size, mip count and format to what the device supports, like D3DX does.
static void CheckRequirements(LPDIRECT3DDEVICE8 dev, UINT *w, UINT *h, UINT *mips, DWORD usage,
	D3DFORMAT *fmt, D3DRESOURCETYPE type)
{
	D3DCAPS8 caps;
	dev->GetDeviceCaps(&caps);
	if (*w == 0 || *w == D3DX_DEFAULT) *w = 256;
	if (*h == 0 || *h == D3DX_DEFAULT) *h = 256;

	bool pow2 = (caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0;
	if (type == D3DRTYPE_CUBETEXTURE) pow2 = (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP_POW2) != 0 || pow2;
	if (pow2) { *w = NextPow2(*w); *h = NextPow2(*h); }
	if (caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) { UINT m = *w > *h ? *w : *h; *w = *h = m; }
	if (caps.MaxTextureWidth && *w > caps.MaxTextureWidth) *w = caps.MaxTextureWidth;
	if (caps.MaxTextureHeight && *h > caps.MaxTextureHeight) *h = caps.MaxTextureHeight;
	if (fmt && IsDXT(*fmt)) { *w = (*w + 3) & ~3u; *h = (*h + 3) & ~3u; }

	UINT full = FullMipCount(*w, *h);
	if (mips && (*mips == 0 || *mips == D3DX_DEFAULT || *mips > full)) *mips = full;

	if (fmt) {
		IDirect3D8 *d3d = NULL;
		D3DDEVICE_CREATION_PARAMETERS cp;
		D3DDISPLAYMODE mode;
		if (SUCCEEDED(dev->GetDirect3D(&d3d)) && SUCCEEDED(dev->GetCreationParameters(&cp)) &&
				SUCCEEDED(dev->GetDisplayMode(&mode))) {
			if (*fmt == D3DFMT_UNKNOWN) *fmt = D3DFMT_A8R8G8B8;
			if (FAILED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format, usage, type, *fmt))) {
				static const D3DFORMAT fallbacks[] = { D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_A4R4G4B4, D3DFMT_R5G6B5 };
				for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i)
					if (SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format, usage, type, fallbacks[i]))) {
						*fmt = fallbacks[i];
						break;
					}
			}
		}
		if (d3d) d3d->Release();
	}
}

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8 dev, UINT w, UINT h, UINT mips, DWORD usage, D3DFORMAT fmt,
	D3DPOOL pool, LPDIRECT3DTEXTURE8 *out)
{
	if (!dev || !out)
		return D3DERR_INVALIDCALL;
	CheckRequirements(dev, &w, &h, &mips, usage, &fmt, D3DRTYPE_TEXTURE);
	return dev->CreateTexture(w, h, mips, usage, fmt, pool, out);
}

HRESULT WINAPI D3DXCreateCubeTexture(LPDIRECT3DDEVICE8 dev, UINT size, UINT mips, DWORD usage, D3DFORMAT fmt,
	D3DPOOL pool, LPDIRECT3DCUBETEXTURE8 *out)
{
	if (!dev || !out)
		return D3DERR_INVALIDCALL;
	UINT w = size, h = size;
	CheckRequirements(dev, &w, &h, &mips, usage, &fmt, D3DRTYPE_CUBETEXTURE);
	return dev->CreateCubeTexture(w, mips, usage, fmt, pool, out);
}

HRESULT WINAPI D3DXCreateVolumeTexture(LPDIRECT3DDEVICE8 dev, UINT w, UINT h, UINT d, UINT mips, DWORD usage,
	D3DFORMAT fmt, D3DPOOL pool, LPDIRECT3DVOLUMETEXTURE8 *out)
{
	if (!dev || !out)
		return D3DERR_INVALIDCALL;
	if (d == 0 || d == D3DX_DEFAULT) d = 1;
	CheckRequirements(dev, &w, &h, NULL, usage, &fmt, D3DRTYPE_VOLUMETEXTURE);
	UINT full = FullMipCount(w, h, d);
	if (mips == 0 || mips == D3DX_DEFAULT || mips > full) mips = full;
	return dev->CreateVolumeTexture(w, h, d, mips, usage, fmt, pool, out);
}

HRESULT WINAPI D3DXFilterTexture(LPDIRECT3DBASETEXTURE8 base, CONST PALETTEENTRY *pal, UINT srcLevel, DWORD filter)
{
	if (!base)
		return D3DERR_INVALIDCALL;
	if (filter == D3DX_DEFAULT) filter = D3DX_FILTER_BOX;
	if (srcLevel == D3DX_DEFAULT) srcLevel = 0;
	UINT levels = base->GetLevelCount();
	HRESULT hr = D3D_OK;

	switch (base->GetType())
	{
		case D3DRTYPE_TEXTURE: {
			IDirect3DTexture8 *tex = (IDirect3DTexture8 *)base;
			for (UINT l = srcLevel + 1; l < levels && SUCCEEDED(hr); ++l) {
				IDirect3DSurface8 *s = NULL, *d = NULL;
				tex->GetSurfaceLevel(l - 1, &s);
				tex->GetSurfaceLevel(l, &d);
				hr = D3DXLoadSurfaceFromSurface(d, pal, NULL, s, pal, NULL, filter, 0);
				if (s) s->Release();
				if (d) d->Release();
			}
			return hr;
		}
		case D3DRTYPE_CUBETEXTURE: {
			IDirect3DCubeTexture8 *tex = (IDirect3DCubeTexture8 *)base;
			for (int face = 0; face < 6; ++face)
				for (UINT l = srcLevel + 1; l < levels && SUCCEEDED(hr); ++l) {
					IDirect3DSurface8 *s = NULL, *d = NULL;
					tex->GetCubeMapSurface((D3DCUBEMAP_FACES)face, l - 1, &s);
					tex->GetCubeMapSurface((D3DCUBEMAP_FACES)face, l, &d);
					hr = D3DXLoadSurfaceFromSurface(d, pal, NULL, s, pal, NULL, filter, 0);
					if (s) s->Release();
					if (d) d->Release();
				}
			return hr;
		}
		default:
			return D3D_OK;	// volume textures: mip levels are left as created
	}
}

// ------------------------------------------------------------------------------------------------
// TexImage files (DDS, TGA)
// ------------------------------------------------------------------------------------------------

struct FileImage
{
	UINT w, h;
	D3DFORMAT fmt;
	std::vector<std::vector<unsigned char> > levels;	// tightly packed rows (DXT: rows of blocks)
};

static bool ReadFileBytes(const char *name, std::vector<unsigned char> &data)
{
	FILE *f = fopen(name, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) { fclose(f); return false; }
	data.resize((size_t)n);
	bool ok = fread(&data[0], 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	return ok;
}

static unsigned Rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

static UINT LevelBytes(D3DFORMAT f, UINT w, UINT h)
{
	if (IsDXT(f))
		return ((w + 3) / 4) * ((h + 3) / 4) * (f == D3DFMT_DXT1 ? 8 : 16);
	return w * h * BytesPerPixel(f);
}

static bool ParseDDS(const std::vector<unsigned char> &d, FileImage &img)
{
	if (d.size() < 128 || memcmp(&d[0], "DDS ", 4) != 0)
		return false;
	const unsigned char *h = &d[4];
	img.h = Rd32(h + 8);
	img.w = Rd32(h + 12);
	UINT mipCount = (Rd32(h + 4) & 0x20000) ? Rd32(h + 24) : 1;
	if (mipCount == 0) mipCount = 1;
	const unsigned char *pf = h + 72;
	unsigned pfFlags = Rd32(pf + 4), fourCC = Rd32(pf + 8), bits = Rd32(pf + 12);
	unsigned rm = Rd32(pf + 16), gm = Rd32(pf + 20), bm = Rd32(pf + 24), am = Rd32(pf + 28);
	if (pfFlags & 0x4) {
		img.fmt = (D3DFORMAT)fourCC;
		if (!IsDXT(img.fmt)) return false;
	} else if (bits == 32 && rm == 0xFF0000 && am == 0xFF000000u) img.fmt = D3DFMT_A8R8G8B8;
	else if (bits == 32 && rm == 0xFF0000) img.fmt = D3DFMT_X8R8G8B8;
	else if (bits == 24) img.fmt = D3DFMT_R8G8B8;
	else if (bits == 16 && rm == 0xF800) img.fmt = D3DFMT_R5G6B5;
	else if (bits == 16 && rm == 0x7C00 && am == 0x8000) img.fmt = D3DFMT_A1R5G5B5;
	else if (bits == 16 && rm == 0x7C00) img.fmt = D3DFMT_X1R5G5B5;
	else if (bits == 16 && rm == 0x0F00) img.fmt = (am ? D3DFMT_A4R4G4B4 : D3DFMT_X4R4G4B4);
	else if (bits == 16 && rm == 0xFF && am == 0xFF00) img.fmt = D3DFMT_A8L8;
	else if (bits == 8 && am == 0xFF && !rm) img.fmt = D3DFMT_A8;
	else if (bits == 8) img.fmt = D3DFMT_L8;
	else return false;
	(void)gm; (void)bm;

	size_t off = 128;
	UINT w = img.w, hh = img.h;
	for (UINT l = 0; l < mipCount; ++l) {
		UINT n = LevelBytes(img.fmt, w, hh);
		if (off + n > d.size()) break;
		img.levels.push_back(std::vector<unsigned char>(d.begin() + off, d.begin() + off + n));
		off += n;
		w = w > 1 ? w / 2 : 1;
		hh = hh > 1 ? hh / 2 : 1;
	}
	return !img.levels.empty();
}

static bool ParseTGA(const std::vector<unsigned char> &d, FileImage &img)
{
	if (d.size() < 18)
		return false;
	unsigned idLen = d[0], cmapType = d[1], type = d[2];
	unsigned w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16], desc = d[17];
	if (cmapType != 0 || !(type == 2 || type == 3 || type == 10 || type == 11) || !w || !h)
		return false;
	UINT src = bpp / 8;
	if (bpp == 32) img.fmt = D3DFMT_A8R8G8B8;
	else if (bpp == 24) img.fmt = D3DFMT_X8R8G8B8;
	else if (bpp == 16) img.fmt = D3DFMT_A1R5G5B5;
	else if (bpp == 8) img.fmt = D3DFMT_L8;
	else return false;
	img.w = w; img.h = h;
	UINT dstBpp = BytesPerPixel(img.fmt);
	std::vector<unsigned char> raw((size_t)w * h * src);
	size_t off = 18 + idLen;
	if (type == 2 || type == 3) {
		if (off + raw.size() > d.size()) return false;
		memcpy(&raw[0], &d[off], raw.size());
	} else {
		size_t o = 0;
		while (o < raw.size() && off < d.size()) {
			unsigned hdr = d[off++];
			unsigned count = (hdr & 0x7F) + 1;
			if (hdr & 0x80) {
				if (off + src > d.size()) return false;
				for (unsigned i = 0; i < count && o < raw.size(); ++i, o += src) memcpy(&raw[o], &d[off], src);
				off += src;
			} else {
				size_t n = (size_t)count * src;
				if (off + n > d.size() || o + n > raw.size()) return false;
				memcpy(&raw[o], &d[off], n);
				off += n; o += n;
			}
		}
	}
	std::vector<unsigned char> out((size_t)w * h * dstBpp);
	bool topDown = (desc & 0x20) != 0;
	for (unsigned y = 0; y < h; ++y) {
		unsigned sy = topDown ? y : (h - 1 - y);
		for (unsigned x = 0; x < w; ++x) {
			const unsigned char *s = &raw[((size_t)sy * w + x) * src];
			unsigned char *t = &out[((size_t)y * w + x) * dstBpp];
			if (bpp == 24) { t[0] = s[0]; t[1] = s[1]; t[2] = s[2]; t[3] = 255; }
			else memcpy(t, s, src);
		}
	}
	img.levels.push_back(out);
	return true;
}

static bool LoadImageFile(const char *name, FileImage &img)
{
	std::vector<unsigned char> data;
	if (!ReadFileBytes(name, data))
		return false;
	return ParseDDS(data, img) || ParseTGA(data, img);
}

/// Put one level of a file image into a lockable system memory surface of the file's format.
static IDirect3DSurface8 *MakeImageSurface(LPDIRECT3DDEVICE8 dev, const FileImage &img, UINT level)
{
	UINT w = img.w >> level, h = img.h >> level;
	if (!w) w = 1;
	if (!h) h = 1;
	IDirect3DSurface8 *s = NULL;
	if (FAILED(dev->CreateImageSurface(w, h, img.fmt, &s)))
		return NULL;
	D3DLOCKED_RECT lr;
	if (FAILED(s->LockRect(&lr, NULL, 0))) {
		s->Release();
		return NULL;
	}
	UINT rows = IsDXT(img.fmt) ? (h + 3) / 4 : h;
	UINT rowBytes = LevelBytes(img.fmt, w, IsDXT(img.fmt) ? 4 : 1);
	for (UINT y = 0; y < rows; ++y)
		memcpy((char *)lr.pBits + y * lr.Pitch, &img.levels[level][(size_t)y * rowBytes], rowBytes);
	s->UnlockRect();
	return s;
}

HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE8 dst, CONST PALETTEENTRY *dstPal, CONST RECT *dstRect,
	LPCSTR file, CONST RECT *srcRect, DWORD filter, D3DCOLOR colorKey, D3DXIMAGE_INFO *info)
{
	FileImage img;
	if (!dst || !file || !LoadImageFile(file, img))
		return D3DXERR_INVALIDDATA;
	if (info) {
		info->Width = img.w; info->Height = img.h; info->Depth = 1;
		info->MipLevels = (UINT)img.levels.size(); info->Format = img.fmt;
	}
	IDirect3DDevice8 *dev = NULL;
	dst->GetDevice(&dev);
	IDirect3DSurface8 *s = dev ? MakeImageSurface(dev, img, 0) : NULL;
	if (dev) dev->Release();
	if (!s)
		return D3DERR_INVALIDCALL;
	HRESULT hr = D3DXLoadSurfaceFromSurface(dst, dstPal, dstRect, s, NULL, srcRect, filter, colorKey);
	s->Release();
	return hr;
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8 dev, LPCSTR file, UINT w, UINT h, UINT mips,
	DWORD usage, D3DFORMAT fmt, D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DCOLOR colorKey,
	D3DXIMAGE_INFO *info, PALETTEENTRY *pal, LPDIRECT3DTEXTURE8 *out)
{
	(void)pal;
	if (!dev || !file || !out)
		return D3DERR_INVALIDCALL;
	FileImage img;
	if (!LoadImageFile(file, img))
		return D3DXERR_INVALIDDATA;
	if (info) {
		info->Width = img.w; info->Height = img.h; info->Depth = 1;
		info->MipLevels = (UINT)img.levels.size(); info->Format = img.fmt;
	}
	if (w == 0 || w == D3DX_DEFAULT) w = img.w;
	if (h == 0 || h == D3DX_DEFAULT) h = img.h;
	if (fmt == D3DFMT_UNKNOWN) fmt = img.fmt;
	if (filter == D3DX_DEFAULT) filter = D3DX_FILTER_BOX;
	if (mipFilter == D3DX_DEFAULT) mipFilter = D3DX_FILTER_BOX;

	IDirect3DTexture8 *tex = NULL;
	HRESULT hr = D3DXCreateTexture(dev, w, h, mips, usage, fmt, pool, &tex);
	if (FAILED(hr))
		return hr;

	UINT levels = tex->GetLevelCount();
	UINT copied = 0;
	for (UINT l = 0; l < levels && l < img.levels.size(); ++l) {
		D3DSURFACE_DESC dd;
		tex->GetLevelDesc(l, &dd);
		// Use the file's own mip levels only when they match the texture's levels exactly.
		if (l > 0 && (dd.Width != (img.w >> l ? img.w >> l : 1) || dd.Height != (img.h >> l ? img.h >> l : 1)))
			break;
		IDirect3DSurface8 *s = MakeImageSurface(dev, img, l);
		IDirect3DSurface8 *d = NULL;
		tex->GetSurfaceLevel(l, &d);
		hr = (s && d) ? D3DXLoadSurfaceFromSurface(d, NULL, NULL, s, NULL, NULL, filter, colorKey) : D3DERR_INVALIDCALL;
		if (s) s->Release();
		if (d) d->Release();
		if (FAILED(hr)) {
			tex->Release();
			return hr;
		}
		++copied;
	}
	if (copied < levels && (mipFilter & 0xFFFF) != D3DX_FILTER_NONE)
		D3DXFilterTexture(tex, NULL, copied - 1, mipFilter);

	*out = tex;
	return D3D_OK;
}

// ------------------------------------------------------------------------------------------------
// Shader assembler
// ------------------------------------------------------------------------------------------------
//
// The vertex/pixel shader 1.x token format is shared by Direct3D 8 and 9, so the D3DX9 assembler
// (d3dx9_43.dll, part of the DirectX End-User Runtime) produces byte code Direct3D 8 accepts.
// It is loaded on demand; without it shader creation fails and the game uses its fixed function
// fallbacks.

typedef HRESULT (WINAPI *PFN_D3DXAssembleShader9)(LPCSTR, UINT, const void *defines, void *include,
	DWORD flags, LPD3DXBUFFER *shader, LPD3DXBUFFER *errors);

static PFN_D3DXAssembleShader9 GetAssembler()
{
	static bool tried = false;
	static PFN_D3DXAssembleShader9 fn = NULL;
	if (!tried) {
		tried = true;
		static const char *dlls[] = { "d3dx9_43.dll", "d3dx9_42.dll", "d3dx9_41.dll", "d3dx9_40.dll" };
		for (size_t i = 0; i < sizeof(dlls) / sizeof(dlls[0]) && !fn; ++i) {
			HMODULE m = LoadLibraryA(dlls[i]);
			if (m)
				fn = (PFN_D3DXAssembleShader9)GetProcAddress(m, "D3DXAssembleShader");
		}
	}
	return fn;
}

HRESULT WINAPI D3DXAssembleShader(LPCVOID src, UINT len, DWORD flags, LPD3DXBUFFER *constants,
	LPD3DXBUFFER *compiled, LPD3DXBUFFER *errors)
{
	if (constants) *constants = NULL;	// "def" constants are embedded in the D3DX9 byte code
	if (errors) *errors = NULL;
	if (compiled) *compiled = NULL;
	PFN_D3DXAssembleShader9 fn = GetAssembler();
	if (!fn)
		return E_NOTIMPL;
	DWORD flags9 = 0;
	if (flags & D3DXASM_DEBUG) flags9 |= 1;				// D3DXSHADER_DEBUG
	if (flags & D3DXASM_SKIPVALIDATION) flags9 |= 2;	// D3DXSHADER_SKIPVALIDATION
	return fn((LPCSTR)src, len, NULL, NULL, flags9, compiled, errors);
}

HRESULT WINAPI D3DXAssembleShaderFromFileA(LPCSTR file, DWORD flags, LPD3DXBUFFER *constants,
	LPD3DXBUFFER *compiled, LPD3DXBUFFER *errors)
{
	std::vector<unsigned char> data;
	if (!ReadFileBytes(file, data))
		return D3DXERR_INVALIDDATA;
	return D3DXAssembleShader(&data[0], (UINT)data.size(), flags, constants, compiled, errors);
}
