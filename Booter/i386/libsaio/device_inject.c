/*
 *	Copyright 2009 Jasmin Fazlic All rights reserved.
 */
/*
 *	Cleaned and merged by iNDi
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "pci.h"
#include "pci_root.h"
#include "device_inject.h"


#ifndef DEBUG_INJECT
#define DEBUG_INJECT 0
#endif

#if DEBUG_INJECT
#define DBG(x...)	printf(x)
#else
#define DBG(x...)
#endif

uint32_t devices_number = 1;
uint32_t builtin_set = 0;
struct DevPropString *string = 0;
uint8_t *stringdata = 0;
uint32_t stringlength = 0;

char *efi_inject_get_devprop_string(uint32_t *len)
{
	if(string) {
		*len = string->length;
		return devprop_generate_string(string);
	}
	verbose("efi_inject_get_devprop_string NULL trying stringdata\n");
	return NULL;
}

void *convertHexStr2Binary(const char *hexStr, int *outLength)
{
  int len;
  char hexNibble;
  char hexByte[2];
  uint8_t binChar;
  uint8_t *binStr;
  int hexStrIdx, binStrIdx, hexNibbleIdx;

  len = strlen(hexStr);
  if (len > 1)
  {
    // the resulting binary will be the half size of the input hex string
    binStr = MALLOC(len / 2);
    binStrIdx = 0;
    hexNibbleIdx = 0;
    for (hexStrIdx = 0; hexStrIdx < len; hexStrIdx++)
    {
      hexNibble = hexStr[hexStrIdx];
      
      // ignore all chars except valid hex numbers
      if (hexNibble >= '0' && hexNibble <= '9'
        || hexNibble >= 'A' && hexNibble <= 'F'
        || hexNibble >= 'a' && hexNibble <= 'f')
      {
        hexByte[hexNibbleIdx++] = hexNibble;
        
        // found both two nibbles, convert to binary
        if (hexNibbleIdx == 2)
        {
          binChar = 0;
          
          for (hexNibbleIdx = 0; hexNibbleIdx < sizeof(hexByte); hexNibbleIdx++)
          {
            if (hexNibbleIdx > 0) binChar = binChar << 4;
            
            if (hexByte[hexNibbleIdx] <= '9') binChar += hexByte[hexNibbleIdx] - '0';
            else if (hexByte[hexNibbleIdx] <= 'F') binChar += hexByte[hexNibbleIdx] - ('A' - 10);
            else if (hexByte[hexNibbleIdx] <= 'f') binChar += hexByte[hexNibbleIdx] - ('a' - 10);
          }
          
          binStr[binStrIdx++] = binChar;						
          hexNibbleIdx = 0;
        }
      }
    }
    *outLength = binStrIdx;
    return binStr;
  }
  else
  {
    *outLength = 0;
    return NULL;
  }
}

void setupDeviceProperties(Node *node)
{
  const char *val;
  uint8_t *binStr;
  int cnt, cnt2;

  static char DEVICE_PROPERTIES_PROP[] = "device-properties";

  /* Generate devprop string.
   */
  uint32_t strlength;
  char *string = efi_inject_get_devprop_string(&strlength);

  /* Use the static "device-properties" boot config key contents if available,
   * otheriwse use the generated one.
   */  
  if (!getValueForKey(kDeviceProperties, &val, &cnt, &bootInfo->bootConfig) && string)
  {
    val = (const char*)string;
    cnt = strlength * 2;
  } 
    
  if (cnt > 1)
  {
    binStr = convertHexStr2Binary(val, &cnt2);
    if (cnt2 > 0) DT__AddProperty(node, DEVICE_PROPERTIES_PROP, cnt2, binStr);
  }
}

struct DevPropString *devprop_create_string(void)
{
	string = (struct DevPropString*)MALLOC(sizeof(struct DevPropString));
	
	if(string == NULL)
		return NULL;
	
	memset(string, 0, sizeof(struct DevPropString));
	string->length = 12;
	string->WHAT2 = 0x01000000;
	return string;
}
 
struct DevPropDevice *devprop_add_device(struct DevPropString *string, char *path)
{
	struct DevPropDevice	*device;
	const char		pciroot_string[] = "PciRoot(0x";
	const char		pci_device_string[] = "Pci(0x";

	if (string == NULL || path == NULL) {
		return NULL;
	}
	device = MALLOC(sizeof(struct DevPropDevice));

	if (strncmp(path, pciroot_string, strlen(pciroot_string))) {
		printf("ERROR parsing device path\n");
		return NULL;
	}

	memset(device, 0, sizeof(struct DevPropDevice));
	device->acpi_dev_path._UID = getPciRootUID();

	int numpaths = 0;
	int		x, curr = 0;
	char	buff[] = "00";

	for (x = 0; x < strlen(path); x++) {
		if (!strncmp(&path[x], pci_device_string, strlen(pci_device_string))) {
			x+=strlen(pci_device_string);
			curr=x;
			while(path[++x] != ',');
			if(x-curr == 2)
				sprintf(buff, "%c%c", path[curr], path[curr+1]);
			else if(x-curr == 1)
				sprintf(buff, "%c", path[curr]);
			else 
			{
				printf("ERROR parsing device path\n");
				numpaths = 0;
				break;
			}
			device->pci_dev_path[numpaths].device = strtoul(buff, NULL, 16);
			
			x += 3; // 0x
			curr = x;
			while(path[++x] != ')');
			if(x-curr == 2)
				sprintf(buff, "%c%c", path[curr], path[curr+1]);
			else if(x-curr == 1)
				sprintf(buff, "%c", path[curr]);
			else
			{
				printf("ERROR parsing device path\n");
				numpaths = 0;
				break;
			}
			device->pci_dev_path[numpaths].function = strtoul(buff, NULL, 16);
			
			numpaths++;
		}
	}
	
	if(!numpaths)
		return NULL;
	
	device->numentries = 0x00;
	
	device->acpi_dev_path.length = 0x0c;
	device->acpi_dev_path.type = 0x02;
	device->acpi_dev_path.subtype = 0x01;
	device->acpi_dev_path._HID = 0xd041030a;
	
	device->num_pci_devpaths = numpaths;
	device->length = 24 + (6*numpaths);
	
	int		i; 
	
	for(i = 0; i < numpaths; i++)
	{
		device->pci_dev_path[i].length = 0x06;
		device->pci_dev_path[i].type = 0x01;
		device->pci_dev_path[i].subtype = 0x01;
	}
	
	device->path_end.length = 0x04;
	device->path_end.type = 0x7f;
	device->path_end.subtype = 0xff;
	
	device->string = string;
	device->data = NULL;
	string->length += device->length;
	
	if(!string->entries)
		if((string->entries = (struct DevPropDevice**)MALLOC(sizeof(device)))== NULL)
			return 0;
	
	string->entries[string->numentries++] = (struct DevPropDevice*)MALLOC(sizeof(device));
	string->entries[string->numentries-1] = device;
	
	return device;
}

int devprop_add_value(struct DevPropDevice *device, char *nm, uint8_t *vl, uint32_t len)
{
	
	if(!nm || !vl || !len)
		return 0;
	
	uint32_t length = ((strlen(nm) * 2) + len + (2 * sizeof(uint32_t)) + 2);
	uint8_t *data = (uint8_t*)MALLOC(length);
	{
		if(!data)
			return 0;
		
		memset(data, 0, length);
		uint32_t off= 0;
		data[off+1] = ((strlen(nm) * 2) + 6) >> 8;
		data[off] =   ((strlen(nm) * 2) + 6) & 0x00FF;
		
		off += 4;
		uint32_t i=0, l = strlen(nm);
		for(i = 0 ; i < l ; i++, off += 2)
		{
			data[off] = *nm++;
		}
		
		off += 2;
		l = len;
		uint32_t *datalength = (uint32_t*)&data[off];
		*datalength = l + 4;
		off += 4;
		for(i = 0 ; i < l ; i++, off++)
		{
			data[off] = *vl++;
		}
	}	
	
	uint32_t offset = device->length - (24 + (6 * device->num_pci_devpaths));
	
	uint8_t *newdata = (uint8_t*)MALLOC((length + offset));
	if(!newdata)
		return 0;
	if(device->data)
		if(offset > 1)
			memcpy(newdata, device->data, offset);

	memcpy(newdata + offset, data, length);
	
	device->length += length;
	device->string->length += length;
	device->numentries++;
	
	if(!device->data)
		device->data = (uint8_t*)MALLOC(sizeof(uint8_t));
	else
		free(device->data);
	
	free(data);
	device->data = newdata;
	
	return 1;
}

char *devprop_generate_string(struct DevPropString *string)
{
	char *buffer = (char*)MALLOC(string->length * 2);
	char *ptr = buffer;
	
	if(!buffer)
		return NULL;

	sprintf(buffer, "%08x%08x%04x%04x", swap32(string->length), string->WHAT2,
			swap16(string->numentries), string->WHAT3);
	buffer += 24;
	int i = 0, x = 0;
	
	while(i < string->numentries)
	{
		sprintf(buffer, "%08x%04x%04x", swap32(string->entries[i]->length),
				swap16(string->entries[i]->numentries), string->entries[i]->WHAT2);
		
		buffer += 16;
		sprintf(buffer, "%02x%02x%04x%08x%08x", string->entries[i]->acpi_dev_path.type,
				string->entries[i]->acpi_dev_path.subtype,
				swap16(string->entries[i]->acpi_dev_path.length),
				string->entries[i]->acpi_dev_path._HID,
				swap32(string->entries[i]->acpi_dev_path._UID));

		buffer += 24;
		for(x=0;x < string->entries[i]->num_pci_devpaths; x++)
		{
			sprintf(buffer, "%02x%02x%04x%02x%02x", string->entries[i]->pci_dev_path[x].type,
					string->entries[i]->pci_dev_path[x].subtype,
					swap16(string->entries[i]->pci_dev_path[x].length),
					string->entries[i]->pci_dev_path[x].function,
					string->entries[i]->pci_dev_path[x].device);
			buffer += 12;
		}
		
		sprintf(buffer, "%02x%02x%04x", string->entries[i]->path_end.type,
				string->entries[i]->path_end.subtype,
				swap16(string->entries[i]->path_end.length));
		
		buffer += 8;
		uint8_t *dataptr = string->entries[i]->data;
		for(x = 0; x < (string->entries[i]->length) - (24 + (6 * string->entries[i]->num_pci_devpaths)) ; x++)
		{
			sprintf(buffer, "%02x", *dataptr++);
			buffer += 2;
		}
		i++;
	}
	return ptr;
}

void devprop_free_string(struct DevPropString *string)
{
	if(!string)
		return;
	
	int i;
	for(i = 0; i < string->numentries; i++)
	{
		if(string->entries[i])
		{
			if(string->entries[i]->data)
			{
				free(string->entries[i]->data);
				string->entries[i]->data = NULL;
			}
			free(string->entries[i]);
			string->entries[i] = NULL;
		}
	}
	
	free(string);
	string = NULL;
}

/* a fine place for this code */

int devprop_add_network_template(struct DevPropDevice *device, uint16_t vendor_id)
{
	if(!device)
		return 0;
	uint8_t builtin = 0x0;
	if((vendor_id != 0x168c) && (builtin_set == 0)) 
	{
		builtin_set = 1;
		builtin = 0x01;
	}
	if(!devprop_add_value(device, "built-in", (uint8_t*)&builtin, 1))
		return 0;
	devices_number++;
	return 1;
}

void set_eth_builtin(pci_dt_t *eth_dev)
{
	char *devicepath = get_pci_dev_path(eth_dev);
	struct DevPropDevice *device = (struct DevPropDevice*)MALLOC(sizeof(struct DevPropDevice));

	verbose("LAN Controller [%04x:%04x] :: %s\n", eth_dev->vendor_id, eth_dev->device_id, devicepath);

	if (!string)
		string = devprop_create_string();

	device = devprop_add_device(string, devicepath);
	if(device)
	{
		verbose("Setting up lan keys\n");
		devprop_add_network_template(device, eth_dev->vendor_id);
		stringdata = (uint8_t*)MALLOC(sizeof(uint8_t) * string->length);
		if(stringdata)
		{
			memcpy(stringdata, (uint8_t*)devprop_generate_string(string), string->length);
			stringlength = string->length;
		}
	}
}
