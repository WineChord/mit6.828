#include <kern/e1000.h>
#include <kern/pmap.h>

// LAB 6: Your driver code here

int tx_init()
{
	// 1. allocate a region of memory for the transmit descriptor list
	struct PageInfo *p = page_alloc(ALLOC_ZERO);
	if(p == NULL)
		return -E_NO_MEM;
	physaddr_t pa = page2pa(p);
	tx_descs = (struct tx_desc *)(page2kva(p)); 
	// 2. decide package buffer addresses and make transmit descriptors refer to them 
	for(int i = 0; i < NTXDESC; i++) {
		tx_descs[i].addr = PADDR(&tx_bufs[i]);
		// tx_descs[i].cmd = 0;
		tx_descs[i].status |= E1000_TXD_STAT_DD;
	}
	// 3. Program the Transmit Descriptor Base Address 
	// (TDBAL/TDBAH) register(s) with the address of the region. 
	// TDBAL is used for 32-bit addresses
	// and both TDBAL and TDBAH are used for 64-bit addresses
	// TDBAL is at offset 03800 (see table 13.2 page 219)
	e1000va[E1000_TDBAL/4] = pa; // E1000 interact directly with physical address
	e1000va[E1000_TDBAH/4] = 0; 
	// 4. Set the Transmit Descriptor Length (TDLEN) register to the size (in bytes) of the descriptor ring.
	// This register must be 128-byte aligned.
	e1000va[E1000_TDLEN/4] = TXDESCSIZE;
	// 5. The Transmit Descriptor Head and Tail (TDH/TDT) registers are initialized (by hardware) to 0b
	// after a power-on or a software initiated Ethernet controller reset. Software should write 0b to both
	// these registers to ensure this.
	e1000va[E1000_TDH/4] = 0;
	e1000va[E1000_TDT/4] = 0; 
	// 6. Initialize the Transmit Control Register (TCTL) for desired operation to include the following:
	// • Set the Enable (TCTL.EN) bit to 1b for normal operation.
	// • Set the Pad Short Packets (TCTL.PSP) bit to 1b.
	// • Configure the Collision Threshold (TCTL.CT) to the desired value. Ethernet standard is 10h.
	// This setting only has meaning in half duplex mode.
	// • Configure the Collision Distance (TCTL.COLD) to its expected value. For full duplex
	// operation, this value should be set to 40h. For gigabit half duplex, this value should be set to
	// 200h. For 10/100 half duplex, this value should be set to 40h.
	e1000va[E1000_TCTL/4] |= E1000_TCTL_EN;
	e1000va[E1000_TCTL/4] |= E1000_TCTL_PSP;
	e1000va[E1000_TCTL/4] |= E1000_TCTL_CT_ETH;
	e1000va[E1000_TCTL/4] |= E1000_TCTL_COLD_F; // assume full duplex
	// 7. For TIPG, refer to the default values described in table 13-77 of 
	// section 13.4.34 for the IEEE 802.3 standard IPG (don't use the values in the table in section 14.5).
	// IPGR2: 6   IPGR1: 4    IPGT: 10
	e1000va[E1000_TIPG/4] = 10; 
	return 0;
}

int 
tx_data(char *buf, size_t len)
{
    if(len > TXBUFLEN)  
        return -E_TXBUF_EXCEED; 
    int tail = e1000va[E1000_TDT/4];
    if(!(tx_descs[tail].status & E1000_TXD_STAT_DD))
        return -E_TXQUEUE_FULL;
    cprintf("transmitting %s\n", buf);
    uint32_t dst = (uint32_t)tx_descs[tail].addr;
    memcpy((void *)KADDR(dst), buf, len);
    tx_descs[tail].status &= ~E1000_TXD_STAT_DD; 
    tx_descs[tail].length = len;
    tx_descs[tail].cmd |= E1000_TXD_CMD_RS|E1000_TXD_CMD_EOP; 
    e1000va[E1000_TDT/4] = (tail+1)%NTXDESC;
    return 0;
}

void 
tx_test()
{
    char buf[15];
    for (int i = 0; i < NTXDESC+2; i++) {
        snprintf(buf, 10, "no.%d\n", i);
        int r = tx_data(buf, strlen(buf));
        cprintf("status %d: %d\n", i, r);
    }
}

int
pci_init_attach(struct pci_func *f) 
{
	pci_func_enable(f);
	e1000va = mmio_map_region((physaddr_t)f->reg_base[0], f->reg_size[0]);
	// To test your mapping, try printing out the device status register (section 13.4.2). 
	// This is a 4 byte register that starts at byte 8 of the register space. 
	// You should get 0x80080783, which indicates a full duplex link is up at 1000 MB/s, among other things.
	cprintf("device status register: %08x\n", e1000va[E1000_STATUS/4]);
	// start transmit initialization
    tx_init();
	//tx_test();
	return 0;
}
