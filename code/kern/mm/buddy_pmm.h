#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* lab2 2412819: requests reserve the next power-of-two number of pages.
 * Free only the returned head, using the exact original request count.
 * The manager supports the current 128 MiB DRAM window (orders 0 through 15).
 * Calls are serial; arbitrary subrange frees and concurrent calls are unsupported.
 */
extern const struct pmm_manager buddy_pmm_manager;

/* Implemented independently in buddy_check.c, not by the allocator. */
void buddy_check(void);

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
