/*
 *  ext2fs.c
 *  
 *
 *  Created by mackerintel on 1/26/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include "libsaio.h"
#include "sl.h"
#include "ext2fs.h"

#define EX2ProbeSize	2048

bool EX2Probe (const void *buf)
{
	return (OSReadLittleInt16(buf+0x438,0)==0xEF53);
}

void EX2GetDescription(CICell ih, char *str, long strMaxLen)
{
	char * buf=MALLOC (EX2ProbeSize);
	str[0]=0;
	if (!buf)
		return;
	Seek(ih, 0);
	Read(ih, (long)buf, EX2ProbeSize);
	if (!EX2Probe (buf))
	{
		free (buf);
		return;
	}
	if (OSReadLittleInt32 (buf+0x44c,0)<1)
	{
		free (buf);
		return;
	}
	str[strMaxLen]=0;
	strncpy (str, buf+0x478, min (strMaxLen, 16));
	free (buf);
}
