/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// d3dx8.h - replacement for the DirectX 8 extension library (D3DX8).
//
// D3DX8 shipped only with the DirectX 8.1 SDK, which is no longer distributed.  This header
// provides the subset of the D3DX8 API used by the game, implemented in d3dx8compat.cpp on top of
// plain Direct3D 8:
//   * math types and functions (D3DXMATRIX, D3DXVECTOR3/4, inverse, multiply, transforms, ...)
//   * D3DXGetFVFVertexSize, D3DXGetErrorStringA
//   * texture helpers: D3DXCreate[Cube|Volume]Texture, D3DXLoadSurfaceFromSurface,
//     D3DXFilterTexture, D3DXCreateTextureFromFileExA (DDS and TGA)
//   * D3DXAssembleShader[FromFile]: forwarded to the D3DX9 assembler (d3dx9_43.dll) at run time;
//     the 1.x shader token format is the same in Direct3D 8 and 9.

#ifndef ZH_D3DX8_H
#define ZH_D3DX8_H

#include <d3d8.h>
#include <math.h>
#include <string.h>

#include "d3dx8math.h"

#ifndef D3DX_DEFAULT
#define D3DX_DEFAULT				((UINT)-1)
#endif

#define D3DX_FILTER_NONE			(1 << 0)
#define D3DX_FILTER_POINT			(2 << 0)
#define D3DX_FILTER_LINEAR			(3 << 0)
#define D3DX_FILTER_TRIANGLE		(4 << 0)
#define D3DX_FILTER_BOX				(5 << 0)
#define D3DX_FILTER_MIRROR_U		(1 << 16)
#define D3DX_FILTER_MIRROR_V		(2 << 16)
#define D3DX_FILTER_MIRROR_W		(4 << 16)
#define D3DX_FILTER_MIRROR			(7 << 16)
#define D3DX_FILTER_DITHER			(8 << 16)

#define D3DXASM_DEBUG				(1 << 0)
#define D3DXASM_SKIPVALIDATION		(1 << 1)

#ifndef D3DXERR_INVALIDDATA
#define D3DXERR_INVALIDDATA			MAKE_HRESULT(1, 0x876, 2905)
#endif

typedef struct _D3DXIMAGE_INFO
{
	UINT Width;
	UINT Height;
	UINT Depth;
	UINT MipLevels;
	D3DFORMAT Format;
} D3DXIMAGE_INFO;

// Minimal ID3DXBuffer: a reference counted block of memory.
struct ID3DXBuffer
{
	virtual HRESULT __stdcall QueryInterface(REFIID, void **) { return E_NOINTERFACE; }
	virtual ULONG __stdcall AddRef() = 0;
	virtual ULONG __stdcall Release() = 0;
	virtual void * __stdcall GetBufferPointer() = 0;
	virtual DWORD __stdcall GetBufferSize() = 0;
};
typedef ID3DXBuffer *LPD3DXBUFFER;

HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer);

UINT WINAPI D3DXGetFVFVertexSize(DWORD FVF);
HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR pBuffer, UINT BufferLen);

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8 pDevice, UINT Width, UINT Height, UINT MipLevels,
	DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateCubeTexture(LPDIRECT3DDEVICE8 pDevice, UINT Size, UINT MipLevels,
	DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DCUBETEXTURE8 *ppCubeTexture);
HRESULT WINAPI D3DXCreateVolumeTexture(LPDIRECT3DDEVICE8 pDevice, UINT Width, UINT Height, UINT Depth,
	UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DVOLUMETEXTURE8 *ppVolumeTexture);

HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8 pDestSurface, CONST PALETTEENTRY *pDestPalette,
	CONST RECT *pDestRect, LPDIRECT3DSURFACE8 pSrcSurface, CONST PALETTEENTRY *pSrcPalette,
	CONST RECT *pSrcRect, DWORD Filter, D3DCOLOR ColorKey);

HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE8 pDestSurface, CONST PALETTEENTRY *pDestPalette,
	CONST RECT *pDestRect, LPCSTR pSrcFile, CONST RECT *pSrcRect, DWORD Filter, D3DCOLOR ColorKey,
	D3DXIMAGE_INFO *pSrcInfo);
#define D3DXLoadSurfaceFromFile D3DXLoadSurfaceFromFileA

HRESULT WINAPI D3DXFilterTexture(LPDIRECT3DBASETEXTURE8 pBaseTexture, CONST PALETTEENTRY *pPalette,
	UINT SrcLevel, DWORD Filter);

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8 pDevice, LPCSTR pSrcFile, UINT Width,
	UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter,
	DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette,
	LPDIRECT3DTEXTURE8 *ppTexture);
#define D3DXCreateTextureFromFileEx D3DXCreateTextureFromFileExA

HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags,
	LPD3DXBUFFER *ppConstants, LPD3DXBUFFER *ppCompiledShader, LPD3DXBUFFER *ppCompilationErrors);
HRESULT WINAPI D3DXAssembleShaderFromFileA(LPCSTR pSrcFile, DWORD Flags,
	LPD3DXBUFFER *ppConstants, LPD3DXBUFFER *ppCompiledShader, LPD3DXBUFFER *ppCompilationErrors);
#define D3DXAssembleShaderFromFile D3DXAssembleShaderFromFileA

#endif // ZH_D3DX8_H
