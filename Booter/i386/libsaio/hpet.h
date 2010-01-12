/*
 * 
 */

#ifndef __LIBSAIO_HPET_H
#define __LIBSAIO_HPET_H

#include "libsaio.h"

#define REG32(base, reg)  ((volatile uint32_t *)base)[(reg) >> 2]

void force_enable_hpet(pci_dt_t *lpc_dev);

struct lpc_controller_t {
	unsigned vendor;
	unsigned device;
	char *name;
};

#endif /* !__LIBSAIO_HPET_H */
