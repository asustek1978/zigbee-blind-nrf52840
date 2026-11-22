/* Host-only shim for ledger unit tests; NOT included in target firmware. */
#pragma once
struct k_spinlock { int unused; };
typedef int k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *lock)
{
    (void)lock;
    return 0;
}
static inline void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key)
{
    (void)lock;
    (void)key;
}
