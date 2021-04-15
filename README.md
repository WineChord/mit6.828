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