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
	cr0 |= CR0_PE|CR0_PG|CR0_AM|CR0_WP|CR0_NE|CR0_MP;
	cr0 &= ~(CR0_TS|CR0_EM);
	lcr0(cr0);
```

