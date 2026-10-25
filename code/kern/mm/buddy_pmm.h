#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* lab2 2412819: buddy_check is supplied by the independent test module. */
extern const struct pmm_manager buddy_pmm_manager;
void buddy_check(void);

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
