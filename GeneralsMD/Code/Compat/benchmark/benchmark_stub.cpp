/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for BYTEmark's RunBenchmark.  The original measured integer, floating point and memory
// speed relative to a 2003-era reference machine to choose default detail settings.  Every
// computer that can run this build is far beyond the highest preset's thresholds, so report
// correspondingly high scores instead of spending seconds measuring.
#include "Benchmark.h"

int RunBenchmark(int, char *[], float *floatIndex, float *intIndex, float *memIndex)
{
	if (floatIndex) *floatIndex = 100.0f;
	if (intIndex) *intIndex = 100.0f;
	if (memIndex) *memIndex = 100.0f;
	return 0;
}
