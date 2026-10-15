/*
**	Command & Conquer Generals Zero Hour(tm)
**	Modified version - not the original program.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

///////////////////////////////////////////////////////////////////////////////////////////////////
// detmath.h
//
// Deterministic transcendental math for the game simulation.
//
// Why this exists:
//   The lockstep multiplayer model requires every machine to compute bit-identical results.
//   The C runtime's sin/cos/atan2/acos/pow/... are NOT guaranteed to produce identical results
//   across CPUs or runtime versions (modern CRTs pick different code paths at run time depending
//   on CPU features such as FMA), and the x87 FSIN/FCOS instructions give different answers on
//   different CPU vendors.  Any of these differences eventually shows up as a CRC mismatch.
//
//   Every function here is built ONLY from IEEE-754 basic operations (+, -, *, /, sqrt) plus the
//   exact helpers floor/frexp/ldexp/fmod.  IEEE-754 requires those to be correctly rounded, so
//   two machines running the same executable with the same FPU mode (see setFPMode) get the same
//   bits back, regardless of CPU vendor, CRT version or OS.
//
//   Accuracy is close to the CRT (around 1 ulp in double, exact to float precision in practice),
//   but bit-for-bit equality with the CRT is NOT a goal - consistency between machines is.
//
// Rules for simulation (GameLogic / Common / WWMath) code:
//   * never call sin/cos/tan/asin/acos/atan/atan2/exp/log/pow (or their *f versions) directly;
//     use DetMath:: instead.
//   * sqrt, fabs, floor, ceil and fmod are exact operations and are safe to call directly.
//   * build with /fp:precise (never /fp:fast), and do not enable FMA contraction.
//
// This header is intentionally free of game-specific types so it can be unit tested standalone.
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef DETMATH_H
#define DETMATH_H

#include <math.h>

class DetMath
{
public:
	// ---- double precision ----------------------------------------------------------------------
	static double Sin(double x);
	static double Cos(double x);
	static double Tan(double x);
	static double Asin(double x);
	static double Acos(double x);
	static double Atan(double x);
	static double Atan2(double y, double x);
	static double Exp(double x);
	static double Log(double x);
	static double Log10(double x);
	static double Pow(double x, double y);

	// ---- single precision (computed in double, then rounded once) ------------------------------
	static float Sinf(float x)						{ return (float)Sin((double)x); }
	static float Cosf(float x)						{ return (float)Cos((double)x); }
	static float Tanf(float x)						{ return (float)Tan((double)x); }
	static float Asinf(float x)						{ return (float)Asin((double)x); }
	static float Acosf(float x)						{ return (float)Acos((double)x); }
	static float Atanf(float x)						{ return (float)Atan((double)x); }
	static float Atan2f(float y, float x)	{ return (float)Atan2((double)y, (double)x); }
	static float Expf(float x)						{ return (float)Exp((double)x); }
	static float Logf(float x)						{ return (float)Log((double)x); }
	static float Log10f(float x)					{ return (float)Log10((double)x); }
	static float Powf(float x, float y)		{ return (float)Pow((double)x, (double)y); }

private:
	static double PiValue()					{ return 3.14159265358979311600e+00; }
	static double HalfPiValue()			{ return 1.57079632679489655800e+00; }
	static double QuarterPiValue()	{ return 7.85398163397448278999e-01; }

	// NaN/Inf tests by bit pattern: older MSVC compilers mis-compile "x != x" for NaN.
	static void HighLow(double x, unsigned int &hi, unsigned int &lo)
	{
		union { double d; unsigned int w[2]; } u;	// little endian: w[1] holds sign/exponent
		u.d = x;
		hi = u.w[1];
		lo = u.w[0];
	}
	static bool IsNaN(double x)
	{
		unsigned int hi, lo;
		HighLow(x, hi, lo);
		return ((hi & 0x7ff00000u) == 0x7ff00000u) && (((hi & 0x000fffffu) | lo) != 0);
	}
	static bool IsInf(double x)
	{
		unsigned int hi, lo;
		HighLow(x, hi, lo);
		return ((hi & 0x7fffffffu) == 0x7ff00000u) && (lo == 0);
	}
	static double NaN()
	{
		volatile double zero = 0.0;
		return zero / zero;
	}
	static double Infinity()
	{
		volatile double zero = 0.0;
		return 1.0 / zero;
	}
	static bool SignBit(double x)
	{
		// distinguishes -0.0 from +0.0 without relying on the CRT
		union { double d; unsigned char b[sizeof(double)]; } u;
		u.d = x;
		// little endian (x86/x64/ARM in the configurations we target)
		return (u.b[sizeof(double) - 1] & 0x80) != 0;
	}
	static double CopySign(double mag, double sgn)
	{
		if (mag < 0.0) mag = -mag;
		return SignBit(sgn) ? -mag : mag;
	}

	// round to nearest integer value (ties away from zero), exact
	static double RoundNearest(double x)
	{
		return (x >= 0.0) ? floor(x + 0.5) : -floor(-x + 0.5);
	}

	// Reduce x to r in about [-pi/4, pi/4] with x = n * pi/2 + r.  Returns n mod 4 in quadrant.
	static double ReduceHalfPi(double x, int &quadrant);

	// polynomial kernels valid for |r| <= pi/4
	static double KernelSin(double r);
	static double KernelCos(double r);

	// atan for 0 <= x <= 1
	static double KernelAtan(double x);
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation
///////////////////////////////////////////////////////////////////////////////////////////////////

inline double DetMath::ReduceHalfPi(double x, int &quadrant)
{
	// pi/2 split into pieces whose products with n (|n| < 2^20) are exact (fdlibm constants).
	const double invPio2	= 6.36619772367581382433e-01;	// 2/pi
	const double pio2_1		= 1.57079632673412561417e+00;	// first 33 bits of pi/2
	const double pio2_2		= 6.07710050630396597660e-11;	// next 33 bits
	const double pio2_2t	= 2.02226624879595063154e-21;	// pi/2 - (pio2_1 + pio2_2)

	// Arguments in the game are small angles; anything enormous is folded with an exact fmod
	// so the multiply below cannot overflow the integer range.
	if (x > 1.0e8 || x < -1.0e8)
	{
		x = fmod(x, 6.28318530717958623200e+00);
	}

	double fn = RoundNearest(x * invPio2);
	double r = x - fn * pio2_1;
	r = r - fn * pio2_2;
	r = r - fn * pio2_2t;

	int n = (int)fn;
	quadrant = n & 3;	// two's complement: also correct for negative n
	return r;
}

inline double DetMath::KernelSin(double r)
{
	// Taylor series through r^19; truncation error < 1e-19 for |r| <= pi/4
	const double s3		= -1.66666666666666657415e-01;	// -1/3!
	const double s5		=  8.33333333333333321769e-03;	//  1/5!
	const double s7		= -1.98412698412698412526e-04;	// -1/7!
	const double s9		=  2.75573192239858925110e-06;	//  1/9!
	const double s11	= -2.50521083854417202239e-08;	// -1/11!
	const double s13	=  1.60590438368216133258e-10;	//  1/13!
	const double s15	= -7.64716373181981641708e-13;	// -1/15!
	const double s17	=  2.81145725434552060745e-15;	//  1/17!
	const double s19	= -8.22063524662432971696e-18;	// -1/19!
	double z = r * r;
	double p = s19;
	p = p * z + s17;
	p = p * z + s15;
	p = p * z + s13;
	p = p * z + s11;
	p = p * z + s9;
	p = p * z + s7;
	p = p * z + s5;
	p = p * z + s3;
	return r + r * z * p;
}

inline double DetMath::KernelCos(double r)
{
	// Taylor series through r^20
	const double c2		= -5.00000000000000000000e-01;	// -1/2!
	const double c4		=  4.16666666666666643537e-02;	//  1/4!
	const double c6		= -1.38888888888888894189e-03;	// -1/6!
	const double c8		=  2.48015873015873015658e-05;	//  1/8!
	const double c10	= -2.75573192239858882758e-07;	// -1/10!
	const double c12	=  2.08767569878680989792e-09;	//  1/12!
	const double c14	= -1.14707455977297247139e-11;	// -1/14!
	const double c16	=  4.77947733238738529744e-14;	//  1/16!
	const double c18	= -1.56192069685862264405e-16;	// -1/18!
	const double c20	=  4.11031762331216485848e-19;	//  1/20!
	double z = r * r;
	double p = c20;
	p = p * z + c18;
	p = p * z + c16;
	p = p * z + c14;
	p = p * z + c12;
	p = p * z + c10;
	p = p * z + c8;
	p = p * z + c6;
	p = p * z + c4;
	p = p * z + c2;
	return 1.0 + z * p;
}

inline double DetMath::Sin(double x)
{
	if (IsNaN(x) || IsInf(x))
		return NaN();

	int q;
	double r = ReduceHalfPi(x, q);
	switch (q)
	{
		case 0:		return KernelSin(r);
		case 1:		return KernelCos(r);
		case 2:		return -KernelSin(r);
		default:	return -KernelCos(r);
	}
}

inline double DetMath::Cos(double x)
{
	if (IsNaN(x) || IsInf(x))
		return NaN();

	int q;
	double r = ReduceHalfPi(x, q);
	switch (q)
	{
		case 0:		return KernelCos(r);
		case 1:		return -KernelSin(r);
		case 2:		return -KernelCos(r);
		default:	return KernelSin(r);
	}
}

inline double DetMath::Tan(double x)
{
	if (IsNaN(x) || IsInf(x))
		return NaN();

	int q;
	double r = ReduceHalfPi(x, q);
	double s = KernelSin(r);
	double c = KernelCos(r);
	// tan has period pi: odd quadrants are -cot(r)
	if (q & 1)
		return -c / s;
	return s / c;
}

inline double DetMath::KernelAtan(double x)
{
	// 0 <= x <= 1.  Two applications of atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))) bring the
	// argument down to <= tan(pi/16) ~= 0.199, where the Taylor series through x^27 converges to
	// well below double precision.  Only +,-,*,/ and sqrt are used.
	double t = x / (1.0 + sqrt(1.0 + x * x));
	t = t / (1.0 + sqrt(1.0 + t * t));

	double z = t * t;
	double p = 1.0 / 27.0;
	p = 1.0 / 25.0 - z * p;
	p = 1.0 / 23.0 - z * p;
	p = 1.0 / 21.0 - z * p;
	p = 1.0 / 19.0 - z * p;
	p = 1.0 / 17.0 - z * p;
	p = 1.0 / 15.0 - z * p;
	p = 1.0 / 13.0 - z * p;
	p = 1.0 / 11.0 - z * p;
	p = 1.0 / 9.0 - z * p;
	p = 1.0 / 7.0 - z * p;
	p = 1.0 / 5.0 - z * p;
	p = 1.0 / 3.0 - z * p;
	double a = t - t * z * p;
	return 4.0 * a;
}

inline double DetMath::Atan(double x)
{
	if (IsNaN(x))
		return x;

	bool negative = x < 0.0;
	double ax = negative ? -x : x;
	double result;
	if (ax <= 1.0)
	{
		result = KernelAtan(ax);
	}
	else
	{
		// atan(x) = pi/2 - atan(1/x); 1/inf == 0 handles infinity
		result = HalfPiValue() - KernelAtan(1.0 / ax);
	}
	return negative ? -result : result;
}

inline double DetMath::Atan2(double y, double x)
{
	if (IsNaN(x) || IsNaN(y))
		return x + y;

	if (y == 0.0)
	{
		// atan2(+-0, x>0 or +0) = +-0, atan2(+-0, x<0 or -0) = +-pi
		if (x < 0.0 || (x == 0.0 && SignBit(x)))
			return CopySign(PiValue(), y);
		return y;
	}

	if (x == 0.0)
		return (y > 0.0) ? HalfPiValue() : -HalfPiValue();

	bool xInf = IsInf(x);
	bool yInf = IsInf(y);
	if (xInf && yInf)
	{
		double a = (x > 0.0) ? QuarterPiValue() : 3.0 * QuarterPiValue();
		return (y > 0.0) ? a : -a;
	}

	double ax = x < 0.0 ? -x : x;
	double ay = y < 0.0 ? -y : y;
	double a;
	if (ay <= ax)
		a = KernelAtan(ay / ax);
	else
		a = HalfPiValue() - KernelAtan(ax / ay);

	if (x < 0.0)
		a = PiValue() - a;
	return (y < 0.0) ? -a : a;
}

inline double DetMath::Asin(double x)
{
	if (IsNaN(x) || x > 1.0 || x < -1.0)
		return NaN();
	// asin(x) = atan2(x, sqrt(1 - x^2)); (1-x)(1+x) keeps precision near |x| == 1
	return Atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}

inline double DetMath::Acos(double x)
{
	if (IsNaN(x) || x > 1.0 || x < -1.0)
		return NaN();
	return Atan2(sqrt((1.0 - x) * (1.0 + x)), x);
}

inline double DetMath::Exp(double x)
{
	if (IsNaN(x))
		return x;
	if (x > 709.782712893384)
		return Infinity();
	if (x < -745.1332191019411)
		return 0.0;

	const double invLn2	= 1.44269504088896338700e+00;
	const double ln2Hi	= 6.93147180369123816490e-01;	// exact in 32 bits
	const double ln2Lo	= 1.90821492927058770002e-10;

	double fn = RoundNearest(x * invLn2);
	double r = (x - fn * ln2Hi) - fn * ln2Lo;	// |r| <= ~0.347

	// Taylor series of e^r through r^20 (Horner)
	double p = 1.0 / 2432902008176640000.0;	// 1/20!
	p = p * r + 1.0 / 121645100408832000.0;	// 1/19!
	p = p * r + 1.0 / 6402373705728000.0;
	p = p * r + 1.0 / 355687428096000.0;
	p = p * r + 1.0 / 20922789888000.0;
	p = p * r + 1.0 / 1307674368000.0;
	p = p * r + 1.0 / 87178291200.0;
	p = p * r + 1.0 / 6227020800.0;
	p = p * r + 1.0 / 479001600.0;
	p = p * r + 1.0 / 39916800.0;
	p = p * r + 1.0 / 3628800.0;
	p = p * r + 1.0 / 362880.0;
	p = p * r + 1.0 / 40320.0;
	p = p * r + 1.0 / 5040.0;
	p = p * r + 1.0 / 720.0;
	p = p * r + 1.0 / 120.0;
	p = p * r + 1.0 / 24.0;
	p = p * r + 1.0 / 6.0;
	p = p * r + 0.5;
	p = p * r + 1.0;
	p = p * r + 1.0;

	return ldexp(p, (int)fn);	// exact scaling by 2^n
}

inline double DetMath::Log(double x)
{
	if (IsNaN(x) || x < 0.0)
		return NaN();
	if (x == 0.0)
		return -Infinity();
	if (IsInf(x))
		return x;

	const double ln2Hi		= 6.93147180369123816490e-01;
	const double ln2Lo		= 1.90821492927058770002e-10;
	const double sqrtHalf	= 7.07106781186547572737e-01;

	int e;
	double m = frexp(x, &e);	// exact: x = m * 2^e, 0.5 <= m < 1
	if (m < sqrtHalf)
	{
		m = m * 2.0;
		e = e - 1;
	}

	// log(m) = 2 atanh(s), s = (m-1)/(m+1), |s| <= 0.1716
	double s = (m - 1.0) / (m + 1.0);
	double z = s * s;
	double p = 1.0 / 27.0;
	p = p * z + 1.0 / 25.0;
	p = p * z + 1.0 / 23.0;
	p = p * z + 1.0 / 21.0;
	p = p * z + 1.0 / 19.0;
	p = p * z + 1.0 / 17.0;
	p = p * z + 1.0 / 15.0;
	p = p * z + 1.0 / 13.0;
	p = p * z + 1.0 / 11.0;
	p = p * z + 1.0 / 9.0;
	p = p * z + 1.0 / 7.0;
	p = p * z + 1.0 / 5.0;
	p = p * z + 1.0 / 3.0;
	double logm = 2.0 * s + 2.0 * s * z * p;

	double fe = (double)e;
	return fe * ln2Hi + (logm + fe * ln2Lo);
}

inline double DetMath::Log10(double x)
{
	const double invLn10 = 4.34294481903251816668e-01;
	return Log(x) * invLn10;
}

inline double DetMath::Pow(double x, double y)
{
	if (y == 0.0)
		return 1.0;
	if (IsNaN(x) || IsNaN(y))
		return x + y;
	if (x == 1.0)
		return 1.0;

	bool yIsInteger = (floor(y) == y) && !IsInf(y);

	// Small integer exponents: exact repeated squaring (also what game code mostly uses, e.g. x^2)
	if (yIsInteger && y <= 1024.0 && y >= -1024.0)
	{
		int n = (int)y;
		bool invert = n < 0;
		if (invert)
			n = -n;
		double base = x;
		double result = 1.0;
		while (n != 0)
		{
			if (n & 1)
				result = result * base;
			base = base * base;
			n = n >> 1;
		}
		return invert ? 1.0 / result : result;
	}

	if (x == 0.0)
		return (y > 0.0) ? 0.0 : Infinity();

	if (x < 0.0)
	{
		if (!yIsInteger)
			return NaN();
		// large integer exponent of a negative base: sign depends on parity
		double half = y * 0.5;
		bool odd = floor(half) != half;
		double mag = Exp(y * Log(-x));
		return odd ? -mag : mag;
	}

	return Exp(y * Log(x));
}

#endif // DETMATH_H
