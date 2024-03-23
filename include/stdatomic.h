#ifndef __STDATOMIC_H
#define __STDATOMIC_H

// __builtin_compare_and_swap(pa, pb, c_val)
// if (*pa == *pb) then *pa = c_val, return true
// else *pb = *pa, return false
#define atomic_compare_exchange_weak(p, old, new) \
	__builtin_compare_and_swap((p), (old), (new))
#define atomic_compare_exchange_strong(p, old, new) \
	__builtin_compare_and_swap((p), (old), (new))

#define atomic_exchange(obj, val) \
	__builtin_atomic_exchange(obj, val)
#define atomic_exchange_explicit(obj, val, order) \
	__builtin_atomic_exchange(obj, val)

#endif
