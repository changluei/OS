#include <proc.h>
#include <kmalloc.h>
#include <kmonitor.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

//lab4 请补充你在lab4中的代码

//lab5 function2 2412439 update alloc_proc
//LAB4 function2 2412439
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        memset(proc, 0, sizeof(struct proc_struct));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
        list_init(&(proc->list_link));
        list_init(&(proc->hash_link));
    }
    return proc;
}


// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}






// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

//lab5 function2 2412439 update kernel_thread
//LAB4 function2 2412819
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

//LAB4 function2 2412819
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    uintptr_t tf_addr = (proc->kstack + KSTACKSIZE - sizeof(struct trapframe)) &
                        ~(uintptr_t)0xF;
    proc->tf = (struct trapframe *)tf_addr;
    *proc->tf = *tf;
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = esp == 0 ? tf_addr : esp;

    memset(&proc->context, 0, sizeof(proc->context));
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = tf_addr;
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

//lab5 function3 YOUR CODE update copy_mm
//LAB4 function2 2412819
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    (void)clone_flags;
    if (current->mm != NULL)
    {
        return -E_INVAL;
    }
    proc->mm = NULL;
    proc->pgdir = boot_pgdir_pa;
    return 0;
}



//lab5 function2 2412439
static bool
mapped_mem_check(struct mm_struct *mm, uintptr_t addr, size_t len, bool write)
{
    if (!user_mem_check(mm, addr, len, write))
    {
        return 0;
    }
    if (mm != NULL)
    {
        // A stack VMA includes pages beyond the initially mapped user stack.
        for (uintptr_t va = ROUNDDOWN(addr, PGSIZE); va < addr + len; va += PGSIZE)
        {
            pte_t *pte = get_pte(mm->pgdir, va, 0);
            if (pte == NULL || (*pte & (PTE_V | PTE_U)) != (PTE_V | PTE_U))
            {
                return 0;
            }
        }
    }
    return 1;
}

static int
load_elf_segment(struct mm_struct *mm, const unsigned char *binary,
                 const struct proghdr *ph)
{
    uint32_t vm_flags = 0, perm = PTE_U;
    if (ph->p_flags & ELF_PF_R)
    {
        vm_flags |= VM_READ;
        perm |= PTE_R;
    }
    if (ph->p_flags & ELF_PF_W)
    {
        vm_flags |= VM_WRITE;
        perm |= PTE_R | PTE_W;
    }
    if (ph->p_flags & ELF_PF_X)
    {
        vm_flags |= VM_EXEC;
        perm |= PTE_X;
    }
    if (vm_flags == 0)
    {
        return -E_INVAL_ELF;
    }

    int ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL);
    if (ret != 0)
    {
        return ret;
    }

    uintptr_t file_end = ph->p_va + ph->p_filesz;
    uintptr_t mem_end = ph->p_va + ph->p_memsz;
    for (uintptr_t va = ROUNDDOWN(ph->p_va, PGSIZE); va < mem_end; va += PGSIZE)
    {
        struct Page *page = pgdir_alloc_page(mm->pgdir, va, perm);
        if (page == NULL)
        {
            return -E_NO_MEM;
        }
        unsigned char *kva = page2kva(page);
        memset(kva, 0, PGSIZE);
        uintptr_t start = va < ph->p_va ? ph->p_va : va;
        uintptr_t end = va + PGSIZE < file_end ? va + PGSIZE : file_end;
        if (start < end)
        {
            size_t file_offset = ph->p_offset + (start - ph->p_va);
            memcpy(kva + (start - va), binary + file_offset, end - start);
        }
    }
    return 0;
}

static int
load_icode(unsigned char *binary, size_t size)
{
    struct elfhdr elf;
    if (size < sizeof(elf))
    {
        return -E_INVAL_ELF;
    }
    memcpy(&elf, binary, sizeof(elf));
    // The Lab5 linker produces little-endian, executable ELF64 images for RISC-V.
    if (elf.e_magic != ELF_MAGIC || elf.e_elf[0] != 2 || elf.e_elf[1] != 1 ||
        elf.e_type != 2 || elf.e_machine != 243 || elf.e_version != 1 ||
        elf.e_ehsize != sizeof(elf) || elf.e_phentsize != sizeof(struct proghdr) ||
        elf.e_phoff < sizeof(elf) || elf.e_phoff > size ||
        elf.e_phnum > (size - elf.e_phoff) / sizeof(struct proghdr) ||
        (elf.e_entry & 1) != 0)
    {
        return -E_INVAL_ELF;
    }

    struct mm_struct *mm = mm_create();
    if (mm == NULL)
    {
        return -E_NO_MEM;
    }
    int ret = setup_pgdir(mm);
    if (ret != 0)
    {
        mm_destroy(mm);
        return ret;
    }

    const uintptr_t stack_base = USTACKTOP - USTACKSIZE;
    uintptr_t previous_end = USERBASE;
    bool executable_entry = 0;
    for (size_t i = 0; i < elf.e_phnum; i++)
    {
        struct proghdr ph;
        memcpy(&ph, binary + elf.e_phoff + i * sizeof(ph), sizeof(ph));
        if (ph.p_type != ELF_PT_LOAD)
        {
            continue;
        }
        if (ph.p_filesz > ph.p_memsz || ph.p_offset > size ||
            ph.p_filesz > size - ph.p_offset)
        {
            ret = -E_INVAL_ELF;
            goto bad_load;
        }
        if (ph.p_memsz == 0)
        {
            continue;
        }
        // Check the page-rounded VMA bounds before inserting the segment.
        if (ph.p_va < previous_end || ph.p_va >= stack_base ||
            ph.p_memsz > stack_base - ph.p_va)
        {
            ret = -E_INVAL_ELF;
            goto bad_load;
        }
        uintptr_t end = ph.p_va + ph.p_memsz;
        if ((ph.p_flags & ELF_PF_X) && ph.p_va <= elf.e_entry && elf.e_entry < end)
        {
            executable_entry = 1;
        }
        previous_end = ROUNDUP(end, PGSIZE);
        ret = load_elf_segment(mm, binary, &ph);
        if (ret != 0)
        {
            goto bad_load;
        }
    }
    if (!executable_entry)
    {
        ret = -E_INVAL_ELF;
        goto bad_load;
    }

    ret = mm_map(mm, stack_base, USTACKSIZE, VM_READ | VM_WRITE | VM_STACK, NULL);
    if (ret != 0)
    {
        goto bad_load;
    }
    for (uintptr_t va = USTACKTOP - 4 * PGSIZE; va < USTACKTOP; va += PGSIZE)
    {
        struct Page *page = pgdir_alloc_page(mm->pgdir, va, PTE_U | PTE_R | PTE_W);
        if (page == NULL)
        {
            ret = -E_NO_MEM;
            goto bad_load;
        }
        memset(page2kva(page), 0, PGSIZE);
    }

    // The source ELF can belong to the address space being replaced.
    struct mm_struct *old_mm = current->mm;
    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    lsatp(current->pgdir);
    flush_tlb();
    if (old_mm != NULL && mm_count_dec(old_mm) == 0)
    {
        exit_mmap(old_mm);
        put_pgdir(old_mm);
        mm_destroy(old_mm);
    }
    asm volatile("fence.i" ::: "memory");

    struct trapframe *tf = current->tf;
    uintptr_t status = (tf->status | SSTATUS_SPIE) & ~(SSTATUS_SPP | SSTATUS_SIE);
    memset(tf, 0, sizeof(*tf));
    tf->gpr.sp = USTACKTOP;
    tf->epc = elf.e_entry;
    tf->status = status;
    return 0;

bad_load:
    exit_mmap(mm);
    put_pgdir(mm);
    mm_destroy(mm);
    return ret;
}

int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    struct mm_struct *mm = current->mm;
    char local_name[PROC_NAME_LEN + 1] = {0};
    len = len < PROC_NAME_LEN ? len : PROC_NAME_LEN;
    if (size == 0 || !mapped_mem_check(mm, (uintptr_t)binary, size, 0) ||
        (len != 0 && !mapped_mem_check(mm, (uintptr_t)name, len, 0)))
    {
        return -E_INVAL;
    }
    if (len != 0)
    {
        memcpy(local_name, name, len);
    }
    int ret = load_icode(binary, size);
    if (ret == 0)
    {
        set_proc_name(current, local_name);
    }
    return ret;
}

int
do_wait(int pid, int *code_store)
{
    if (pid < 0 || pid >= MAX_PID)
    {
        return -E_BAD_PROC;
    }
    while (1)
    {
        bool intr_flag;
        local_intr_save(intr_flag);
        if (current->flags & PF_EXITING)
        {
            local_intr_restore(intr_flag);
            return do_exit(-E_KILLED);
        }

        struct proc_struct *child = pid == 0 ? current->cptr : find_proc(pid);
        if (child == NULL || child->parent != current)
        {
            local_intr_restore(intr_flag);
            return -E_BAD_PROC;
        }
        if (code_store != NULL &&
            !mapped_mem_check(current->mm, (uintptr_t)code_store, sizeof(*code_store), 1))
        {
            local_intr_restore(intr_flag);
            return -E_INVAL;
        }
        if (pid == 0)
        {
            while (child != NULL && child->state != PROC_ZOMBIE)
            {
                child = child->optr;
            }
        }
        if (child != NULL && child->state == PROC_ZOMBIE)
        {
            if (code_store != NULL &&
                !copy_to_user(current->mm, code_store, &child->exit_code, sizeof(*code_store)))
            {
                local_intr_restore(intr_flag);
                return -E_INVAL;
            }
            unhash_proc(child);
            remove_links(child);
            local_intr_restore(intr_flag);
            put_kstack(child);
            kfree(child);
            return 0;
        }

        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        local_intr_restore(intr_flag);
    }
}


//lab5 function3 YOUR CODE

//LAB4 function2 2412819
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    if (nr_process >= MAX_PROCESS)
    {
        return -E_NO_FREE_PROC;
    }

    int ret = -E_NO_MEM;
    struct proc_struct *proc = alloc_proc();
    if (proc == NULL)
    {
        return ret;
    }
    proc->parent = current;

    if ((ret = setup_kstack(proc)) != 0)
    {
        goto bad_fork_cleanup_proc;
    }
    if ((ret = copy_mm(clone_flags, proc)) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }
    copy_thread(proc, stack, tf);

    bool intr_flag;
    local_intr_save(intr_flag);
    proc->pid = get_pid();
    set_links(proc);
    hash_proc(proc);
    wakeup_proc(proc);
    local_intr_restore(intr_flag);
    return proc->pid;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    return ret;
}

//lab5 function4 YOUR CODE


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


//lab5 function2 2412439
static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    int ret = do_execve(name, strlen(name), binary, size);
    if (ret != 0)
    {
        return ret;
    }

    // Kernel threads already have a trapframe at the top of their kernel stack.
    intr_disable();
    forkrets(current->tf);
    panic("forkrets returned from kernel_execve");
}

static int
user_main(void *arg)
{
#ifdef TEST
    return KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    return KERNEL_EXECVE(exit);
#endif
}

static int
init_main(void *arg)
{
    size_t free_pages_before = nr_free_pages();
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0)
    {
        panic("cannot create user_main: %d.", pid);
    }
    while (do_wait(0, NULL) == 0)
    {
    }

    cprintf("all user-mode processes have quit.\n");
    assert(current == initproc && initproc->cptr == NULL && nr_process == 2);
    assert(nr_free_pages() == free_pages_before);
    cprintf("init check memory pass.\n");
    while (1)
    {
        kmonitor(NULL);
    }
}

//LAB4 function2 2412439
void proc_init(void)
{
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    idleproc = alloc_proc();
    if (idleproc == NULL)
    {
        panic("cannot allocate idleproc.\n");
    }
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;
    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0)
    {
        panic("cannot create initproc: %d.\n", pid);
    }
    initproc = find_proc(pid);
    assert(initproc != NULL);
    set_proc_name(initproc, "init");
}

//LAB4 function3 2412852
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}

void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
        bool intr_flag;
        local_intr_save(intr_flag);
        struct proc_struct *prev = current;
        current = proc;
        write_csr(satp, 0x8000000000000000ULL | (proc->pgdir >> RISCV_PGSHIFT));
        flush_tlb();
        switch_to(&prev->context, &proc->context);
        local_intr_restore(intr_flag);
    }
}
