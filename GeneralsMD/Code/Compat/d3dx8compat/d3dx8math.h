/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// d3dx8math.h - replacement for the D3DX8 math library (types and functions used by the game).
// Conventions follow D3DX: row vectors, row-major matrices, v' = v * M.

#ifndef ZH_D3DX8MATH_H
#define ZH_D3DX8MATH_H

#include <d3d8.h>
#include <math.h>

#ifndef D3DX_PI
#define D3DX_PI		((FLOAT)3.141592654f)
#define D3DX_1BYPI	((FLOAT)0.318309886f)
#define D3DXToRadian(degree) ((degree) * (D3DX_PI / 180.0f))
#define D3DXToDegree(radian) ((radian) * (180.0f / D3DX_PI))
#endif

struct D3DXVECTOR2
{
	FLOAT x, y;
	D3DXVECTOR2() {}
	D3DXVECTOR2(const FLOAT *f) : x(f[0]), y(f[1]) {}
	D3DXVECTOR2(FLOAT fx, FLOAT fy) : x(fx), y(fy) {}
	operator FLOAT* () { return &x; }
	operator const FLOAT* () const { return &x; }
	D3DXVECTOR2 operator+(const D3DXVECTOR2 &v) const { return D3DXVECTOR2(x + v.x, y + v.y); }
	D3DXVECTOR2 operator-(const D3DXVECTOR2 &v) const { return D3DXVECTOR2(x - v.x, y - v.y); }
	D3DXVECTOR2 operator*(FLOAT f) const { return D3DXVECTOR2(x * f, y * f); }
};

struct D3DXVECTOR3 : public D3DVECTOR
{
	D3DXVECTOR3() {}
	D3DXVECTOR3(const FLOAT *f) { x = f[0]; y = f[1]; z = f[2]; }
	D3DXVECTOR3(const D3DVECTOR &v) { x = v.x; y = v.y; z = v.z; }
	D3DXVECTOR3(FLOAT fx, FLOAT fy, FLOAT fz) { x = fx; y = fy; z = fz; }
	operator FLOAT* () { return &x; }
	operator const FLOAT* () const { return &x; }
	D3DXVECTOR3 &operator+=(const D3DXVECTOR3 &v) { x += v.x; y += v.y; z += v.z; return *this; }
	D3DXVECTOR3 &operator-=(const D3DXVECTOR3 &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
	D3DXVECTOR3 &operator*=(FLOAT f) { x *= f; y *= f; z *= f; return *this; }
	D3DXVECTOR3 &operator/=(FLOAT f) { FLOAT i = 1.0f / f; x *= i; y *= i; z *= i; return *this; }
	D3DXVECTOR3 operator+() const { return *this; }
	D3DXVECTOR3 operator-() const { return D3DXVECTOR3(-x, -y, -z); }
	D3DXVECTOR3 operator+(const D3DXVECTOR3 &v) const { return D3DXVECTOR3(x + v.x, y + v.y, z + v.z); }
	D3DXVECTOR3 operator-(const D3DXVECTOR3 &v) const { return D3DXVECTOR3(x - v.x, y - v.y, z - v.z); }
	D3DXVECTOR3 operator*(FLOAT f) const { return D3DXVECTOR3(x * f, y * f, z * f); }
	D3DXVECTOR3 operator/(FLOAT f) const { FLOAT i = 1.0f / f; return D3DXVECTOR3(x * i, y * i, z * i); }
	friend D3DXVECTOR3 operator*(FLOAT f, const D3DXVECTOR3 &v) { return D3DXVECTOR3(f * v.x, f * v.y, f * v.z); }
	bool operator==(const D3DXVECTOR3 &v) const { return x == v.x && y == v.y && z == v.z; }
	bool operator!=(const D3DXVECTOR3 &v) const { return !(*this == v); }
};

struct D3DXVECTOR4
{
	FLOAT x, y, z, w;
	D3DXVECTOR4() {}
	D3DXVECTOR4(const FLOAT *f) : x(f[0]), y(f[1]), z(f[2]), w(f[3]) {}
	D3DXVECTOR4(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) : x(fx), y(fy), z(fz), w(fw) {}
	operator FLOAT* () { return &x; }
	operator const FLOAT* () const { return &x; }
	D3DXVECTOR4 &operator+=(const D3DXVECTOR4 &v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
	D3DXVECTOR4 &operator-=(const D3DXVECTOR4 &v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
	D3DXVECTOR4 &operator*=(FLOAT f) { x *= f; y *= f; z *= f; w *= f; return *this; }
	D3DXVECTOR4 &operator/=(FLOAT f) { FLOAT i = 1.0f / f; x *= i; y *= i; z *= i; w *= i; return *this; }
	D3DXVECTOR4 operator+() const { return *this; }
	D3DXVECTOR4 operator-() const { return D3DXVECTOR4(-x, -y, -z, -w); }
	D3DXVECTOR4 operator+(const D3DXVECTOR4 &v) const { return D3DXVECTOR4(x + v.x, y + v.y, z + v.z, w + v.w); }
	D3DXVECTOR4 operator-(const D3DXVECTOR4 &v) const { return D3DXVECTOR4(x - v.x, y - v.y, z - v.z, w - v.w); }
	D3DXVECTOR4 operator*(FLOAT f) const { return D3DXVECTOR4(x * f, y * f, z * f, w * f); }
	D3DXVECTOR4 operator/(FLOAT f) const { FLOAT i = 1.0f / f; return D3DXVECTOR4(x * i, y * i, z * i, w * i); }
	friend D3DXVECTOR4 operator*(FLOAT f, const D3DXVECTOR4 &v) { return D3DXVECTOR4(f * v.x, f * v.y, f * v.z, f * v.w); }
	bool operator==(const D3DXVECTOR4 &v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
	bool operator!=(const D3DXVECTOR4 &v) const { return !(*this == v); }
};

struct D3DXMATRIX : public D3DMATRIX
{
	D3DXMATRIX() {}
	D3DXMATRIX(const FLOAT *f) { for (int i = 0; i < 16; ++i) (&_11)[i] = f[i]; }
	D3DXMATRIX(const D3DMATRIX &m) { for (int i = 0; i < 16; ++i) (&_11)[i] = (&m._11)[i]; }
	D3DXMATRIX(FLOAT f11, FLOAT f12, FLOAT f13, FLOAT f14,
						 FLOAT f21, FLOAT f22, FLOAT f23, FLOAT f24,
						 FLOAT f31, FLOAT f32, FLOAT f33, FLOAT f34,
						 FLOAT f41, FLOAT f42, FLOAT f43, FLOAT f44)
	{
		_11 = f11; _12 = f12; _13 = f13; _14 = f14;
		_21 = f21; _22 = f22; _23 = f23; _24 = f24;
		_31 = f31; _32 = f32; _33 = f33; _34 = f34;
		_41 = f41; _42 = f42; _43 = f43; _44 = f44;
	}
	FLOAT &operator()(UINT row, UINT col) { return (&_11)[row * 4 + col]; }
	FLOAT operator()(UINT row, UINT col) const { return (&_11)[row * 4 + col]; }
	operator FLOAT* () { return &_11; }
	operator const FLOAT* () const { return &_11; }

	D3DXMATRIX operator*(const D3DXMATRIX &m) const
	{
		D3DXMATRIX r;
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				r(i, j) = (*this)(i, 0) * m(0, j) + (*this)(i, 1) * m(1, j) + (*this)(i, 2) * m(2, j) + (*this)(i, 3) * m(3, j);
		return r;
	}
	D3DXMATRIX &operator*=(const D3DXMATRIX &m) { *this = *this * m; return *this; }
	D3DXMATRIX &operator+=(const D3DXMATRIX &m) { for (int i = 0; i < 16; ++i) (&_11)[i] += (&m._11)[i]; return *this; }
	D3DXMATRIX &operator-=(const D3DXMATRIX &m) { for (int i = 0; i < 16; ++i) (&_11)[i] -= (&m._11)[i]; return *this; }
	D3DXMATRIX &operator*=(FLOAT f) { for (int i = 0; i < 16; ++i) (&_11)[i] *= f; return *this; }
	D3DXMATRIX &operator/=(FLOAT f) { FLOAT inv = 1.0f / f; return (*this *= inv); }
	D3DXMATRIX operator+() const { return *this; }
	D3DXMATRIX operator-() const { D3DXMATRIX r(*this); r *= -1.0f; return r; }
	D3DXMATRIX operator+(const D3DXMATRIX &m) const { D3DXMATRIX r(*this); r += m; return r; }
	D3DXMATRIX operator-(const D3DXMATRIX &m) const { D3DXMATRIX r(*this); r -= m; return r; }
	D3DXMATRIX operator*(FLOAT f) const { D3DXMATRIX r(*this); r *= f; return r; }
	D3DXMATRIX operator/(FLOAT f) const { D3DXMATRIX r(*this); r /= f; return r; }
	friend D3DXMATRIX operator*(FLOAT f, const D3DXMATRIX &m) { return m * f; }
	bool operator==(const D3DXMATRIX &m) const { for (int i = 0; i < 16; ++i) if ((&_11)[i] != (&m._11)[i]) return false; return true; }
	bool operator!=(const D3DXMATRIX &m) const { return !(*this == m); }
};

struct D3DXQUATERNION
{
	FLOAT x, y, z, w;
	D3DXQUATERNION() {}
	D3DXQUATERNION(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) : x(fx), y(fy), z(fz), w(fw) {}
};

struct D3DXPLANE
{
	FLOAT a, b, c, d;
	D3DXPLANE() {}
	D3DXPLANE(FLOAT fa, FLOAT fb, FLOAT fc, FLOAT fd) : a(fa), b(fb), c(fc), d(fd) {}
};

// ---- vectors -----------------------------------------------------------------------------------

inline FLOAT D3DXVec3Dot(const D3DXVECTOR3 *a, const D3DXVECTOR3 *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }
inline FLOAT D3DXVec3Length(const D3DXVECTOR3 *v) { return sqrtf(D3DXVec3Dot(v, v)); }
inline FLOAT D3DXVec3LengthSq(const D3DXVECTOR3 *v) { return D3DXVec3Dot(v, v); }
inline D3DXVECTOR3 *D3DXVec3Cross(D3DXVECTOR3 *out, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
	D3DXVECTOR3 r(a->y * b->z - a->z * b->y, a->z * b->x - a->x * b->z, a->x * b->y - a->y * b->x);
	*out = r;
	return out;
}
inline D3DXVECTOR3 *D3DXVec3Normalize(D3DXVECTOR3 *out, const D3DXVECTOR3 *v)
{
	FLOAT len = D3DXVec3Length(v);
	if (len == 0.0f) { out->x = out->y = out->z = 0.0f; return out; }
	*out = *v / len;
	return out;
}
inline FLOAT D3DXVec4Dot(const D3DXVECTOR4 *a, const D3DXVECTOR4 *b) { return a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w; }

inline D3DXVECTOR4 *D3DXVec4Transform(D3DXVECTOR4 *out, const D3DXVECTOR4 *v, const D3DXMATRIX *m)
{
	D3DXVECTOR4 r(
		v->x * m->_11 + v->y * m->_21 + v->z * m->_31 + v->w * m->_41,
		v->x * m->_12 + v->y * m->_22 + v->z * m->_32 + v->w * m->_42,
		v->x * m->_13 + v->y * m->_23 + v->z * m->_33 + v->w * m->_43,
		v->x * m->_14 + v->y * m->_24 + v->z * m->_34 + v->w * m->_44);
	*out = r;
	return out;
}
/// Transform (x, y, z, 1) by m.
inline D3DXVECTOR4 *D3DXVec3Transform(D3DXVECTOR4 *out, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
	D3DXVECTOR4 v4(v->x, v->y, v->z, 1.0f);
	return D3DXVec4Transform(out, &v4, m);
}
/// Transform (x, y, z, 1) by m and project back into w = 1.
inline D3DXVECTOR3 *D3DXVec3TransformCoord(D3DXVECTOR3 *out, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
	D3DXVECTOR4 r;
	D3DXVec3Transform(&r, v, m);
	FLOAT inv = (r.w != 0.0f) ? 1.0f / r.w : 1.0f;
	*out = D3DXVECTOR3(r.x * inv, r.y * inv, r.z * inv);
	return out;
}
/// Transform (x, y, z, 0) by m.
inline D3DXVECTOR3 *D3DXVec3TransformNormal(D3DXVECTOR3 *out, const D3DXVECTOR3 *v, const D3DXMATRIX *m)
{
	D3DXVECTOR3 r(
		v->x * m->_11 + v->y * m->_21 + v->z * m->_31,
		v->x * m->_12 + v->y * m->_22 + v->z * m->_32,
		v->x * m->_13 + v->y * m->_23 + v->z * m->_33);
	*out = r;
	return out;
}

// ---- matrices ----------------------------------------------------------------------------------

inline D3DXMATRIX *D3DXMatrixIdentity(D3DXMATRIX *out)
{
	*out = D3DXMATRIX(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	return out;
}
inline BOOL D3DXMatrixIsIdentity(const D3DXMATRIX *m)
{
	D3DXMATRIX i;
	D3DXMatrixIdentity(&i);
	return *m == i;
}
inline D3DXMATRIX *D3DXMatrixMultiply(D3DXMATRIX *out, const D3DXMATRIX *a, const D3DXMATRIX *b)
{
	*out = (*a) * (*b);
	return out;
}
inline D3DXMATRIX *D3DXMatrixTranspose(D3DXMATRIX *out, const D3DXMATRIX *m)
{
	D3DXMATRIX r;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			r(i, j) = (*m)(j, i);
	*out = r;
	return out;
}
inline D3DXMATRIX *D3DXMatrixScaling(D3DXMATRIX *out, FLOAT sx, FLOAT sy, FLOAT sz)
{
	*out = D3DXMATRIX(sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1);
	return out;
}
inline D3DXMATRIX *D3DXMatrixTranslation(D3DXMATRIX *out, FLOAT x, FLOAT y, FLOAT z)
{
	*out = D3DXMATRIX(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1);
	return out;
}
inline D3DXMATRIX *D3DXMatrixRotationX(D3DXMATRIX *out, FLOAT a)
{
	FLOAT c = cosf(a), s = sinf(a);
	*out = D3DXMATRIX(1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1);
	return out;
}
inline D3DXMATRIX *D3DXMatrixRotationY(D3DXMATRIX *out, FLOAT a)
{
	FLOAT c = cosf(a), s = sinf(a);
	*out = D3DXMATRIX(c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1);
	return out;
}
inline D3DXMATRIX *D3DXMatrixRotationZ(D3DXMATRIX *out, FLOAT a)
{
	FLOAT c = cosf(a), s = sinf(a);
	*out = D3DXMATRIX(c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	return out;
}
/// General 4x4 inverse (cofactor expansion).  Returns NULL if the matrix is singular.
D3DXMATRIX *D3DXMatrixInverse(D3DXMATRIX *out, FLOAT *determinant, const D3DXMATRIX *m);

#endif // ZH_D3DX8MATH_H
