/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// See CompLibHeader/lzhl.h.
#include "CompLibHeader/lzhl.h"

LZHL_CHANDLE LZHLCreateCompressor(void) { return LZHL_CHANDLE_NULL; }
size_t LZHLCompressorCalcMaxBuf(size_t rawSize) { return rawSize + rawSize / 8 + 64; }
size_t LZHLCompress(LZHL_CHANDLE, void *, const void *, size_t) { return 0; }
void LZHLDestroyCompressor(LZHL_CHANDLE) {}

LZHL_DHANDLE LZHLCreateDecompressor(void) { return LZHL_DHANDLE_NULL; }
int LZHLDecompress(LZHL_DHANDLE, void *, size_t *, const void *, size_t *) { return 0; }
void LZHLDestroyDecompressor(LZHL_DHANDLE) {}
