/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// d3dx8compat.cpp - implementation of the D3DX8 subset declared in d3dx8.h / d3dx8math.h.

#include "d3dx8.h"
#include <stdio.h>
#include <stdlib.h>

// ------------------------------------------------------------------------------------------------
// Math
// ------------------------------------------------------------------------------------------------

D3DXMATRIX *D3DXMatrixInverse(D3DXMATRIX *out, FLOAT *determinant, const D3DXMATRIX *mat)
{
	const FLOAT *m = &mat->_11;
	FLOAT inv[16];

	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

	FLOAT det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if (determinant)
		*determinant = det;
	if (det == 0.0f)
		return NULL;

	FLOAT invDet = 1.0f / det;
	for (int i = 0; i < 16; ++i)
		(&out->_11)[i] = inv[i] * invDet;
	return out;
}

// ------------------------------------------------------------------------------------------------
// Misc
// ------------------------------------------------------------------------------------------------

UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf)
{
	UINT size = 0;
	switch (fvf & D3DFVF_POSITION_MASK)
	{
		case D3DFVF_XYZ:	size += 12; break;
		case D3DFVF_XYZRHW:	size += 16; break;
		case D3DFVF_XYZB1:	size += 16; break;
		case D3DFVF_XYZB2:	size += 20; break;
		case D3DFVF_XYZB3:	size += 24; break;
		case D3DFVF_XYZB4:	size += 28; break;
		case D3DFVF_XYZB5:	size += 32; break;
	}
	if (fvf & D3DFVF_NORMAL)	size += 12;
	if (fvf & D3DFVF_PSIZE)		size += 4;
	if (fvf & D3DFVF_DIFFUSE)	size += 4;
	if (fvf & D3DFVF_SPECULAR)	size += 4;

	UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (UINT i = 0; i < texCount; ++i)
	{
		// D3DFVF_TEXCOORDSIZEn: 2 bits per stage; 0 = 2 floats, 1 = 3, 2 = 4, 3 = 1
		switch ((fvf >> (16 + i * 2)) & 3)
		{
			case 0: size += 8; break;
			case 1: size += 12; break;
			case 2: size += 16; break;
			case 3: size += 4; break;
		}
	}
	return size;
}

HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR buffer, UINT len)
{
	if (!buffer || len == 0)
		return E_INVALIDARG;
	const char *name = NULL;
	switch (hr)
	{
		case D3D_OK:							name = "D3D_OK"; break;
		case D3DERR_WRONGTEXTUREFORMAT:			name = "D3DERR_WRONGTEXTUREFORMAT"; break;
		case D3DERR_UNSUPPORTEDCOLOROPERATION:	name = "D3DERR_UNSUPPORTEDCOLOROPERATION"; break;
		case D3DERR_UNSUPPORTEDCOLORARG:		name = "D3DERR_UNSUPPORTEDCOLORARG"; break;
		case D3DERR_UNSUPPORTEDALPHAOPERATION:	name = "D3DERR_UNSUPPORTEDALPHAOPERATION"; break;
		case D3DERR_UNSUPPORTEDALPHAARG:		name = "D3DERR_UNSUPPORTEDALPHAARG"; break;
		case D3DERR_TOOMANYOPERATIONS:			name = "D3DERR_TOOMANYOPERATIONS"; break;
		case D3DERR_CONFLICTINGTEXTUREFILTER:	name = "D3DERR_CONFLICTINGTEXTUREFILTER"; break;
		case D3DERR_UNSUPPORTEDFACTORVALUE:		name = "D3DERR_UNSUPPORTEDFACTORVALUE"; break;
		case D3DERR_CONFLICTINGRENDERSTATE:		name = "D3DERR_CONFLICTINGRENDERSTATE"; break;
		case D3DERR_UNSUPPORTEDTEXTUREFILTER:	name = "D3DERR_UNSUPPORTEDTEXTUREFILTER"; break;
		case D3DERR_CONFLICTINGTEXTUREPALETTE:	name = "D3DERR_CONFLICTINGTEXTUREPALETTE"; break;
		case D3DERR_DRIVERINTERNALERROR:		name = "D3DERR_DRIVERINTERNALERROR"; break;
		case D3DERR_NOTFOUND:					name = "D3DERR_NOTFOUND"; break;
		case D3DERR_MOREDATA:					name = "D3DERR_MOREDATA"; break;
		case D3DERR_DEVICELOST:					name = "D3DERR_DEVICELOST"; break;
		case D3DERR_DEVICENOTRESET:				name = "D3DERR_DEVICENOTRESET"; break;
		case D3DERR_NOTAVAILABLE:				name = "D3DERR_NOTAVAILABLE"; break;
		case D3DERR_OUTOFVIDEOMEMORY:			name = "D3DERR_OUTOFVIDEOMEMORY"; break;
		case D3DERR_INVALIDDEVICE:				name = "D3DERR_INVALIDDEVICE"; break;
		case D3DERR_INVALIDCALL:				name = "D3DERR_INVALIDCALL"; break;
		case D3DERR_DRIVERINVALIDCALL:			name = "D3DERR_DRIVERINVALIDCALL"; break;
		case E_OUTOFMEMORY:						name = "E_OUTOFMEMORY"; break;
		case E_NOTIMPL:							name = "E_NOTIMPL"; break;
	}
	if (name)
		_snprintf(buffer, len, "%s", name);
	else
		_snprintf(buffer, len, "HRESULT 0x%08lX", (unsigned long)hr);
	buffer[len - 1] = 0;
	return D3D_OK;
}

// ------------------------------------------------------------------------------------------------
// ID3DXBuffer
// ------------------------------------------------------------------------------------------------

namespace {
class BufferImpl : public ID3DXBuffer
{
public:
	BufferImpl(DWORD size) : m_refs(1), m_size(size), m_data(malloc(size ? size : 1)) {}
	~BufferImpl() { free(m_data); }
	ULONG __stdcall AddRef() { return ++m_refs; }
	ULONG __stdcall Release() { ULONG r = --m_refs; if (r == 0) delete this; return r; }
	void * __stdcall GetBufferPointer() { return m_data; }
	DWORD __stdcall GetBufferSize() { return m_size; }
private:
	ULONG m_refs;
	DWORD m_size;
	void *m_data;
};
}

HRESULT WINAPI D3DXCreateBuffer(DWORD size, LPD3DXBUFFER *out)
{
	if (!out)
		return D3DERR_INVALIDCALL;
	*out = new BufferImpl(size);
	return D3D_OK;
}
