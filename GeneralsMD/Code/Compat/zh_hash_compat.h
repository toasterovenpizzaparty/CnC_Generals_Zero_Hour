/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// zh_hash_compat.h - std::hash_map / hash_set (STLport/SGI extensions) on top of the standard
// unordered containers.
//
// Important difference handled here: STLport's hash<const char*> hashed the *characters*, while
// std::hash<const char*> hashes the *pointer value*.  Code written for STLport relies on the
// former (e.g. maps keyed by string).  Pointer hashing would also make iteration order depend on
// memory addresses, i.e. differ between machines - fatal for the deterministic simulation.

#ifndef ZH_HASH_COMPAT_H
#define ZH_HASH_COMPAT_H

#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <stddef.h>

namespace zh
{
	/// STLport's __stl_hash_string, with 32 bit arithmetic so 32 and 64-bit builds agree.
	inline size_t hash_cstring(const char *s)
	{
		unsigned int h = 0;
		for ( ; *s; ++s)
			h = 5 * h + (unsigned int)(int)*s;	// *s is a (signed) char, as in STLport
		return (size_t)h;
	}

	struct cstring_hash
	{
		size_t operator()(const char *s) const { return hash_cstring(s); }
	};

	// Map the hasher named in old code to one with STLport semantics.
	template <class H> struct fix_hasher { typedef H type; };
	template <> struct fix_hasher< std::hash<const char *> > { typedef cstring_hash type; };
	template <> struct fix_hasher< std::hash<char *> > { typedef cstring_hash type; };

	template <class A, class T> struct rebind
	{
		typedef typename std::allocator_traits<A>::template rebind_alloc<T> type;
	};
}

namespace std
{
	template <class K, class V, class H = std::hash<K>, class E = std::equal_to<K>,
		class A = std::allocator< std::pair<const K, V> > >
	using hash_map = std::unordered_map<K, V, typename zh::fix_hasher<H>::type, E,
		typename zh::rebind<A, std::pair<const K, V> >::type>;

	template <class K, class V, class H = std::hash<K>, class E = std::equal_to<K>,
		class A = std::allocator< std::pair<const K, V> > >
	using hash_multimap = std::unordered_multimap<K, V, typename zh::fix_hasher<H>::type, E,
		typename zh::rebind<A, std::pair<const K, V> >::type>;

	template <class K, class H = std::hash<K>, class E = std::equal_to<K>, class A = std::allocator<K> >
	using hash_set = std::unordered_set<K, typename zh::fix_hasher<H>::type, E, typename zh::rebind<A, K>::type>;

	template <class K, class H = std::hash<K>, class E = std::equal_to<K>, class A = std::allocator<K> >
	using hash_multiset = std::unordered_multiset<K, typename zh::fix_hasher<H>::type, E, typename zh::rebind<A, K>::type>;
}

#endif // ZH_HASH_COMPAT_H
