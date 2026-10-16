/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the LZH-Light library header (not part of the source release).
//
// The game only ever writes RefPack-compressed or uncompressed data
// (CompressionManager::getPreferredCompression); LZH ("NOX") data is only read if a file happens
// to be compressed that way.  Without the original library such data cannot be decoded: these
// functions report failure and the caller treats the file as unreadable.

#ifndef ZH_LZHL_H
#define ZH_LZHL_H

#include <stddef.h>

typedef void *LZHL_CHANDLE;
typedef void *LZHL_DHANDLE;
#define LZHL_CHANDLE_NULL ((LZHL_CHANDLE)0)
#define LZHL_DHANDLE_NULL ((LZHL_DHANDLE)0)

LZHL_CHANDLE LZHLCreateCompressor(void);
size_t LZHLCompressorCalcMaxBuf(size_t rawSize);
size_t LZHLCompress(LZHL_CHANDLE compressor, void *dst, const void *src, size_t srcSize);
void LZHLDestroyCompressor(LZHL_CHANDLE compressor);

LZHL_DHANDLE LZHLCreateDecompressor(void);
int LZHLDecompress(LZHL_DHANDLE decompressor, void *dst, size_t *dstSize, const void *src, size_t *srcSize);
void LZHLDestroyDecompressor(LZHL_DHANDLE decompressor);

#endif
