/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// zh_intrin.h - portable replacements for x86 instructions that were written as inline assembly.
// MSVC inline assembly (__asm { ... }) does not exist on x64 or on other compilers; these wrappers
// map to compiler intrinsics instead.

#ifndef ZH_INTRIN_H
#define ZH_INTRIN_H

#if defined(_MSC_VER)
	#include <intrin.h>
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	#include <cpuid.h>
	#include <x86intrin.h>
#endif

/// Execute CPUID for the given leaf (sub-leaf 0).  regs = { eax, ebx, ecx, edx }.
inline void ZH_Cpuid(unsigned regs[4], unsigned leaf)
{
#if defined(_MSC_VER)
	int r[4];
	__cpuidex(r, (int)leaf, 0);
	regs[0] = (unsigned)r[0]; regs[1] = (unsigned)r[1]; regs[2] = (unsigned)r[2]; regs[3] = (unsigned)r[3];
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
	unsigned a, b, c, d;
	__cpuid_count(leaf, 0, a, b, c, d);
	regs[0] = a; regs[1] = b; regs[2] = c; regs[3] = d;
#else
	regs[0] = regs[1] = regs[2] = regs[3] = 0;
#endif
}

/// Every CPU that can run this game (and every x64 CPU) has CPUID.
inline bool ZH_HasCpuid()
{
#if defined(_MSC_VER) || (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
	return true;
#else
	return false;
#endif
}

/// Read the CPU time stamp counter.
inline unsigned long long ZH_Rdtsc()
{
#if defined(_MSC_VER) || (defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__)))
	return (unsigned long long)__rdtsc();
#else
	return 0;
#endif
}

#endif // ZH_INTRIN_H
