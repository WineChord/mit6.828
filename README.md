# MIT 6.828 

## Lab1 Booting a PC 

### PC Bootstrap

The physical address space is shown as follows:

```

+------------------+  <- 0xFFFFFFFF (4GB)
|      32-bit      |
|  memory mapped   |
|     devices      |
|                  |
/\/\/\/\/\/\/\/\/\/\

/\/\/\/\/\/\/\/\/\/\
|                  |
|      Unused      |
|                  |
+------------------+  <- depends on amount of RAM
|                  |
|                  |
| Extended Memory  |
|                  |
|                  |
+------------------+  <- 0x00100000 (1MB)
|     BIOS ROM     |
+------------------+  <- 0x000F0000 (960KB)
|  16-bit devices, |
|  expansion ROMs  |
+------------------+  <- 0x000C0000 (768KB)
|   VGA Display    |
+------------------+  <- 0x000A0000 (640KB)
|                  |
|    Low Memory    |
|                  |
+------------------+  <- 0x00000000
```

The exact booting procedure and details are architecture dependent. As for mit 6.828 lab, the booting steps are:

1. BIOS: IBM PC starts at physical address `0x000ffff0` (BIOS ROM) with CS = `0xf000`, IP = `0xfff0` (just 16 bytes before the end of BIOS ROM). The first instruction is 
`[f000:fff0] 0xffff0:	ljmp   $0xf000,$0xe05b` which jumps to the segmented address with CS = `0xf000`, IP = `0xe05b`. The processor is running in real mode and the physical address is CS*16 + IP. The BIOS will setup an interrupt descriptor table and initialize various devices such as VGA display. After initializing the PCI bus and all the important devices the BIOS knows about, it searches for a bootable device. It will read the bootloader from the bootable disk and transfer control to it.

2. bootloader: The disk is divided into 512 bytes region called _sector_. The first sector of a disk is the boot sector. The BIOS loads the boot sector into physical address from `0x7c00` to `0x7dff`. Then it will jump to `0000:7c00` to pass control to the bootloader. The bootloader does the followint thing (see `boot/boot.S` and `boot/main.c`)
    
    1. Switch the processor from real mode to 32-bit protected mode. Then the software is able to access the all the memory above 1MB in the process's physical address space. (details: 1. setup the important data segment registers such as DS,ES,SS. 2. Enable A20, which is for backwards compatibility with the earliest PCs, the addresses higher than 1MB will wrap around to zero by default. Need to undo this. 3. Switch from real to protected mode, using a bootstrap GDT and segment translation that makes virtual addresses identical to their physical address, so that the effective memory map does not change during the switch. The exact instruction is `lgdt gdtdesc` and set protected mode enable flag `CR0_PE_ON` bit for cr0 register. 4. Jump to next instruction, but in 32-bit code segment. This will switch the processor into 32-bit mode. 5. In 32-bit code segment, it will setup the protected-mode data segment registers such as DS,ES,FS,GS,SS. Then it will set up the stack pointer and call into `bootmain` function in `/boot/main.c`)
    2. Reads the kernel from the hard disk by directly accessing the IDE disk registers via the x86's special I/O instructions. details:
        * Use `readseg` to read the 1st page off dist into physical address `0x10000` (64KB at low physical memory). This part forms the ELF header.
        * Load each program segment into physical memory. The ELF header contain the information about the number of program headers (`ELFHDR->e_phnum`), and the start address of program headers (`ELFHDR->e_phoff`). Each program header contains the load address (`ph->p_pa`), memory size (`ph->p_memsz`) and the program segment location on disk (`ph->p_offset`). The `.bss` part is zeroed (the size is `ph->p_memsz - ph->p_filesz`). Generally, the kernel is loaded at physical address starting from `0x100000` (1MB) just above the BIOS part. 
        * Call the entry point from the ELF header. This will enter the kernel code (`kern/entry.S`) `call *0x10018`. (the entry point is `ELFHDR->e_entry`). From the assembly we can see that the entry address is `0xf010000c`. This will be the first address the kernel starts to execute. 

3. kernel: `kern/entry.S` when first enter `entry`, we haven't set up virtual memory yet, so we're running from the physical address the boot loader loaded the kernel at: 1MB (plus a few bytes). However, the C code is linked to run at KERNBASE+1MB. Hence, we set up a trivial page directory that translates virtual addresses `[KERNBASE,KERNBASE+4MB)` to physical addresses `[0,4MB)`. This 4MB region will be sufficient until we set up our real page table in `mem_init`. 
    1. Load the physical address of `entry_pgdir` into cr3. `entry_pgdir` is page directories that map VA's `[0,4MB)` and `[KERNBASE,KERNBASE+4MB)` to PA's `[0,4MB)`. 
    2. Trun on paging: set `CR0_PE|CR0_PG|CR0_WP` bit of cr0 register. (PE means protected mode enable, PG means paging, WP means write protect). If paging is not turned on, the first instruction will be affected is the code to jump above KERNBASE (`mov $relocated, %eax; jmp *%eax`). 
    3. Jump above KERNBASE
    4. Clear the frame pointer register (EBP), set the stack pointer (`movl $(bootstacktop),%esp`) (__This is where the stack gets initialized.__)
    5. Run C code (`call i386_init`, in file `kern/init.c`)
    6. Inside `i386_init()`, do a list of initializations:
        * `cons_init`: initialize the console. Then we can call `cprintf`
        * `mem_init`: initialize memory management (lab2)
        * `env_init`: initialize user environment (lab3)
        * `trap_init`: initialize trap (lab3)
        * `mp_init`: initialize multiprocessor (lab4)
        * `lapic_init`: initialize local APIC(Advanced Programmable Interrupt Controller) (lab4)
        * `pic_init`: initialize multitasking (8295A interrupt controllers) (lab4)
        * `time_init`: initialize time ticks (lab6)
        * `pci_init`: initialize PCI (Peripheral Component Interconnect) for network driver (lab6)
        * `boot_aps`: start non-boot CPUs (lab4)
        * `ENV_CREATE(fs_fs, ENV_TYPE_FS)`: start file system (lab5)
        * `ENV_CREATE(net_ns, ENV_TYPE_NS)`: start network server environment (lab6)
        * `ENV_CREATE(user_icode, ENV_TYPE_USER)`: start a shell (lab5)
        * `shed_yield`: schedule and run the first user environment (lab4)

Notes:

> The 4 prerequisites to enter protect mode:
>   * Disable interrupts
>   * Enable the A20 line
>   * Load the Global Descriptor Table
>   * Set PE (protected mode enable) bit in CR0 
>
> The instruction `ljmp $PROT_MODE_CSEG, $protcseg` will makes the processor start executing in 32-bit code. 
> 
> The VMA (link address) and LMA (load address) is not the same for kernel image (`f0100000` and `00100000`). It is linked at a very high virtual address in order to leave the lower part of the processor's virtual address space for user programs to use. The virtual address `0xf0100000` is mapped into physical address `0x00100000`. 
> 
> If paging is not turned on, the first instruction will be affected is the code to jump above KERNBASE (`mov $relocated, %eax; jmp *%eax`). 

Typical question: How does `cprintf` implemented?

1. Initialize the console using `cons_init`, it will do the following:
    1. `cga_init`: CGA (Color Graphics Adapter) initialization
    2. `kbd_init`: keyboard initialization 
    3. `serial_init`: serial port initialization 
2. The call of `cprintf(const char *fmt, ...)`:
    1. `va_list ap` will point to the first argument, `fmt` will point to the format string. `vcprintf(fmt, ap)` is enclosed by `va_start(ap, fmt)` and `va_end(ap)`. Inside the call to `va_arg(ap, type)` will return the value of the given type and increment `ap` according to the type size. 
    2. `vcprintf(const char *fmt, va_list ap)` will call `vprintfmt((void*)putch, &cnt, fmt, ap)`. `cprintf, vcprintf` are located in `kern/printf.c`, whereas `vprintfmt` is located in `lib/printfmt.c`.
    3. `vprintfmt` mainly process the `fmt` string and `ap` argument list to decide what to print and call `putch` to print them on the console.
    4. `putch` is inside `kern/printf.c`. It calls `cputchar(int ch)`(`kern/console.c`) which in turn calls `cons_putc(int c)`(`kern/console.c`)
    5. `cons_putc` will call `serial_putc(c), lpt_putc(c), cga_putc(c)` to print the character on the console.

The start of stack address:

This can be inferred from the code `kern/entry.S`

```asm
.data
###################################################################
# boot stack
###################################################################
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:
```

According to the `objdump -h kernel`, `.data` is loaded at `0xf011b000` (maybe different). So `bootstacktop` should be `0xf0123000` and `obj/kern/kernel.asm` consolidates my assumption. 


## Lab2 Memory Management 

### Physical Page Management 

This part will go through the first half of `mem_init` in `kern/pmap.c`.

1. `i386_detect_memory`: This will find out how much memory the machine has (number of pages for base memory and number of pages for total memory).
2. create initial page directory `kern_pgdir` using `boot_alloc` and initialize it to zero. The implementation of `boot_alloc(uint32_t n)` is as follows (it is only used while jos is setting up its virtual memory system. `page_alloc` is the real allocator. If `n>0`, it will allocate enough pages of contiguous physical memory to hold `n` bytes. If `n==0`, it will return the address of the next free page without allocating anything):
    1. Initialize the static pointer `nextfree` if it is the first time. `nextfree` will be initialized to `end` rounded up to `PGSIZE`. `end` is a symbol that points to the end of the kernel's bss segment: it is the first virtual address that the linker didn't assign to any kernel code or global variables. (just like heap).
    2. Allocate a chunk large enough to hold `n` bytes, then update `nextfree`. 
3. Recursively insert PD in itself as a page table, to form a virtual page table at virtual address UVPT: `kern_pgdir[PDX(UVPT)] = PADDR(kern_pgdir)|PTE_U|PTE_P;`. Explanation see [the clever mapping trick](https://pdos.csail.mit.edu/6.828/2018/labs/lab4/uvpt.html). In this way, the page tables are mapped into the virtual address space so we can access these virtual address to directly access the page table entry. Otherwise we have to use `KADDR(pgdir[PDX(va)])+PTX(va)` to access the address of page table entry. (note that in the formula pgdir stores the physical addresses of each page directory entry). After using this mapping, we can access the page table entry for a virtual address `va` by using `uvpt[PGNUM(va)]` (`uvpt[]` see `inc/memlayout.h`)
4. Using `boot_alloc` to allocate an array of npages (this is the number of physical pages we detect on the machine) `struct PageInfo`s and store it in `pages`. These are the all physical pages we can allocate. The kernel uses this array to keep track of physical pages: for each physical page, there is a corresponding `struct PageInfo` in this array.
5. Using `boot_alloc` to allocate kernel data structures for environments which is an array of size `NENV` of `struct Env` and make `envs` point to it. (lab3)
6. Here we've allocated the initial kernel data structures and set up the list of free physical pages. Then we'll call `page_init` to initialize page structure and memory free list. After this function is done, `boot_alloc` will never be used and only the page allocator functions are used. The goal of `page_init` is to mark used physical pages as used and form a `page_free_list`. `struct PageInfo` has a reference count member `pp_ref` and page link `pp_link`. `pp_ref` is used to record usage count (some physical pages might be mapped to multiple virtual addresses) and `pp_link` is used to link free pages. `page_free_list` will point to the head node of free pages. The procedure for `page_init` is:
    1. Mark physical page 0 as in use. This way we can preserve the real-mode IDT and BIOS structures in case we ever need them. (???)
    2. Mark the rest of base memory `[PGSIZE, npages_basemem * PGSIZE)` except physical page at `MPENTRY_PADDR`(lab4 multiprocessor) as free. Remember the physical address space shown at the beginning. The base memory spans `[0,640KB)` address range `[0,0xA0000)`. This has 160 pages. `MPENTRY_PADDR` is `0x7000` which is page 7. 
    3. Mark IO hole as used. `[640KB,1024KB)` `[0xA0000,0x100000)`. 
    4. Mark the kernel `.text, .data, .bss` and kernel data structures' physical page as used. The upper bound can be obtained by `boot_alloc(0)`. 
    5. Mark the rest as free and add them to free list.
7. Now all further memory management will go through the `page_*` functions.

> `page_*` functions implementation:
> 
> * `page_alloc(int alloc_flag)`: Allocates a physical page. If (alloc_flags & ALLOC_ZERO), the entire returned physical page will be filled with `'\0'` bytes. The reference count of the page should not be incremented, as the caller must do this if necessary (either explicitly or via `page_insert`). The implementation is simple. You grap a node from `page_free_list` and let `page_free_list` point to `page_free_list->pp_link`. When using `memset`, remember to convert `PageInfo` to virtual address using `page2kva`.
> * `page_free(struct PageInfo *pp)`: trivial, link that page into `page_free_list`.

### Virtual Memory 

As for _segmentation_ and _page translation_:

```
           Selector  +--------------+         +-----------+
          ---------->|              |         |           |
                     | Segmentation |         |  Paging   |
Software             |              |-------->|           |---------->  RAM
            Offset   |  Mechanism   |         | Mechanism |
          ---------->|              |         |           |
                     +--------------+         +-----------+
            Virtual                   Linear                Physical
```

In x86 terminology, a _virtual address_ consists of a segment selector and an offset within the segment. A _linear address_ is what you get after segment translation but before page translation. A _physical address_ is what you finally get after both segment and page translation and what ultimately goes out on the hardware bus to your RAM. 

A C pointer is the 'offset' component of the virtual address. In `boot/boot.S` the Global Descriptor Table (GDT) is installed to disable segment translation by setting all segment base addresses to 0 and limits to `0xffffffff`. Therefore the 'selector' has no effect and the linear address always equals the offset of the virtual address. 

Also, a simple page table has already been installed so that the kernel could run at its link address of `0xf0100000`. It only mapped 4MB. Here it is going to map the first 256MB of physical memory to virtual address `0xf0000000` and to map a number of other regions of the virtual address space.

Once we're in protected mode, all memory references are interpreted as virtual addresses and translated by the MMU, which means all pointers in C are virtual addresses. 

Page Table Management 
* `pgdir_walk(pde_t *pgdir, const void *va, int create)`: It return the page table entry pointer corresponding to the virtual address `va` and `pgdir`. This requires walking the two-level page table structure. The rational is to get page directory entry using `pgdir[PDX(va)]` and check whether there exists a mapping using `PTE_P` flag. Allocate a new page for the page table if `create=true`. Then get page table entry address by `KADDR(PTE_ADDR(*pde)) + PTX(va)`. 
* `boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)`: Map `[va,va+size)` of virtual address to physical address `[pa,pa+size)`. Iterate through all the pages inside this region, use `pgdir_walk` to find its corresponding page table entry. Set the value of page table entry to the corresponding physical address and permission bits. Also update the permission bits of page directory entry.
* `page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)`: Return the page mapped at virtual address `va` (returns `struct PageInfo`). Implementation: first use `pgdir_walk` to find the corresponding page table entry, and then use `pa2page(PTE_ADDR(*pte))` to get the page table entry's corresponding `struct PageInfo*`.
* `page_remove(pde_t *pgdir, void *va)`: Unmap the physical page at virtual address `va`. Implementation: first use `page_lookup` to find its `struct PageInfo*`, then invalidate the translation lookaside buffer, decrement the reference count (if reach zero, free the page using `page_free`).
* `page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)`: Map the physical page `pp` at virtual address `va`. If there is already a page mapped at `va`, it should be `page_remove`d. If necessary, on demand, a page table should be allocated and inserted into `pgdir`. `pp->pp_ref` should be incremented if the insertion succeeds. The TLB must be invalidated if a page was formerly present at `va`. Corner-case: the same pp is re-inserted at the same virtual address in same pgdir. I first try to distinguish this case, looks like the following:
```c
	physaddr_t pa = page2pa(pp); // physical addr of page 
	pte_t *p = pgdir_walk(pgdir, va, 0); 
	// already exists a map and the physical address mismatch 
	if (p != NULL && (*p & PTE_P) && (PTE_ADDR(*p) != pa)) { 
		page_remove(pgdir, va);
		tlb_invalidate(pgdir, va);
	}
	p = pgdir_walk(pgdir, va, 1); 
	if (p == NULL) // allocation fails 
		return -E_NO_MEM;
	if (PTE_ADDR(*p) != pa)
		pp->pp_ref += 1; 
	*p = pa | perm | PTE_P; 
	pgdir[PDX(va)] = pgdir[PDX(va)] | perm | PTE_P;
	return 0;
```
Although it can pass lab2, it will cause error in later labs and it is really hard to discover that what's wrong is here. The correct solution is elegant:
```c
	physaddr_t pa = page2pa(pp); // physical addr of page 
	pte_t *p = pgdir_walk(pgdir, va, 1); 
	if (p == NULL)
		return -E_NO_MEM;
	pp->pp_ref++;
	if ((*p) & PTE_P) 
		page_remove(pgdir, va);
	*p = pa | perm | PTE_P; 
	pgdir[PDX(va)] = pgdir[PDX(va)] | perm | PTE_P;
	return 0;
```
The idea is first increment the reference count, if a mapping already exists, the `page_remove` function will take care of decrementing the reference count and invalidating the translation lookaside buffer. 


### Kernel Address Space

Now back to second half of `mem_init` function, we need to use these `page_*` utilities to set up the virtual memory of kernel address space. The virtual memory layout is shown as follows(should be memorized together with the physical address space):

```c
/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+                 --+          PDE 1023 
 *                     |                              | RW/--             |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *                     :              .               :                256 MB
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--             |
 *                     |                              | RW/--             |
 *                     |   Remapped Physical Memory   | RW/--             |
 *                     |                              | RW/--             |
 *    KERNBASE, ---->  +------------------------------+ 0xf0000000      --+          PDE 960
 *    KSTACKTOP        |     CPU0's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                   |          
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     |     CPU1's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE 4MB
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *    MMIOLIM ------>  +------------------------------+ 0xefc00000      --+          PDE 959 
 *                     |       Memory-mapped I/O      | RW/--  PTSIZE 4MB
 * ULIM, MMIOBASE -->  +------------------------------+ 0xef800000                   PDE 958
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE 4MB 
 *    UVPT      ---->  +------------------------------+ 0xef400000                   PDE 957
 *                     |          RO PAGES            | R-/R-  PTSIZE 4MB
 *    UPAGES    ---->  +------------------------------+ 0xef000000                   PDE 956 
 *                     |           RO ENVS            | R-/R-  PTSIZE 4MB
 * UTOP,UENVS ------>  +------------------------------+ 0xeec00000                   PDE 955
 * UXSTACKTOP -/       |     User Exception Stack     | RW/RW  PGSIZE 4KB
 *                     +------------------------------+ 0xeebff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE 4KB
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE 4KB
 *                     +------------------------------+ 0xeebfd000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE 4MB
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+          PDE 1
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE 4MB
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+          PDE 0
 *
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.  JOS user programs map pages temporarily at UTEMP.
 */
```

The rest of `mem_init`:

1. Map `pages` read-only by the user at linear address `UPAGES` 
```c
	boot_map_region(kern_pgdir, UPAGES, pages_size, PADDR(pages), PTE_U | PTE_P);
```
2. Map `envs` array read-only by the user at linear address `UENVS` (lab3)
```c
	boot_map_region(kern_pgdir, UENVS, envs_size, PADDR(envs), PTE_U | PTE_P);
```
3. Use the physical memory that `bootstack` refers to as the kernel stack.
```c
	boot_map_region(kern_pgdir, KSTACKTOP-KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W);
```
4. Map all of physical memory at `KERNBASE`. `[KERNBASE,2^32)->[0,2^32-KERNBASE)`
```c
	size_t sz = (1LL<<32) - KERNBASE;
	boot_map_region(kern_pgdir, KERNBASE, sz, 0, PTE_W);
```
5. Initialize the SMP-related parts of the memory map (lab4) `mem_init_mp`
    * It will map the per-CPU stacks in the region `[KSTACKTOP-PTSIZE,KSTACKTOP)`
```c
	// Map per-CPU stacks starting at KSTACKTOP, for up to 'NCPU' CPUs.
	//
	// For CPU i, use the physical memory that 'percpu_kstacks[i]' refers
	// to as its kernel stack. CPU i's kernel stack grows down from virtual
	// address kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP), and is
	// divided into two pieces, just like the single stack you set up in
	// mem_init:
	//     * [kstacktop_i - KSTKSIZE, kstacktop_i)
	//          -- backed by physical memory
	//     * [kstacktop_i - (KSTKSIZE + KSTKGAP), kstacktop_i - KSTKSIZE)
	//          -- not backed; so if the kernel overflows its stack,
	//             it will fault rather than overwrite another CPU's stack.
	//             Known as a "guard page".
	//     Permissions: kernel RW, user NONE
	//
	// LAB 4: Your code here:
	size_t i;
	uintptr_t va, kstacktop_i;
	physaddr_t pa;
	for (i = 0; i < NCPU; i++) {
		kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
		va = kstacktop_i - KSTKSIZE;
		pa = PADDR(&percpu_kstacks[i]);
		boot_map_region(kern_pgdir, va, KSTKSIZE, pa, PTE_W);
	}
``` 
6. Switch from the minimal entry page directory to the full `ker_pgdir` page table we just created. 
```c
	lcr3(PADDR(kern_pgdir));
```
7. Configure more flags in cr0
```c
	cr0 = rcr0();
    //               Alignment Mask  Numeric Error  Monitor co-processor
	cr0 |= CR0_PE|CR0_PG|CR0_AM|CR0_WP|CR0_NE|CR0_MP;
	cr0 &= ~(CR0_TS|CR0_EM);
	lcr0(cr0);
```

## Lab3 User Environment 

### User Environment and Exception Handling 

jos's environment is similar to process in a modern operating system. The structure to describe each environment is as follows:
```c
struct Env {
	struct Trapframe env_tf;	// Saved registers
	struct Env *env_link;		// Next free Env
	envid_t env_id;			// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	enum EnvType env_type;		// Indicates special system environments
	unsigned env_status;		// Status of the environment
	uint32_t env_runs;		// Number of times environment has run
	int env_cpunum;			// The CPU that the env is running on

	// Address space
	pde_t *env_pgdir;		// Kernel virtual address of page dir

	// Exception handling
	void *env_pgfault_upcall;	// Page fault upcall entry point

	// Lab 4 IPC
	bool env_ipc_recving;		// Env is blocked receiving
	void *env_ipc_dstva;		// VA at which to map received page
	uint32_t env_ipc_value;		// Data value sent to us
	envid_t env_ipc_from;		// envid of the sender
	int env_ipc_perm;		// Perm of page mapping received
};
```
__Note:__ jos's `struct Env` is analogous to `struct proc` in xv6. Both structures hold the environment's user-mode register state in a `Trapframe` structure. In jos, inidividual environment do not have their own kernel stacks as processes do in xv6. There can be only one jos environment active in the kernel at a time, so jos needs only a single kernel stack. (So if we can have multiple processes truly simultaneously running on different cores, we should provide kernel stack for each process. When will that stack be needed? Besides, jos implements multi-processor, so it still can only run one process in the kernel at a time?).

Creating and Running Environments:

* `env_init`: Mark all environments in `envs` as free, set their `env_ids` to 0, and insert them into the `env_free_list`. (purpose: form `env_free_list`).
* `env_setup_vm(struct Env *e)`: Initialize the kernel virtual memory layout for environment e. Allocate a page directory, set `e->env_pgdir` accordingly, and initialize the kernel portion of the new environment's address space. This will not map anything into the user portion. Steps:
    1. Allocate a page for the page directory using `page_alloc`
    2. Set `e->env_pgdir`: `e->env_pgdir = (pde_t *)page2kva(p)`
    3. Initialize the page directory. Use `kern_pgdir` as a template. The virtual address space of all envs is identical above `UTOP` except at `UVPT`. `UVPT` should map the env's own page table: `e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;`
* `env_alloc(struct Env **newenv_store, envid_t parent_id)`: Allocates and initializes a new environment. Steps:
    1. Grab a `struct Env *` from `env_free_list`: `e=env_free_list`. After the allocation is done, at the end of funtion it will be committed: `env_free_list=e->env_link`. 
    2. Allocate and set up the page directory for this environment using `env_setup_vm` (this will initialize the kernel virtual memory layout for environment `e`). 
    3. Generate an `env_id` for this environment. An environment ID `envid_t` has three parts:
    ```c
    // +1+---------------21-----------------+--------10--------+
    // |0|          Uniqueifier             |   Environment    |
    // | |                                  |      Index       |
    // +------------------------------------+------------------+
    //                                       \--- ENVX(eid) --/
    ```
    `ENVX(eid)` = the environment's index in the `envs[]` array. The uniqueifier distinguishes environments that were created at different times, but share the same environment index. `envid_t == 0` stands for the current environment. 
    4. Set the basic status variables: `env_parent_id, env_type, env_status, env_runs`.
    5. Clear out all the saved register state, to prevent the register values of a prior environment inhabiting this `Env` structure from "leaking" into this new environment: `memset(&e->env_tf, 0, sizeof(e->env_tf);`
    6. Set up appropriate initial values for the segment registers. `GD_UD` is the user data segment selector in the GDT, `GD_UT` is the user text segment selector. The low 2 bits of each segment register contains the Requestor Privilege Level (RPL), 3 means user mode. When switching privilege levels, the hardware does various checks involving the RPL and the Descriptor Privilege Level (DPL) stored in the descriptors themselves. The Current Privilege Level (CPL) is stored in the lowest 2 bits of the code segment selector (CS). Access to a segment is allowed only if CPL <= DPL and RPL <= DPL. `e->env_tf.tf_ds/es/ss` is initialized to `GD_UD|3`. `e->env_tf.tf_cs` is initialized to `GD_UT|3`. `e->env_tf.tf_esp` is initialized to `USTACKTOP`. `tf_eip` is initialized later after loading the binary image. 
    7. (lab4) Enable interrupts while in user mode. `tf_eflags|=FL_IF`
    8. Clear the page fault handler until user installs one. `env_pgfault_upcall=0`
    9. Clear the IPC receiving flag. `env_ipc_recving=0`
    10. Commit the allocation. `env_free_list=e->env_link; *newenv_store=e;`


> __Notes:__
> * RPL (Requested Privilege Level): low 2 bits of each segment register.
> * CPL (Current Privilege Level): low 2 bits of the code segment selector.
> * DPL (Descriptor Privilege Level): stored in the descriptor.
>
> Accessing a segment requires CPL <= DPL and RPL <= DPL.
> 
> [https://stackoverflow.com/questions/36617718/difference-between-dpl-and-rpl-in-x86](https://stackoverflow.com/questions/36617718/difference-between-dpl-and-rpl-in-x86)
> > An application would ordinarily not be able to access the memory in segment X (because CPL > DPL). But depending on how the system call was implemented, an application might be able to invoke the system call with a parameter of an address within segment X. Then, because the system call is privileged, it would be able to write to segment X on behalf of the application. This could introduce a privilege escalation vulnerability into the operating system.
> > 
> > To mitigate this, the official recommendation is that when a privileged routine accepts a segment selector provided by unprivileged code, it should first set the RPL of the segment selector to match that of the unprivileged code3. This way, the operating system would not be able to make any accesses to that segment that the unprivileged caller would not already be able to make. This helps enforce the boundary between the operating system and applications.
> > 
> > Currently:
> > 
> > Segment protection was introduced with the 286, before paging existed in the x86 family of processors. Back then, segmentation was the only way to restrict access to kernel memory from a user-mode context. RPL provided a convenient way to enforce this restriction when passing pointers across different privilege levels.
> > 
> > Modern operating systems use paging to restrict access to memory, which removes the need for segmentation. Since we don't need segmentation, we can use a flat memory model, which means segment registers CS, DS, SS, and ES all have a base of zero and extend through the entire address space. In fact, in 64-bit "long mode", a flat memory model is enforced, regardless of the contents of those four segment registers. Segments are still used sometimes (for example, Windows uses FS and GS to point to the Thread Information Block and 0x23 and 0x33 to switch between 32- and 64-bit code, and Linux is similar), but you just don't go passing segments around anymore. So RPL is mostly an unused leftover from older times.

* `region_alloc(struct Env *e, void *va, size_t len)`: Allocate len bytes of physical memory for environment `e`, and map it at virtual address `va` in the environment's address space. Steps:
    1. Calculate the page-aligned start and end virtual address.
    2. Loop through the pages of virtual address, use `page_alloc` to allocate physical page and use `page_insert` to insert the physical page at the corresponding virtual address.
* `load_icode(struct Env *e, uint8_t *binary)`: Set up the initial program binary, stack, and processor flags for a user process. This function is only called during kernel initialization, before running the first user-mode environment. It loads all loadable segments from the ELF binary image into the environment's user memory, starting at the appropriate virtual addresses indicated in the ELF program header. At the same time it clears the `.bss` section. This is very similar to what the boot loader does, except that the boot loader also needs to read the code from disk. Finally the function maps one page for the program's initial stack. Steps:
    1. Switch to environment page directory so that the virtual address translation of user address space can take effect. (first you should save the `cr3` in order to resume it at the end of the function because we still need to run in kernel address space after calling this function). 
    2. The begining of `binary` is ELF header. Get number of program headers from the ELF header. Loop through the headers. When is program header is of type `ELF_PROG_LOAD`, allocate physical pages for the virtual address region specified in this program header and copy the program section to virtual address, clear the `.bss` if exists. 
    3. Set `env_tf.tf_eip` to ELF header's `e_entry`.
    4. Now we have load the `.text, .data, .bss` segments of the program into memory (physically backed virtual address (although in modern linux, only the virtual address is allocated, virtual to physical mapping will only be created via page fault(????))). For the user portion of the address space, we still need to setup the stack area.
    5. Map one page for the program's initial stack at virtual address `USTACKTOP-PGSIZE`:
    ```c
    region_alloc(e, (void *)(USTACKTOP-PGSIZE), PGSIZE);
    ```
    6. Resume `cr3` 
* `env_create(uint8_t *binary, enum EnvType type)`: Allocate a new env with `env_alloc`, loads the named elf binary into it with `load_icode`, and sets its `env_type`. This function is only called during kernel initialization, before running the first user-mode environment. The new `env`'s parent ID is set to 0. Steps:
    1. Allocate a new env using `env_alloc` (this will initialize the kernel virtual address space, set up its registers, etc.)
    2. Call `load_icode` to load the binary into the environment's address space (this will also set up the `eip` register for the environment).
    3. Set it `EnvType`
    4. (lab5 fs) if this is the file server (type == ENV_TYPE_FS) give it IO privileges. 
* `env_free(struct Env *e)`: Frees env `e` and all memory it uses. Steps:
    1. If freeing the current environment, switch to `kern_pgdir` before freeing the page directory using `lcr3(PADDR(kern_pgdir))`.
    2. Flush all mapped pages in the user portion of the address space. 
        1. Loop through all the page directory entries below `UTOP`
        2. if it is mapped (`PTE_P` is on for the page directory entry) 
            1. find the physical and virtual address for the page table.
            2. unmap all the page table entries in this page table using `page_remove`.
            3. free the page table itself (`page_decref`).
    3. Free the page directory (`page_decref`).
    4. Return the environment to the free list. 
* `env_destroy(struct Env *e)`: Frees environment `e`. If `e` was the current `env`, then runs a new environment (and does not return to the caller)
    1. If `e` is currently running on other CPUs, change its state to `ENV_DYING`. A zombie environment will be freed the next time it traps to the kernel. 
    2. Call `env_free` to free env and all memory it uses. 
    3. If the env is current environment, call `sched_yield`(lab4, will choose a user environment to run and run it).
* `env_run(struct Env *e)`: Context switch from `curenv` to env `e`. If this is the first call to `env_run`, `curenv` is NULL.
    1. If this is a context switch (a new environment is running):
        1. Set the current environment (if any) back to `ENV_RUNNABLE` if it is `ENV_RUNNING`.
        2. Set `curenv` to the new environment
        3. Set its status to `ENV_RUNNING`
        4. Increment its `env_runs` counter
        5. Use `lcr3` to switch to its address space 
    2. Use `env_pop_tf` to restore the environment's state from `e->env_tf`.
* `env_pop_tf(struct Trapframe *tf)`: Restores the register values in the Trapframe with the `iret` instruction. This exits the kernel and starts executing some environment's code. This function does not return. 


Interrupt and Exception Handling:

* `trap_init`: inside `kern/init.c` function `i386_init`, called after `env_init`. It initialize the IDT with the addresses of the corresponding interrupt handlers. Each of the handlers should build a `struct Trapframe` on the stack and call `trap` with a pointer to the Trapframe. `trap` then handles the exception/interrupt or dispatches to a specific handler function. 
    1. Use `SETGATE` macro the set up interrupt/trap gate descriptor. Some examples are:
    ```c
    SETGATE(idt[T_DIVIDE ], 0, GD_KT, (uint32_t) (&t_divide ), 0); 
	SETGATE(idt[T_DEBUG  ], 0, GD_KT, (uint32_t) (&t_debug  ), 0);   
	SETGATE(idt[T_NMI    ], 0, GD_KT, (uint32_t) (&t_nmi    ), 0);   
    ```
    `T_*` is the trap numbers defined in `inc/trap.h` and `t_*` are functions declared in this scope, defined in `kern/trapentry.S`, for example:
    ```asm
    TRAPHANDLER_NOEC(t_divide , T_DIVIDE ) /*  0)		// divide error */
    TRAPHANDLER_NOEC(t_debug  , T_DEBUG  ) /*  1)		// debug exception */
    TRAPHANDLER_NOEC(t_nmi    , T_NMI    ) /*  2)		// non-maskable interrupt */
    //...
    TRAPHANDLER(     t_dblflt , T_DBLFLT ) /*  8)		// double fault */
    ```
    `TRAPHANDLER` defines a globally-visible function for handling a trap. It pushed a trap number onto the stack, then jumps to `_alltraps`. This will be used for traps where the CPU automatically pushes an error code. `TRAPHANDLER_NOEC` is used for traps where the CPU doesn't push an error code. It pushes a 0 in place of the error code, so the trap frame has the same format in either case. 
    The definition of `_alltraps` is:
    ```asm
    _alltraps:
    	pushl %ds;
    	pushl %es;
    	pusha;
    	movw $GD_KD, %ax;
    	movw %ax, %ds;
    	movw $GD_KD, %ax;
    	movw %ax, %es;
    	pushl %esp;
    	call trap;
    ```
    2. Initiate per-cpu setup using `trap_init_percpu`. This will initialize and load the per-CPU TSS (Task State Segment) and IDT.
        1. Setup a TSS so that we get the right stack when we trap to the kernel. 
        ```c
	    size_t i = thiscpu->cpu_id;
	    thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - i*(KSTKSIZE+KSTKGAP);
	    thiscpu->cpu_ts.ts_ss0 = GD_KD;
	    thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate); // io map base address
        ```
        2. Initialize the TSS slot of the gdt. 
        ```c
	    gdt[(GD_TSS0 >> 3)+i] = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
	    				sizeof(struct Taskstate) - 1, 0);
	    gdt[(GD_TSS0 >> 3)+i].sd_s = 0; // 0 for system, 1 for application
        ```
        3. Load the TSS selector (the bottom 3 bits are left 0)
        ```c
	    ltr(GD_TSS0+(i<<3)); // ltr means load task register 
        ```
        4. Load the IDT
        ```c
        lidt(&idt_pd);
        ```

> The purpose of having an individual handler function for each exception/interrupt: To push the corresponding error code onto the stack. This is used for the codes going to handle it further like `trap_dispatch` to distinguish the interrupts.
> 
> If user calls `int $14` directly which corresponds to the kernel's page fault handler, but this will produce interrupt vector 13. This is to provide permission control or isolation. For each interrupt handler, we can define it whether can be triggered by a user program or not. So that we can ensure user programs would not interfere with the kernel. 

> __Notes:__ Exceptions and interrupts are both _protected control transfers_, which cause the processor to switch from user to kernel mode(CPL=0) without giving the user-mode code any opportunity to interfere with the functioning of the kernel or other environments. In Intel's terminology, an _interrupt_ is a protected control trasfer that is caused by an asynchronous event usually external to the processor, such as notification of external device I/O activity. An _exception_, in contrast, is a protected control transfer caused synchronously by the currently running code, for example due to a divide by zero or an invalid memory access.
>
> In order to ensure that these protected control transfers are actually _protected_, the processor's interrupt/exception mechanism is designed so that the code currently running when the interrupt or exception occurs _does not get to choose arbitrarily where the kernel is entered or how_. Instead, the processor ensures that the kernel can be entered only under carefally controlled conditions. On the x86, two mechanisms work together to provide this protection:
>
> 1. The Interrupt Descriptor Table. The processor ensures that interrupts and exceptions can only cause the kernel to be entered at a few specific, well-defined entry-points _determined by the kernel itself_, and not by the code running when the interrupt or exception is taken. The x86 allows up to 256 different interrupt or exception entry points into the kernel, each with a different _interrupt vector_. A vector is a number between 0 and 255. An interrupt's vector is determined by the source of the interrupt: different devices, error conditions, and application requests to the kernel generate interrupts with different vectors. The CPU uses the vector as an index into the processor's _interrupt descriptor table_(IDT), which the kernel sets up in kernel-private memory, much like the GDT. From the appropriate entry in this table the processor loads: `EIP` and `CS`(includes in bits 0-1 the privilege level at which the exception handler is to run.). 
> 2. The Task State Segment. The processor needs a place to save the _old_ processor state before the interrupt or exception occurred, such as the original values of `EIP` and `CS` before the processor invoked the exception handler, so that the exception handler can later restore that old state and resume the interrupted code from where it left off. _But this save area for the old processor state must in turn be protected from unprivileged user-mode code; otherwise buggy or malicious user code could compromise the kernel._ For this reason, when an x86 processor takes an interrupt or trap that causes a privilege level change from user to kernel mode, it also switches to a stack in the kernel's memory. A structure called the _task state segment_(TSS) specifies the segment selector and address where this stack lives. The processor pushes (on this new stack) `SS, ESP, EFLAGS, CS, EIP`, and an optional error code. Then it loads the `CS` and `EIP` from the interrupt descriptor, and sets the `ESP` and `SS` to refer to the new stack. Although the TSS is large and can potentially serve a variety of purposes, jos only uses it to define the kernel stack that the processor should switch to when it transfers from user to kernel mode. Since 'kernel mode' in jos is privilege level 0 on the x86, the processor uses the `ESP0` and `SS0` fields of the TSS to define the kernel stack when entering kernel mode. 
> 
> Types of Exceptions and Interrupts:
>
> All of the synchronous exceptions that the x86 processor can generate internally use interrupt vector between 0 and 31, and therefore map to IDT entries 0-31. Interrupt vectors greater than 31 are only used by _software interrupts_, which can be generated by the `int` instruction, or asynchronous _hardware interrupts_, caused by external devices when they need attention. 
>
> Example:
> 
> The control flow when a divide by zero exception happened in user mode:
>
> 1. The processor switches to the stack defined by the `SS0` and `ESP0` fields of the TSS, which in jos will hold values `GD_KD` and `KSTACKTOP`, respectively.
> 2. The processor pushes the exception parameters on the kernel stack, starting at address `KSTACKTOP`:
>   ```
>                        +--------------------+ KSTACKTOP             
>                        | 0x00000 | old SS   |     " - 4
>                        |      old ESP       |     " - 8
>                        |     old EFLAGS     |     " - 12
>                        | 0x00000 | old CS   |     " - 16
>                        |      old EIP       |     " - 20 <---- ESP 
>                        +--------------------+  
>   ```
> 3. Divide error is interrupt vector 0 on the x86, the processor reads IDT entry 0 and set `CS:EIP` to point to the handler function described by the entry.
> 4. The handler function takes control and handles the exception, for example by terminating the user environment. 
>
> For certain types of x86 exceptions, in addition to the 'standard' five words above, the processor pushes onto the stack another word containing an _error code_. The page fault exception, number 14, is an important example. 
>   ```
>                        +--------------------+ KSTACKTOP             
>                        | 0x00000 | old SS   |     " - 4
>                        |      old ESP       |     " - 8
>                        |     old EFLAGS     |     " - 12
>                        | 0x00000 | old CS   |     " - 16
>                        |      old EIP       |     " - 20
>                        |     error code     |     " - 24 <---- ESP
>                        +--------------------+             
>   ```
> 
> The Trapframe structure:
>   ```c
>   struct Trapframe {
>   	struct PushRegs tf_regs;
>   	uint16_t tf_es;
>   	uint16_t tf_padding1;
>   	uint16_t tf_ds;
>   	uint16_t tf_padding2;
>   	uint32_t tf_trapno;
>   	/* below here defined by x86 hardware */
>   	uint32_t tf_err;
>   	uintptr_t tf_eip;
>   	uint16_t tf_cs;
>   	uint16_t tf_padding3;
>   	uint32_t tf_eflags;
>   	/* below here only when crossing rings, such as from user to kernel */
>   	uintptr_t tf_esp;
>   	uint16_t tf_ss;
>   	uint16_t tf_padding4;
>   } __attribute__((packed));
>   ```
> 
> Nested Exceptions and Interrupts
> 
> The processor can take exceptions and interrupts both from kernel and user mode. It is only when entering the kernel from user mode, however, that the x86 processor automatically switches stacks before pushing its old register state onto the stack and invoking the appropriate exception handler through the IDT. If the processor is _already_ in kernel mode when the interrupt or exception occurs (the low 2 bits of the `CS` register are already zero), then the CPU just pushes more values on the same kernel stack. In this way, the kernel can gracefully handle _nested exceptions_ caused by code within the kernel itself. If the processor is already in kernel mode and takes a nested exception, since it does not need to switch stacks, it does not save the old `SS` or `ESP` registers. For exception types that do not push an error code, the kernel stack therefore looks like the following on entry to the exception handler:
>   ```
>                        +--------------------+ <---- old ESP
>                        |     old EFLAGS     |     " - 4
>                        | 0x00000 | old CS   |     " - 8
>                        |      old EIP       |     " - 12
>                        +--------------------+      
>   ```
> There is one important caveat to the processor's nested exception capability. If the processor takes an exception while already in kernel mode, and _cannot push its old state onto the kernel stack_ for any reason such as lack of stack space, then there is nothing the processor can do to recover, so it simply resets itself. Needless to say, the kernel should be designed so that this can't happen. 

> Control flow for IDT:
>   ```
>         IDT                   trapentry.S         trap.c
>      
>   +----------------+                        
>   |   &handler1    |---------> handler1:          trap (struct Trapframe *tf)
>   |                |             // do stuff      {
>   |                |             call trap          // handle the exception/interrupt
>   |                |             // ...           }
>   +----------------+
>   |   &handler2    |--------> handler2:
>   |                |            // do stuff
>   |                |            call trap
>   |                |            // ...
>   +----------------+
>          .
>          .
>          .
>   +----------------+
>   |   &handlerX    |--------> handlerX:
>   |                |             // do stuff
>   |                |             call trap
>   |                |             // ...
>   +----------------+
>   ```


### Page Faults, Breakpoints Exceptions, and System Calls

The assembly code in `trapentry.S` will call `trap` in `kern/trap.c`. Steps are:

1. Clear DF (direction flag)
2. Halt the CPU if some other CPU has called panic()
3. Re-acquire the big kernel lock if we were halted in `sched_yield`
4. Check that interrupts are disabled
5. If trapped from user mode 
    1. Acquire the big kernel lock
    2. Garbage collect if current environment is a zombie
    3. Copy trap frame (which is currently on the stack) into `curenv->env_tf`, so that running the environment will restart at the trap point and the trapframe on the stack should be ignored from here on
6. Dispatch based on what type of trap occurred using `trap_dispatch`
7. If we mode it to this point, then no other environment was scheduled, so we should return to the current environment if doing so makes sense.
    ```c
	if (curenv && curenv->env_status == ENV_RUNNING)
		env_run(curenv);
	else
		sched_yield();
    ```

The steps for `trap_dispatch`:

1. Decide what to do based on the trap number from `tf->tf_trapno`:
    1. If `T_PGFLT` (page fault), call `page_fault_handler`
    2. If `T_BRKPT` (break point exception), call `monitor` to enter the jos kernel monitor.
    3. If `T_SYSCALL` (system call), use `tf_regs`'s six register as argument to call `syscall`, the return value is stored in `tf->tf_regs.reg_eax`.
    4. If `IRQ_OFFSET+IRQ_SPURIOUS`, handle the spurious interrupts (lab4)
    5. If `IRQ_OFFSET+IRQ_TIMER`, handle clock interrupts. Only call `time_tick` on one cpu. Then use `lapic_eoi` to acknowledge the interrupt and call `sched_yield`. (lab4 preemptive multi-tasking)
    6. If `IRQ_OFFSET+IRQ_KBD`, call `kbd_intr` to handle keyboard interrupts (lab5 shell)
    7. If `IRQ_OFFSET+IRQ_SERIAL`, call `serial_intr` to handle serial interrupts (lab5 shell)
2. If none of above matches, this is unexpected trap: the user process or the kernel has a bug. Print the trapframe and exit. 

For page fault handler `page_fault_handler`:

1. Read processor's CR2 register to find the faulting address `fault_va = rcr2();`
2. Handle kernel-mode page faults: should panic `panic("page fault in kernel mode\n")`
3. Reach here means that it is user mode page fault. 
    1. Call the environment's page fault upcall, if it exists. Details:
        1. Set up a page fault stack frame on the user exception stack (below UXSTACKTOP), then branch to `curenv->env_pgfault_upcall`. `struct UTrapframe` looks like:
        ```c
        struct UTrapframe {
        	/* information about the fault */
        	uint32_t utf_fault_va;	/* va for T_PGFLT, 0 otherwise */
        	uint32_t utf_err;
        	/* trap-time return state */
        	struct PushRegs utf_regs;
        	uintptr_t utf_eip;
        	uint32_t utf_eflags;
        	/* the trap-time stack to return to */
        	uintptr_t utf_esp;
        } __attribute__((packed));
        ```
        If this is the first time enter the page fault (another situation is nested page fault), just save old `esp` and set new `esp` to user exception stack. If this is the page fault occurred during the handling of another page fault (i.e. the `esp` has already pointed to the user exception stack), then an extra word should be leaved between the current top of the exception stack and the new stack frame because the exception stack _is_ the trap-time stack. 
        2. Save `fault_va, tf_regs, tf_eflags, tf_eip, tf_err` to the user trap frame. 
        3. Set `tf->tf_eip` to the `curenv->env_pgfault_upcall`. Rerun current environment, then the environment will start to run from the page fault handler registered earlier. 
        ```c
	    struct UTrapframe *p;
    
	    if (tf->tf_esp >= UXSTACKTOP-PGSIZE && tf->tf_esp < UXSTACKTOP) {
	    	// esp is already on the user exception stack 
	    	user_mem_assert(curenv, (void *)(tf->tf_esp-4-sizeof(struct UTrapframe)), sizeof(struct UTrapframe)+4, PTE_W);
	    	tf->tf_esp -= 4;      // push 4 bytes
	    	*((int32_t *)tf->tf_esp) = 0;
	    	tf->tf_esp -= sizeof(struct UTrapframe);
	    	p = (struct UTrapframe *)tf->tf_esp;
	    	p->utf_esp = tf->tf_esp+sizeof(struct UTrapframe)+4;
	    }
	    else {
	    	p = (struct UTrapframe *)(UXSTACKTOP-sizeof(struct UTrapframe));
	    	user_mem_assert(curenv, (void *)p, sizeof(struct UTrapframe), PTE_W);
	    	p->utf_esp = tf->tf_esp;
	    	tf->tf_esp = (uintptr_t)p;
	    }
	    p->utf_fault_va = fault_va;
	    p->utf_regs = tf->tf_regs;
	    p->utf_eflags = tf->tf_eflags;
	    p->utf_eip = tf->tf_eip;
	    p->utf_err = tf->tf_err;
    
	    tf->tf_eip = (uintptr_t)(curenv->env_pgfault_upcall);
	    env_run(curenv);
        ```
    2. If upcall doesn't exist, destroy the environment.


For system calls `T_SYSCALL`:

```c
	case T_SYSCALL:
		saved_syscall_num = tf->tf_regs.reg_eax;
		// cprintf("before syscall num: %d\n", saved_syscall_num);
		r = syscall(tf->tf_regs.reg_eax, 
							tf->tf_regs.reg_edx,
							tf->tf_regs.reg_ecx,
							tf->tf_regs.reg_ebx,
							tf->tf_regs.reg_edi,
							tf->tf_regs.reg_esi);
		// cprintf("syscall num: %d, errno: %d\n", saved_syscall_num, r);
		// if (r < 0)
		//	panic("syscall: %e\n", r);
		// curenv->env_tf.tf_regs.reg_eax = r;
		tf->tf_regs.reg_eax = r;
		return;
``` 

inside `syscall` function in `kern/syscall.c`, it will dispatch the syscall according to its system call number:

```c
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");

	switch (syscallno) {
	case SYS_cputs:
    //...
```

User interface for system call in inside `lib/syscall.c`:

```c
void
sys_cputs(const char *s, size_t len)
{
	syscall(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}
// ... 
```


```c
static inline int32_t
syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile("int %1\n"
		     : "=a" (ret)
		     : "i" (T_SYSCALL),
		       "a" (num),
		       "d" (a1),
		       "c" (a2),
		       "b" (a3),
		       "D" (a4),
		       "S" (a5)
		     : "cc", "memory");

	if(check && ret > 0)
		panic("syscall %d returned %d (> 0)", num, ret);

	return ret;
}
```



