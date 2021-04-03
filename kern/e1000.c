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
rx_init()
{
    // 1. Program the Receive Address Register(s) (RAL/RAH) with the desired Ethernet addresses.
    // RAL[0]/RAH[0] should always be used to store the Individual Ethernet MAC address of the
    // Ethernet controller. This can come from the EEPROM or from any other means (for example, on
    // some machines, this comes from the system PROM not the EEPROM on the adapter port).
    e1000va[E1000_RAL/4] = MAC_LOW;
    e1000va[E1000_RAH/4] = MAC_HIGH | E1000_RAH_AV;
    // 2. Initialize the MTA (Multicast Table Array) to 0b. Per software, entries can be added to this table as
    // desired.
    e1000va[E1000_MTA/4] = 0;
    // 3. Program the Interrupt Mask Set/Read (IMS) register to enable any interrupt the software driver
    // wants to be notified of when the event occurs. Suggested bits include RXT, RXO, RXDMT,
    // RXSEQ, and LSC. There is no immediate reason to enable the transmit interrupts.
    // 4. If software uses the Receive Descriptor Minimum Threshold Interrupt, the Receive Delay Timer
    // (RDTR) register should be initialized with the desired delay time.
    // * not needed now
    // 5. Allocate a region of memory for the receive descriptor list. Software should insure this memory is
    // aligned on a paragraph (16-byte) boundary. Program the Receive Descriptor Base Address
    // (RDBAL/RDBAH) register(s) with the address of the region. RDBAL is used for 32-bit addresses
    // and both RDBAL and RDBAH are used for 64-bit addresses.
    struct PageInfo *p = page_alloc(ALLOC_ZERO);
    if(p == NULL)
        return -E_NO_MEM;
    physaddr_t pa = page2pa(p);
    rx_descs = page2kva(p);
    e1000va[E1000_RDBAL/4] = pa; 
    e1000va[E1000_RDBAH/4] = 0;
    // 6. Set the Receive Descriptor Length (RDLEN) register to the size (in bytes) of the descriptor ring.
    // This register must be 128-byte aligned.
    e1000va[E1000_RDLEN/4] = RXDESCSIZE; 
    // 7. The Receive Descriptor Head and Tail registers are initialized (by hardware) to 0b after a power-on
    // or a software-initiated Ethernet controller reset. Receive buffers of appropriate size should be
    // allocated and pointers to these buffers should be stored in the receive descriptor ring. Software
    // initializes the Receive Descriptor Head (RDH) register and Receive Descriptor Tail (RDT) with the
    // appropriate head and tail addresses. Head should point to the first valid receive descriptor in the
    // descriptor ring and tail should point to one descriptor beyond the last valid descriptor in the
    // descriptor ring.
    for(int i = 0; i < RXDESCSIZE; i++) {
        rx_descs[i].addr = PADDR(&rx_bufs[i]);
    }
    e1000va[E1000_RDH/4] = 0; // index 
    e1000va[E1000_RDT/4] = RXDESCSIZE-1; 
    // 8. Program the Receive Control (RCTL) register with appropriate values for desired operation to
    // include the following:
    // • Set the receiver Enable (RCTL.EN) bit to 1b for normal operation. However, it is best to leave
    // the Ethernet controller receive logic disabled (RCTL.EN = 0b) until after the receive
    // descriptor ring has been initialized and software is ready to process received packets.
    // • Set the Long Packet Enable (RCTL.LPE) bit to 1b when processing packets greater than the
    // standard Ethernet packet size. For example, this bit would be set to 1b when processing Jumbo
    // Frames.
    // • Loopback Mode (RCTL.LBM) should be set to 00b for normal operation.
    // • Configure the Receive Descriptor Minimum Threshold Size (RCTL.RDMTS) bits to the
    // desired value.
    // • Configure the Multicast Offset (RCTL.MO) bits to the desired value.
    // • Set the Broadcast Accept Mode (RCTL.BAM) bit to 1b allowing the hardware to accept
    // broadcast packets.
    // • Configure the Receive Buffer Size (RCTL.BSIZE) bits to reflect the size of the receive buffers
    // software provides to hardware. Also configure the Buffer Extension Size (RCTL.BSEX) bits if
    // receive buffer needs to be larger than 2048 bytes.
    // • Set the Strip Ethernet CRC (RCTL.SECRC) bit if the desire is for hardware to strip the CRC
    // prior to DMA-ing the receive packet to host memory.
    // • For the 82541xx and 82547GI/EI , program the Interrupt Mask Set/Read (IMS) register to
    // enable any interrupt the driver wants to be notified of when the even occurs. Suggested bits
    // include RXT, RXO, RXDMT, RXSEQ, and LSC. There is no immediate reason to enable the
    // transmit interrupts. Plan to optimize interrupts later, including programming the interrupt
    // moderation registers TIDV, TADV, RADV and IDTR.
    // • For the 82541xx and 82547GI/EI , if software uses the Receive Descriptor Minimum
    // Threshold Interrupt, the Receive Delay Timer (RDTR) register should be initialized with the
    // desired delay time.
    e1000va[E1000_RCTL/4] |= E1000_RCTL_EN;
    // don't set Jumbo Frame
    // e1000va[E1000_RCTL/4] |= E1000_RCTL_LPE; 
    // normal loop back mode
    e1000va[E1000_RCTL/4] |= E1000_RCTL_LBM_NO;
    // set broadcast accept mode
    e1000va[E1000_RCTL/4] |= E1000_RCTL_BAM;
    // configure receive buffer size 
    e1000va[E1000_RCTL/4] |= E1000_RCTL_SZ_2048;
    // strip ethernet CRC 
    e1000va[E1000_RCTL/4] |= E1000_RCTL_SECRC;
    return 0;
}

int  
rx_data(char *buf, size_t len)
{
    int tail = e1000va[E1000_RDT/4];
    int idx = (tail+1)%RXDESCSIZE;
    if(!(rx_descs[idx].status & E1000_RXD_STAT_DD))
        return -E_RXQUEUE_EMPTY;
    physaddr_t pa = rx_descs[idx].addr;
    int res = rx_descs[idx].length;
    res = MIN(res, len);
    //memcpy(buf, rx_bufs[idx], res);
    memcpy(buf, (void *)(KADDR(pa)), res);
    //rx_descs[idx].status &= ~E1000_RXD_STAT_DD;
    e1000va[E1000_RDT/4] = idx;
    return res;
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
    rx_init();
	return 0;
}
