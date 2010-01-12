/*
 *  ATI injector
 *
 *  Copyright (C) 2009  Jasmin Fazlic, iNDi, netkas
 *
 *  ATI injector is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ATI driver and injector is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ATI injector.  If not, see <http://www.gnu.org/licenses/>.
 */
 /*
 * Alternatively you can choose to comply with APSL
 */
 

#ifndef __LIBSAIO_ATI_H
#define __LIBSAIO_ATI_H

bool setup_ati_devprop(pci_dt_t *ati_dev);

struct ati_chipsets_t {
	unsigned device;
	char *name;
};

struct ati_data_key {
	uint32_t size;
	char *name;
	uint8_t data[];
};

#define REG8(reg)  ((volatile uint8_t *)regs)[(reg)]
#define REG16(reg)  ((volatile uint16_t *)regs)[(reg) >> 1]
#define REG32R(reg)  ((volatile uint32_t *)regs)[(reg) >> 2]
#define REG32W(reg, val)  ((volatile uint32_t *)regs)[(reg) >> 2] = (val)


#endif /* !__LIBSAIO_ATI_H */
