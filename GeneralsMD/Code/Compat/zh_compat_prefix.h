/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// zh_compat_prefix.h - force-included into every C++ translation unit (see CMakeLists.txt).
// Only fills gaps of specific toolchains; must stay free of game code dependencies.

#ifndef ZH_COMPAT_PREFIX_H
#define ZH_COMPAT_PREFIX_H

// ATL (used only by the embedded web browser of the online lobby, which the game never creates)
// is available with MSVC only.
#ifndef ZH_HAS_ATL
	#if defined(_MSC_VER) && !defined(ZH_NO_ATL)
		#define ZH_HAS_ATL 1
	#else
		#define ZH_HAS_ATL 0
	#endif
#endif

#if defined(__MINGW32__)
	// Direct3D 8 constants that MinGW-w64's d3d8 headers do not define.
	#ifndef D3DPRESENT_RATE_DEFAULT
	#define D3DPRESENT_RATE_DEFAULT	0x00000000
	#endif
	// _set_se_translator (MSVC structured exception translation) is declared in <eh.h>, which
	// MSVC pulls in implicitly.  With GCC it has no effect; crashes are still reported by the
	// unhandled exception filter.
	#include <eh.h>

	// MSVC accepts _int64 as a synonym of __int64.
	#ifndef _int64
	#define _int64 __int64
	#endif

	#ifndef D3DCURSOR_IMMEDIATE_UPDATE
	#define D3DCURSOR_IMMEDIATE_UPDATE	0x00000001L
	#endif
	#ifndef D3DSGR_NO_CALIBRATION
	#define D3DSGR_NO_CALIBRATION	0x00000000L
	#define D3DSGR_CALIBRATE		0x00000001L
	#endif
#endif

#endif // ZH_COMPAT_PREFIX_H
