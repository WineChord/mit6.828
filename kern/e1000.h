#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include "pmap.h" // mmio_map_region

#define E1000_DEV_ID_82540EM             0x100E
#define E1000_VEND_ID_82540EM            0x8086

volatile uint32_t *e1000va;

#endif  // SOL >= 6
