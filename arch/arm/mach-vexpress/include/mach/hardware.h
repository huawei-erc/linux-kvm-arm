#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

/* macro to get at MMIO space when running virtually */
#define IO_ADDRESS(x) (((x) & 0x0fffffff) + (((x) >> 4) & 0x0f000000) + 0xf0000000)
#define __io_address(n) ((void __iomem __force *)IO_ADDRESS(n))

#endif /* __ASM_ARCH_HARDWARE_H */
