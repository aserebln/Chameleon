/*
 *	Copyright 2009 Jasmin Fazlic All rights reserved.
 */
/*
 *	Cleaned and merged by iNDi
 */

#ifndef __LIBSAIO_DEVICE_INJECT_H
#define __LIBSAIO_DEVICE_INJECT_H

#define DP_ADD_TEMP_VAL(dev, val) devprop_add_value(dev, (char*)val[0], (uint8_t*)val[1], strlen(val[1]) + 1)
#define DP_ADD_TEMP_VAL_DATA(dev, val) devprop_add_value(dev, (char*)val.name, (uint8_t*)val.data, val.size)
#define MAX_PCI_DEV_PATHS 4

extern struct DevPropString *string;
extern uint8_t *stringdata;
extern uint32_t stringlength;
extern void *convertHexStr2Binary(const char *hexStr, int *outLength);
extern void setupDeviceProperties(Node *node);

struct ACPIDevPath {
	uint8_t		type;		// = 2 ACPI device-path
	uint8_t		subtype;	// = 1 ACPI Device-path
	uint16_t	length;		// = 0x0c
	uint32_t	_HID;		// = 0xD041030A ?
	uint32_t	_UID;		// = 0x00000000 PCI ROOT
};

struct PCIDevPath {
	uint8_t		type;		// = 1 Hardware device-path
	uint8_t		subtype;	// = 1 PCI
	uint16_t	length;		// = 6
	uint8_t		function;	// pci func number
	uint8_t		device;		// pci dev number
};

struct DevicePathEnd {
	uint8_t		type;		// = 0x7f
	uint8_t		subtype;	// = 0xff
	uint16_t	length;		// = 4;
};

struct DevPropDevice {
	uint32_t length;
	uint16_t numentries;
	uint16_t WHAT2;										// 0x0000 ?
	struct ACPIDevPath acpi_dev_path;					// = 0x02010c00 0xd041030a
	struct PCIDevPath  pci_dev_path[MAX_PCI_DEV_PATHS]; // = 0x01010600 func dev
	struct DevicePathEnd path_end;						// = 0x7fff0400
	uint8_t *data;
	
	// ------------------------
	uint8_t	 num_pci_devpaths;
	struct DevPropString *string;
	// ------------------------	
};

struct DevPropString {
	uint32_t length;
	uint32_t WHAT2;			// 0x01000000 ?
	uint16_t numentries;
	uint16_t WHAT3;			// 0x0000     ?
	struct DevPropDevice **entries;
};

char			*efi_inject_get_devprop_string(uint32_t *len);
int			devprop_add_network_template(struct DevPropDevice *device, uint16_t vendor_id);
struct DevPropString	*devprop_create_string(void);
struct DevPropDevice	*devprop_add_device(struct DevPropString *string, char *path);
int			devprop_add_value(struct DevPropDevice *device, char *nm, uint8_t *vl, uint32_t len);
char			*devprop_generate_string(struct DevPropString *string);
void			devprop_free_string(struct DevPropString *string);

#endif /* !__LIBSAIO_DEVICE_INJECT_H */
