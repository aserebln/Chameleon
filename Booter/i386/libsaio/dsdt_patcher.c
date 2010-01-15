/*
 * Copyright 2008 mackerintel
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "acpi.h"
#include "efi_tables.h"
#include "fake_efi.h"
#include "dsdt_patcher.h"
#include "platform.h"

#ifndef DEBUG_DSDT
#define DEBUG_DSDT 0
#endif

#if DEBUG_DSDT==2
#define DBG(x...)  {printf(x); sleep(1);}
#elif DEBUG_DSDT==1
#define DBG(x...)  printf(x)
#else
#define DBG(x...)
#endif

/* Gets the ACPI 1.0 RSDP address */
static struct acpi_2_rsdp* getAddressOfAcpiTable()
{
    /* TODO: Before searching the BIOS space we are supposed to search the first 1K of the EBDA */
	
    void *acpi_addr = (void*)ACPI_RANGE_START;
    for(; acpi_addr <= (void*)ACPI_RANGE_END; acpi_addr += 16)
    {
        if(*(uint64_t *)acpi_addr == ACPI_SIGNATURE_UINT64_LE)
        {
            uint8_t csum = checksum8(acpi_addr, 20);
            if(csum == 0)
            {
                // Only return the table if it is a true version 1.0 table (Revision 0)
                if(((struct acpi_2_rsdp*)acpi_addr)->Revision == 0)
                    return acpi_addr;
            }
        }
    }
    return NULL;
}

/* Gets the ACPI 2.0 RSDP address */
static struct acpi_2_rsdp* getAddressOfAcpi20Table()
{
    /* TODO: Before searching the BIOS space we are supposed to search the first 1K of the EBDA */
	
    void *acpi_addr = (void*)ACPI_RANGE_START;
    for(; acpi_addr <= (void*)ACPI_RANGE_END; acpi_addr += 16)
    {
        if(*(uint64_t *)acpi_addr == ACPI_SIGNATURE_UINT64_LE)
        {
            uint8_t csum = checksum8(acpi_addr, 20);

            /* Only assume this is a 2.0 or better table if the revision is greater than 0
             * NOTE: ACPI 3.0 spec only seems to say that 1.0 tables have revision 1
             * and that the current revision is 2.. I am going to assume that rev > 0 is 2.0.
             */

            if(csum == 0 && (((struct acpi_2_rsdp*)acpi_addr)->Revision > 0))
            {
                uint8_t csum2 = checksum8(acpi_addr, sizeof(struct acpi_2_rsdp));
                if(csum2 == 0)
                    return acpi_addr;
            }
        }
    }
    return NULL;
}


/* Setup ACPI without replacing DSDT. */
int setupAcpiNoMod()
{
//	addConfigurationTable(&gEfiAcpiTableGuid, getAddressOfAcpiTable(), "ACPI");
//	addConfigurationTable(&gEfiAcpi20TableGuid, getAddressOfAcpi20Table(), "ACPI_20");
	/* XXX aserebln why uint32 cast if pointer is uint64 ? */
	acpi10_p = (uint32_t)getAddressOfAcpiTable();
	acpi20_p = (uint32_t)getAddressOfAcpi20Table();
	addConfigurationTable(&gEfiAcpiTableGuid, &acpi10_p, "ACPI");
	if(acpi20_p) addConfigurationTable(&gEfiAcpi20TableGuid, &acpi20_p, "ACPI_20");
	return 1;
}

/* Setup ACPI. Replace DSDT if DSDT.aml is found */
int setupAcpi(void)
{
	int fd, version;
	void *new_dsdt;
	const char *dsdt_filename;
	int len;
	bool tmp;
	bool drop_ssdt;
	bool fix_restart;

	if (!getValueForKey(kDSDT, &dsdt_filename, &len, &bootInfo->bootConfig)) {
		dsdt_filename = "/Extra/DSDT.aml";
	}

	if ((fd = open_bvdev("bt(0,0)", dsdt_filename, 0)) < 0) {
		verbose("No DSDT replacement found. Leaving ACPI data as is\n");
		return setupAcpiNoMod();
	}

	// Load replacement DSDT
	new_dsdt = (void*)AllocateKernelMemory(file_size (fd));
	if (!new_dsdt) {
		printf("Couldn't allocate memory for DSDT\n");
		close(fd);
		return setupAcpiNoMod();
	}
	if (read(fd, new_dsdt, file_size(fd)) != file_size(fd)) {
		printf("Couldn't read file\n");
		close(fd);
		return setupAcpiNoMod();
	}
	close(fd);
	DBG("New DSDT Loaded in memory\n");

	drop_ssdt = getBoolForKey(kDropSSDT, &tmp, &bootInfo->bootConfig) && tmp;

	if (Platform.CPU.Vendor == 0x756E6547) {	/* Intel */
		fix_restart = true;
		getBoolForKey(kRestartFix, &fix_restart, &bootInfo->bootConfig);
	} else {
		fix_restart = false;
	}

	// Do the same procedure for both versions of ACPI
	for (version=0; version<2; version++) {
		struct acpi_2_rsdp *rsdp, *rsdp_mod;
		struct acpi_2_rsdt *rsdt, *rsdt_mod;
		int rsdplength;
		
		// Find original rsdp
		rsdp=(struct acpi_2_rsdp *)(version?getAddressOfAcpi20Table():getAddressOfAcpiTable());
		if (!rsdp)
		{
			DBG("No ACPI version %d found. Ignoring\n", version+1);
			if (version)
				addConfigurationTable(&gEfiAcpi20TableGuid, NULL, "ACPI_20");
			else
				addConfigurationTable(&gEfiAcpiTableGuid, NULL, "ACPI");
			continue;
		}
		rsdplength=version?rsdp->Length:20;

		DBG("RSDP version %d found @%x. Length=%d\n",version+1,rsdp,rsdplength);

		/* FIXME: no check that memory allocation succeeded 
		 * Copy and patch RSDP,RSDT, XSDT and FADT
		 * For more info see ACPI Specification pages 110 and following
		 */

		rsdp_mod=(struct acpi_2_rsdp *) AllocateKernelMemory(rsdplength);
		memcpy(rsdp_mod, rsdp, rsdplength);    
		rsdt=(struct acpi_2_rsdt *)(rsdp->RsdtAddress);

		DBG("RSDT @%x, Length %d\n",rsdt, rsdt->Length);
		
		if (rsdt && (uint32_t)rsdt !=0xffffffff && rsdt->Length<0x10000)
		{
			uint32_t *rsdt_entries;
			int rsdt_entries_num;
			int dropoffset=0, i;
			
			rsdt_mod=(struct acpi_2_rsdt *)AllocateKernelMemory(rsdt->Length); 
			memcpy (rsdt_mod, rsdt, rsdt->Length);
			rsdp_mod->RsdtAddress=(uint32_t)rsdt_mod;
			rsdt_entries_num=(rsdt_mod->Length-sizeof(struct acpi_2_rsdt))/4;
			rsdt_entries=(uint32_t *)(rsdt_mod+1);
			for (i=0;i<rsdt_entries_num;i++)
			{
				char *table=(char *)(rsdt_entries[i]);
				if (!table)
					continue;

				DBG("TABLE %c%c%c%c,",table[0],table[1],table[2],table[3]);

				rsdt_entries[i-dropoffset]=rsdt_entries[i];
				if (drop_ssdt && table[0]=='S' && table[1]=='S' && table[2]=='D' && table[3]=='T')
				{
					dropoffset++;
					continue;
				}
				if (table[0]=='D' && table[1]=='S' && table[2]=='D' && table[3]=='T')
				{
					DBG("DSDT found\n");
					rsdt_entries[i-dropoffset]=(uint32_t)new_dsdt;
					continue;
				}
				if (table[0]=='F' && table[1]=='A' && table[2]=='C' && table[3]=='P')
				{
					struct acpi_2_fadt *fadt, *fadt_mod;
					fadt=(struct acpi_2_fadt *)rsdt_entries[i];

					DBG("FADT found @%x, Length %d\n",fadt, fadt->Length);

					if (!fadt || (uint32_t)fadt == 0xffffffff || fadt->Length>0x10000)
					{
						printf("FADT incorrect. Not modified\n");
						continue;
					}
					
					if (fix_restart && fadt->Length < 0x81) {
						fadt_mod = (struct acpi_2_fadt *)AllocateKernelMemory(0x81);
						memcpy(fadt_mod, fadt, fadt->Length);
						fadt_mod->Length = 0x81;
					} else {                                                                                          
						fadt_mod = (struct acpi_2_fadt *)AllocateKernelMemory(fadt->Length);
						memcpy(fadt_mod, fadt, fadt->Length);
					}

					if (fix_restart) {
						fadt_mod->Flags |= 0x400;
						fadt_mod->Reset_SpaceID = 0x01;
						fadt_mod->Reset_BitWidth = 0x08;
						fadt_mod->Reset_BitOffset = 0x00;
						fadt_mod->Reset_AccessWidth = 0x01;
						fadt_mod->Reset_Address = 0x0cf9;
						fadt_mod->Reset_Value = 0x06;
						verbose("FACP: Restart Fix applied\n");
					}

					// Patch DSDT Address
					DBG("Old DSDT @%x,%x\n",fadt_mod->DSDT,fadt_mod->X_DSDT);

					fadt_mod->DSDT=(uint32_t)new_dsdt;
					if ((uint32_t)(&(fadt_mod->X_DSDT))-(uint32_t)fadt_mod+8<=fadt_mod->Length)
						fadt_mod->X_DSDT=(uint32_t)new_dsdt;

					DBG("New DSDT @%x,%x\n",fadt_mod->DSDT,fadt_mod->X_DSDT);

					// Correct the checksum
					fadt_mod->Checksum=0;
					fadt_mod->Checksum=256-checksum8(fadt_mod,fadt_mod->Length);
					
					rsdt_entries[i-dropoffset]=(uint32_t)fadt_mod;
					continue;
				}
			}

			// Correct the checksum of RSDT
			rsdt_mod->Length-=4*dropoffset;

			DBG("RSDT Original checksum %d\n", rsdt_mod->Checksum);

			rsdt_mod->Checksum=0;
			rsdt_mod->Checksum=256-checksum8(rsdt_mod,rsdt_mod->Length);

			DBG("RSDT New checksum %d at %x\n", rsdt_mod->Checksum,rsdt_mod);
		}
		else
		{
			rsdp_mod->RsdtAddress=0;
			printf("RSDT not found or RSDT incorrect\n");
		}

		if (version)
		{
			struct acpi_2_xsdt *xsdt, *xsdt_mod;

			// FIXME: handle 64-bit address correctly

			xsdt=(struct acpi_2_xsdt*) ((uint32_t)rsdp->XsdtAddress);
			DBG("XSDT @%x;%x, Length=%d\n", (uint32_t)(rsdp->XsdtAddress>>32),(uint32_t)rsdp->XsdtAddress,
					xsdt->Length);
			if (xsdt && (uint64_t)rsdp->XsdtAddress<0xffffffff && xsdt->Length<0x10000)
			{
				uint64_t *xsdt_entries;
				int xsdt_entries_num, i;
				int dropoffset=0;
				
				xsdt_mod=(struct acpi_2_xsdt*)AllocateKernelMemory(xsdt->Length); 
				memcpy(xsdt_mod, xsdt, xsdt->Length);
				rsdp_mod->XsdtAddress=(uint32_t)xsdt_mod;
				xsdt_entries_num=(xsdt_mod->Length-sizeof(struct acpi_2_xsdt))/8;
				xsdt_entries=(uint64_t *)(xsdt_mod+1);
				for (i=0;i<xsdt_entries_num;i++)
				{
					char *table=(char *)((uint32_t)(xsdt_entries[i]));
					if (!table)
						continue;
					xsdt_entries[i-dropoffset]=xsdt_entries[i];
					if (drop_ssdt && table[0]=='S' && table[1]=='S' && table[2]=='D' && table[3]=='T')
					{
						dropoffset++;
						continue;
					}					
					if (table[0]=='D' && table[1]=='S' && table[2]=='D' && table[3]=='T')
					{
						DBG("DSDT found\n");

						xsdt_entries[i-dropoffset]=(uint32_t)new_dsdt;

						DBG("TABLE %c%c%c%c@%x,",table[0],table[1],table[2],table[3],xsdt_entries[i]);
						
						continue;
					}
					if (table[0]=='F' && table[1]=='A' && table[2]=='C' && table[3]=='P')
					{
						struct acpi_2_fadt *fadt, *fadt_mod;
						fadt=(struct acpi_2_fadt *)(uint32_t)xsdt_entries[i];

						DBG("FADT found @%x,%x, Length %d\n",(uint32_t)(xsdt_entries[i]>>32),fadt, 
								 fadt->Length);

						if (!fadt || (uint64_t)xsdt_entries[i] >= 0xffffffff || fadt->Length>0x10000)
						{
							printf("FADT incorrect or after 4GB. Dropping XSDT\n");
							goto drop_xsdt;
						}

						if (fix_restart && fadt->Length < 0x81) {
							fadt_mod = (struct acpi_2_fadt *)AllocateKernelMemory(0x81);
							memcpy(fadt_mod, fadt, fadt->Length);
							fadt_mod->Length = 0x81;
						} else {
							fadt_mod = (struct acpi_2_fadt*)AllocateKernelMemory(fadt->Length); 
							memcpy(fadt_mod, fadt, fadt->Length);
						}

						if (fix_restart) {
							fadt_mod->Flags |= 0x400;
							fadt_mod->Reset_SpaceID = 0x01;
							fadt_mod->Reset_BitWidth = 0x08;
							fadt_mod->Reset_BitOffset = 0x00;
							fadt_mod->Reset_AccessWidth = 0x01;
							fadt_mod->Reset_Address = 0x0cf9;
							fadt_mod->Reset_Value = 0x06;
							verbose("FACPV2: Restart Fix applied\n");
						}

						// Patch DSDT Address
						DBG("Old DSDT @%x,%x\n",fadt_mod->DSDT,fadt_mod->X_DSDT);

						fadt_mod->DSDT=(uint32_t)new_dsdt;
						if ((uint32_t)(&(fadt_mod->X_DSDT))-(uint32_t)fadt_mod+8<=fadt_mod->Length)
							fadt_mod->X_DSDT=(uint32_t)new_dsdt;

						DBG("New DSDT @%x,%x\n",fadt_mod->DSDT,fadt_mod->X_DSDT);

						// Patch system-type into PM_Profile
						if (fadt_mod->PM_Profile != Platform.Type) {
							verbose("FACPV2: changing PM_Profile from 0x%02x->0x%02x\n", fadt_mod->PM_Profile, Platform.Type);
							fadt_mod->PM_Profile = Platform.Type;
						}

						// Correct the checksum
						fadt_mod->Checksum=0;
						fadt_mod->Checksum=256-checksum8(fadt_mod,fadt_mod->Length);
						
						xsdt_entries[i-dropoffset]=(uint32_t)fadt_mod;

						DBG("TABLE %c%c%c%c@%x,",table[0],table[1],table[2],table[3],xsdt_entries[i]);

						continue;
					}

					DBG("TABLE %c%c%c%c@%x,",table[0],table[1],table[2],table[3],xsdt_entries[i]);

				}

				// Correct the checksum of XSDT
				xsdt_mod->Length-=8*dropoffset;
				xsdt_mod->Checksum=0;
				xsdt_mod->Checksum=256-checksum8(xsdt_mod,xsdt_mod->Length);
			}
			else
			{
			drop_xsdt:

				DBG("About to drop XSDT\n");

				/*FIXME: Now we just hope that if MacOS doesn't find XSDT it reverts to RSDT. 
				 * A Better strategy would be to generate
				 */

				rsdp_mod->XsdtAddress=0xffffffffffffffffLL;
				printf("XSDT not found or XSDT incorrect\n");
			}
		}

		// Correct the checksum of RSDP      

		DBG("Original checksum %d\n", rsdp_mod->Checksum);

		rsdp_mod->Checksum=0;
		rsdp_mod->Checksum=256-checksum8(rsdp_mod,20);

		DBG("New checksum %d\n", rsdp_mod->Checksum);

		if (version)
		{
			DBG("Original extended checksum %d\n", rsdp_mod->ExtendedChecksum);

			rsdp_mod->ExtendedChecksum=0;
			rsdp_mod->ExtendedChecksum=256-checksum8(rsdp_mod,rsdp_mod->Length);

			DBG("New extended checksum %d\n", rsdp_mod->ExtendedChecksum);

		}
		
		verbose("Patched ACPI version %d DSDT\n", version+1);
		if (version)
		{
	/* XXX aserebln why uint32 cast if pointer is uint64 ? */
			acpi20_p = (uint32_t)rsdp_mod;
			addConfigurationTable(&gEfiAcpi20TableGuid, &acpi20_p, "ACPI_20");
		}
		else
		{
	/* XXX aserebln why uint32 cast if pointer is uint64 ? */
			acpi10_p = (uint32_t)rsdp_mod;
			addConfigurationTable(&gEfiAcpiTableGuid, &acpi10_p, "ACPI");
		}
	}
#if DEBUG_DSDT
	printf("(Press a key to continue...)\n");
	getc();
#endif
	return 1;
}
