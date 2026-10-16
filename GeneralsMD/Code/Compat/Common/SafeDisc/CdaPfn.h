/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Stand-in for the SafeDisc copy protection API (not part of the source release; the retail
// copy protection was removed when the game was re-released).  The markers compile to nothing.
#ifndef ZH_CDAPFN_H
#define ZH_CDAPFN_H

#define CDAPFN_OVERHEAD_L1		1
#define CDAPFN_OVERHEAD_L2		2
#define CDAPFN_OVERHEAD_L3		3
#define CDAPFN_OVERHEAD_L4		4
#define CDAPFN_OVERHEAD_L5		5
#define CDAPFN_CONSTRAINT_NONE	0

#define CDAPFN_DECLARE_GLOBAL(func, overhead, constraint)
#define CDAPFN_ENDMARK(func)

#endif
