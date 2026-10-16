// Standalone test for DetMath (deterministic math used by the simulation).
//
//   1. accuracy against the platform libm (double and float)
//   2. prints a hash over a large set of results; build this file with different compilers,
//      optimization levels and CPUs: the hash must be identical everywhere.
//
// Build:  g++ -O2 -ffp-contract=off -I../../Libraries/Source/WWVegas/WWMath detmath_test.cpp -o detmath_test
//         cl /O2 /fp:precise /I..\..\Libraries\Source\WWVegas\WWMath detmath_test.cpp

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "detmath.h"

static unsigned long long g_hash = 1469598103934665603ULL;	// FNV-1a 64

static void hashDouble(double d)
{
	unsigned char b[sizeof(double)];
	memcpy(b, &d, sizeof(d));
	for (unsigned i = 0; i < sizeof(d); ++i)
	{
		g_hash ^= b[i];
		g_hash *= 1099511628211ULL;
	}
}

static void hashFloat(float f)
{
	unsigned char b[sizeof(float)];
	memcpy(b, &f, sizeof(f));
	for (unsigned i = 0; i < sizeof(f); ++i)
	{
		g_hash ^= b[i];
		g_hash *= 1099511628211ULL;
	}
}

// relative error in units of double epsilon; absolute near zero
static double errUlps(double got, double ref)
{
	if (ref != ref) return (got != got) ? 0.0 : 1e30;
	if (got != got) return 1e30;
	if (ref == got) return 0.0;
	double scale = fabs(ref);
	if (scale < 1e-300) scale = 1e-300;
	if (scale < 1e-12) scale = 1e-12;  // absolute error near zeros of the function
	return fabs(got - ref) / scale / 2.220446049250313e-16;
}

struct Stat { const char *name; double maxUlps; double worstX; int floatMismatch; int n; };

static void report(Stat &s)
{
	printf("  %-6s n=%-7d max err %8.2f ulp (double) at x=%-14.6g float results != correctly-rounded: %d\n",
		s.name, s.n, s.maxUlps, s.worstX, s.floatMismatch);
}

static int failures = 0;
static void check(Stat &s, double limitUlps)
{
	report(s);
#if defined(__GLIBC__)
	// The accuracy limit is only enforced against glibc, whose libm is accurate to <1 ulp.  Other C
	// runtimes are not a usable reference: msvcrt's sin/cos/pow lose 10+ ulp for large arguments
	// (which is exactly why the game must not use them).  The determinism hash below is what
	// matters on every platform.
	if (s.maxUlps > limitUlps) { printf("  FAIL: %s exceeds %g ulp\n", s.name, limitUlps); ++failures; }
#endif
}

int main()
{
	const int N = 200000;
	Stat ssin = {"sin",0,0,0,0}, scos = {"cos",0,0,0,0}, stan = {"tan",0,0,0,0}, satan = {"atan",0,0,0,0};
	Stat sasin = {"asin",0,0,0,0}, sacos = {"acos",0,0,0,0}, sexp = {"exp",0,0,0,0}, slog = {"log",0,0,0,0};
	Stat spow = {"pow",0,0,0,0}, satan2 = {"atan2",0,0,0,0};

	// deterministic pseudo-random sequence (no rand(): must be identical everywhere)
	unsigned int seed = 12345u;
	for (int i = 0; i < N; ++i)
	{
		seed = seed * 1664525u + 1013904223u;
		double u = (double)(seed >> 8) / 16777216.0;	// [0,1)
		seed = seed * 1664525u + 1013904223u;
		double v = (double)(seed >> 8) / 16777216.0;

		double a = (u - 0.5) * 200.0;		// angles in [-100, 100]
		double t = u * 2.0 - 1.0;			// [-1, 1]
		double e = (u - 0.5) * 100.0;		// exp args
		double l = u * 1000.0 + 1e-6;		// log args
		double py = (v - 0.5) * 8.0;		// pow exponent

		double r;
		r = DetMath::Sin(a);  hashDouble(r); { double x = errUlps(r, sin(a)); if (x > ssin.maxUlps) { ssin.maxUlps = x; ssin.worstX = a; } } ssin.n++;
		r = DetMath::Cos(a);  hashDouble(r); { double x = errUlps(r, cos(a)); if (x > scos.maxUlps) { scos.maxUlps = x; scos.worstX = a; } } scos.n++;
		r = DetMath::Tan(a);  hashDouble(r); { double x = errUlps(r, tan(a)); if (x > stan.maxUlps) { stan.maxUlps = x; stan.worstX = a; } } stan.n++;
		r = DetMath::Atan(a); hashDouble(r); { double x = errUlps(r, atan(a)); if (x > satan.maxUlps) { satan.maxUlps = x; satan.worstX = a; } } satan.n++;
		r = DetMath::Asin(t); hashDouble(r); { double x = errUlps(r, asin(t)); if (x > sasin.maxUlps) { sasin.maxUlps = x; sasin.worstX = t; } } sasin.n++;
		r = DetMath::Acos(t); hashDouble(r); { double x = errUlps(r, acos(t)); if (x > sacos.maxUlps) { sacos.maxUlps = x; sacos.worstX = t; } } sacos.n++;
		r = DetMath::Exp(e);  hashDouble(r); { double x = errUlps(r, exp(e)); if (x > sexp.maxUlps) { sexp.maxUlps = x; sexp.worstX = e; } } sexp.n++;
		r = DetMath::Log(l);  hashDouble(r); { double x = errUlps(r, log(l)); if (x > slog.maxUlps) { slog.maxUlps = x; slog.worstX = l; } } slog.n++;
		r = DetMath::Pow(l, py); hashDouble(r); { double x = errUlps(r, pow(l, py)); if (x > spow.maxUlps) { spow.maxUlps = x; spow.worstX = l; } } spow.n++;
		double ay = (u - 0.5) * 50.0, ax = (v - 0.5) * 50.0;
		r = DetMath::Atan2(ay, ax); hashDouble(r); { double x = errUlps(r, atan2(ay, ax)); if (x > satan2.maxUlps) { satan2.maxUlps = x; satan2.worstX = ay; } } satan2.n++;

		// float API, as used by the simulation
		float fa = (float)a, ft = (float)t;
		float f;
		f = DetMath::Sinf(fa);  hashFloat(f); if (f != (float)sin((double)fa)) ssin.floatMismatch++;
		f = DetMath::Cosf(fa);  hashFloat(f); if (f != (float)cos((double)fa)) scos.floatMismatch++;
		f = DetMath::Acosf(ft); hashFloat(f); if (f != (float)acos((double)ft)) sacos.floatMismatch++;
		f = DetMath::Atan2f((float)ay, (float)ax); hashFloat(f); if (f != (float)atan2((double)(float)ay, (double)(float)ax)) satan2.floatMismatch++;
	}

	printf("DetMath accuracy vs platform libm:\n");
	check(ssin, 4); check(scos, 4); check(stan, 8); check(satan, 4); check(satan2, 4);
	check(sasin, 8); check(sacos, 8); check(sexp, 4); check(slog, 4); check(spow, 64);

	// special values
	struct { const char *what; double got; double want; } sp[] = {
		{"atan2(0,-1)", DetMath::Atan2(0.0, -1.0), 3.14159265358979311600},
		{"atan2(-0,-1)", DetMath::Atan2(-0.0, -1.0), -3.14159265358979311600},
		{"atan2(1,0)", DetMath::Atan2(1.0, 0.0), 1.57079632679489655800},
		{"atan2(0,0)", DetMath::Atan2(0.0, 0.0), 0.0},
		{"acos(1)", DetMath::Acos(1.0), 0.0},
		{"acos(-1)", DetMath::Acos(-1.0), 3.14159265358979311600},
		{"asin(1)", DetMath::Asin(1.0), 1.57079632679489655800},
		{"pow(2,10)", DetMath::Pow(2.0, 10.0), 1024.0},
		{"pow(-2,3)", DetMath::Pow(-2.0, 3.0), -8.0},
		{"pow(9,0.5)", DetMath::Pow(9.0, 0.5), 3.0},
		{"exp(0)", DetMath::Exp(0.0), 1.0},
		{"log(1)", DetMath::Log(1.0), 0.0},
		{"sin(0)", DetMath::Sin(0.0), 0.0},
		{"cos(0)", DetMath::Cos(0.0), 1.0},
	};
	for (unsigned i = 0; i < sizeof(sp) / sizeof(sp[0]); ++i)
	{
		hashDouble(sp[i].got);
		if (errUlps(sp[i].got, sp[i].want) > 4)
		{
			printf("  FAIL special: %s = %.17g, want %.17g\n", sp[i].what, sp[i].got, sp[i].want);
			++failures;
		}
	}
	if (!(DetMath::Acos(1.5) != DetMath::Acos(1.5))) { printf("  FAIL acos(1.5) should be NaN\n"); ++failures; }

	printf("RESULT HASH: %016llx  (must be identical on every compiler/CPU/optimization level)\n", g_hash);
	printf("%s\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED");
	return failures ? 1 : 0;
}
