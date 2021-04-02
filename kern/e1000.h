#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pmap.h> // mmio_map_region
#include <kern/pci.h>
#include <inc/error.h>
#include <inc/string.h> // memcpy

#define E1000_DEV_ID_82540EM             0x100E
#define E1000_VEND_ID_82540EM            0x8086

#define E1000_STATUS   0x00008  /* Device Status - RO */
#define E1000_TCTL     0x00400  /* TX Control - RW */
#define E1000_TIPG     0x00410  /* TX Inter-packet gap -RW */
#define E1000_TDBAL    0x03800  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808  /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810  /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818  /* TX Descripotr Tail - RW */

/* Transmit Descriptor bit definitions */
#define E1000_TXD_DTYP_D     0x00100000 /* Data Descriptor */
#define E1000_TXD_DTYP_C     0x00000000 /* Context Descriptor */
#define E1000_TXD_POPTS_IXSM 0x01       /* Insert IP checksum */
#define E1000_TXD_POPTS_TXSM 0x02       /* Insert TCP/UDP checksum */
#define E1000_TXD_CMD_EOP    0x01       /* End of Packet */
#define E1000_TXD_CMD_IFCS   0x02       /* Insert FCS (Ethernet CRC) */
#define E1000_TXD_CMD_IC     0x04       /* Insert Checksum */
#define E1000_TXD_CMD_RS     0x08       /* Report Status */
#define E1000_TXD_CMD_RPS    0x10       /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT   0x20       /* Descriptor extension (0 = legacy) */
#define E1000_TXD_CMD_VLE    0x40       /* Add VLAN tag */
#define E1000_TXD_CMD_IDE    0x80       /* Enable Tidv register */
#define E1000_TXD_STAT_DD    0x01       /* Descriptor Done */
#define E1000_TXD_STAT_EC    0x02       /* Excess Collisions */
#define E1000_TXD_STAT_LC    0x04       /* Late Collisions */
#define E1000_TXD_STAT_TU    0x08       /* Transmit underrun */
#define E1000_TXD_CMD_TCP    0x01       /* TCP packet */
#define E1000_TXD_CMD_IP     0x02       /* IP packet */
#define E1000_TXD_CMD_TSE    0x04       /* TCP Seg enable */
#define E1000_TXD_STAT_TC    0x04       /* Tx Underrun */

#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_CT_ETH 0x00000100    /* Ethernet is 10h */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */
#define E1000_TCTL_COLD_F 0x00040000    /* for full duplex */


struct tx_desc
{
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
}__attribute__((packed));


#define NTXDESC 32 // # of transmit descriptors
#define TXDESCSIZE (NTXDESC*sizeof(struct tx_desc))
#define TXBUFLEN 1520 // the maximum size of an Ethernet packet is 1518 bytes
#define TXBUFBASE (NTXDESC*sizeof(struct tx_desc))

volatile uint32_t *e1000va; // virtual address for E1000 mmio region

struct tx_buf
{
    char buf[TXBUFLEN];
}tx_bufs[NTXDESC];


struct tx_desc *tx_descs; 

int
pci_init_attach(struct pci_func *f);

#endif  // SOL >= 6
