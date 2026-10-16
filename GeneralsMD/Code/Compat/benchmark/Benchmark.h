/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the BYTEmark benchmark library (not part of the source release).
// The game only uses the scores to pick default detail settings; see benchmark_stub.cpp.
#ifndef ZH_BENCHMARK_H
#define ZH_BENCHMARK_H

int RunBenchmark(int argc, char *argv[], float *floatIndex, float *intIndex, float *memIndex);

#endif
