// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	// panic("pgfault not implemented");

	if (!((err&FEC_WR) && (uvpt[PGNUM(addr)]&PTE_COW))) 
		panic("fault is not write to cow\n");
	
	void *va = (void *)(PGNUM(addr)<<PGSHIFT);

	// allocate a new page, map it at PFTEMP (syscall 1)
	if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e\n", r);
	// copy the data from the old page to the new page 
	memmove((void *)PFTEMP, va, PGSIZE);
	// move the new page to the old page's address (syscall 2)
	if ((r = sys_page_map(0, (void *)PFTEMP, 0, va, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e\n", r);
	// unmap temp location (syscall 3)
	if ((r = sys_page_unmap(0, (void *)PFTEMP)) < 0)
		panic("sys_page_unmap: %e\n", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	// panic("duppage not implemented");
	if(uvpt[pn]&PTE_SHARE) {
		void *pg = (void *)(pn*PGSIZE);
		if((r = sys_page_map(0, pg, envid, pg, PTE_SYSCALL)) < 0)
			panic("sys_page_map: %e\n", r);
	} else if ((uvpt[pn]&PTE_W) || (uvpt[pn]&PTE_COW)) {
		void *pg = (void *)(pn*PGSIZE);
		// why the order matters here?
		if ((r = sys_page_map(0, pg, envid, pg, PTE_P|PTE_U|PTE_COW)) < 0)
			panic("sys_page_map: %e\n", r);
		// remap to cow as well 
		if ((r = sys_page_map(0, pg, 0, pg, PTE_P|PTE_U|PTE_COW)) < 0)
			panic("sys_page_map: %e\n", r);
	} else if (uvpt[pn]&PTE_P) {
		// only present: read-only
		void *pg = (void *)(pn*PGSIZE);
		if ((r = sys_page_map(0, pg, envid, pg, PTE_P|PTE_U)) < 0)
			panic("sys_page_map: %e\n", r);
	}
	return 0;
}

static int
dduppage(envid_t envid, unsigned pn)
{
	int r;
	void *addr = (void *)(pn*PGSIZE);
	envid_t dstenv = envid;
	if ((r = sys_page_alloc(dstenv, addr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	if ((r = sys_page_map(dstenv, addr, 0, UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	memmove(UTEMP, addr, PGSIZE);
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
	return 0;
}

extern void _pgfault_upcall(void);

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// panic("fork not implemented");
	envid_t eid;
	uint8_t *addr;
	int r,i;
	extern unsigned char end[];
	set_pgfault_handler(pgfault);
	eid = sys_exofork();
	if (eid < 0) 
		panic("sys_exofork: %e\n", eid);
	if (eid == 0) {
		// we are the child 
		// change thisenv
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	// we are the parent
	for (addr = (uint8_t *) 0; addr < (uint8_t *)(USTACKTOP); addr += PGSIZE) {
		i = PGNUM(addr);
		if (!((uvpd[i>>10]&PTE_P)&&(uvpt[i]&PTE_P)))
			continue; 
		duppage(eid, i);
	}

	r = sys_page_alloc(eid, (void *)(UXSTACKTOP-PGSIZE), PTE_P|PTE_U|PTE_W);
	if (r < 0)
		panic("sys_page_alloc: %e\n", r);
	r = sys_env_set_pgfault_upcall(eid, _pgfault_upcall);
	if (r < 0)
		panic("sys_env_set_pgfault_upcall: %e\n", r);

	if ((r = sys_env_set_status(eid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e\n", r);
	return eid;
}

static int
duppage0(envid_t envid, unsigned pn)
{
	int r;
	void *pg = (void *)(pn*PGSIZE);
	if (uvpt[pn]&PTE_W) {
		if ((r = sys_page_map(0, pg, envid, pg, PTE_P|PTE_U|PTE_W)) < 0)
			panic("sys_page_map: %e\n", r);
	} else if (uvpt[pn]&PTE_P) {
		// only present: read-only
		if ((r = sys_page_map(0, pg, envid, pg, PTE_P|PTE_U)) < 0)
			panic("sys_page_map: %e\n", r);
	}
	return 0;
}

// Challenge!
int
sfork(void)
{
	// panic("sfork not implemented");
	envid_t eid;
	uint8_t *addr;
	int r,i;
	extern unsigned char end[];
	set_pgfault_handler(pgfault);
	eid = sys_exofork();
	if (eid < 0) 
		panic("sys_exofork: %e\n", eid);
	if (eid == 0) {
		// we are the child 
		// change thisenv
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	// we are the parent
	for (addr = (uint8_t *) 0; addr < (uint8_t *)(USTACKTOP-PGSIZE); addr += PGSIZE) {
		i = PGNUM(addr);
		if (!((uvpd[i>>10]&PTE_P)&&(uvpt[i]&PTE_P)))
			continue; 
		duppage0(eid, i);
	}
	duppage(eid, PGNUM(&addr));

	r = sys_page_alloc(eid, (void *)(UXSTACKTOP-PGSIZE), PTE_P|PTE_U|PTE_W);
	if (r < 0)
		panic("sys_page_alloc: %e\n", r);
	r = sys_env_set_pgfault_upcall(eid, _pgfault_upcall);
	if (r < 0)
		panic("sys_env_set_pgfault_upcall: %e\n", r);

	if ((r = sys_env_set_status(eid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e\n", r);
	return eid;
}
