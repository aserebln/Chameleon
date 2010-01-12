/*
 * Copyright (c) 2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 2.0 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define tolower(c)     (((c)>='A' && c<='Z')?((c) | 0x20):(c))
#include "libsaio.h"
#include "sl.h"

#include "msdos_private.h"
#include "msdos.h"

#define LABEL_LENGTH		11
#define MAX_DOS_BLOCKSIZE	2048

#define	CLUST_FIRST		2/* reserved cluster range */
#define	CLUST_RSRVD32	0x0ffffff8	/* reserved cluster range */
#define	CLUST_RSRVD16	0xfff8	/* reserved cluster range */
#define	CLUST_RSRVD12	0xff8	/* reserved cluster range */


#define false 0
#define true 1

static int msdosressector=0;
static int msdosnfats = 0;
static int msdosfatsecs = 0;
static int msdosbps = 0;
static int msdosclustersize = 0;
static int msdosrootDirSectors = 0;
static CICell msdoscurrent = 0;
static int msdosrootcluster = 0;
static int msdosfatbits = 0;

#if UNUSED
/*
 * Check a volume label.
 */
static int
oklabel(const char *src)
{
    int c, i;

    for (i = 0, c = 0; i <= 11; i++) {
        c = (u_char)*src++;
        if (c < ' ' + !i || strchr("\"*+,./:;<=>?[\\]|", c))
            break;
    }
    return i && !c;
}
#endif /* UNUSED */

void MSDOSFree(CICell ih)
{
	if(msdoscurrent == ih)
        msdoscurrent = 0;
    free(ih);
}

int MSDOSProbe(const void * buffer)
{
    union bootsector    *bsp;
    struct bpb33        *b33;
    struct bpb50        *b50;
    struct bpb710       *b710;
    u_int16_t	        bps;
    u_int8_t		spc;
	
    bsp = (union bootsector *)buffer;
    b33 = (struct bpb33 *)bsp->bs33.bsBPB;
    b50 = (struct bpb50 *)bsp->bs50.bsBPB;
    b710 = (struct bpb710 *)bsp->bs710.bsBPB;
    
    /* We only work with 512, 1024, and 2048 byte sectors */
    bps = OSSwapLittleToHostInt16(b33->bpbBytesPerSec);
    if ((bps < 0x200) || (bps & (bps - 1)) || (bps > 0x800)) 
        return 0;
    
	/* Check to make sure valid sectors per cluster */
    spc = b33->bpbSecPerClust;
    if ((spc == 0 ) || (spc & (spc - 1))) 
        return 0;	
	
	if (OSSwapLittleToHostInt16(b50->bpbRootDirEnts) == 0) { /* It's FAT32 */
		if (!memcmp(((struct extboot *)bsp->bs710.bsExt)->exFileSysType, "FAT32   ", 8))
			return 32;
	}
	else if (((struct extboot *)bsp->bs50.bsExt)->exBootSignature == EXBOOTSIG) {
		if (!memcmp((char *)((struct extboot *)bsp->bs50.bsExt)->exFileSysType, "FAT16   ", 8))
			return 16;
		if (!memcmp((char *)((struct extboot *)bsp->bs50.bsExt)->exFileSysType, "FAT12   ", 8))
			return 12;
	}	
		
	return 0;
}


long
MSDOSInitPartition (CICell ih)
{
    union bootsector    *bsp;
    struct bpb33        *b33;
    struct bpb50        *b50;
    struct bpb710       *b710;
    u_int8_t		spc;
    char                *buf;
	
	if (msdoscurrent == ih)
	{
		CacheInit(ih, msdosclustersize);
		return 0;
	}
	
	buf=MALLOC (512);
	/*
     * Read the boot sector of the filesystem, and then check the
     * boot signature.  If not a dos boot sector then error out.
     *
     * NOTE: 2048 is a maximum sector size in current...
     */
    Seek(ih, 0);
    Read(ih, (long)buf, 512);
	
    bsp = (union bootsector *)buf;
    b33 = (struct bpb33 *)bsp->bs33.bsBPB;
    b50 = (struct bpb50 *)bsp->bs50.bsBPB;
    b710 = (struct bpb710 *)bsp->bs710.bsBPB;
    
	
    /* We only work with 512, 1024, and 2048 byte sectors */
    msdosbps = OSSwapLittleToHostInt16(b33->bpbBytesPerSec);
    if ((msdosbps < 0x200) || (msdosbps & (msdosbps - 1)) || (msdosbps > 0x800)) 
	{
		free (buf);
		return -1;
	}
	
    /* Check to make sure valid sectors per cluster */
    spc = b33->bpbSecPerClust;
    if ((spc == 0 ) || (spc & (spc - 1))) 
		return -1;
	
	if (OSSwapLittleToHostInt16(b50->bpbRootDirEnts) == 0) { /* It's FAT32 */
		if (memcmp(((struct extboot *)bsp->bs710.bsExt)->exFileSysType, "FAT32   ", 8))
		{
			free (buf);
			return -1;
		}
		msdosressector = OSSwapLittleToHostInt16(b710->bpbResSectors);
		msdosnfats = b710->bpbFATs;
		msdosfatsecs = OSSwapLittleToHostInt16(b710->bpbBigFATsecs);
		msdosrootcluster = OSSwapLittleToHostInt32(b710->bpbRootClust);
		msdosrootDirSectors = 0;
		msdosfatbits = 32;
	}
	else if (((struct extboot *)bsp->bs50.bsExt)->exBootSignature == EXBOOTSIG) {
		if (!memcmp((char *)((struct extboot *)bsp->bs50.bsExt)->exFileSysType, "FAT16   ", 8))
			msdosfatbits = 16;
		else if (!memcmp((char *)((struct extboot *)bsp->bs50.bsExt)->exFileSysType, "FAT12   ", 8))
			msdosfatbits = 12;
		else
		{
			free (buf);
			return -1;
		}		
		
		msdosressector = OSSwapLittleToHostInt16(b33->bpbResSectors);
		msdosnfats = b33->bpbFATs;
		msdosfatsecs = OSSwapLittleToHostInt16(b33->bpbFATsecs);
		msdosrootcluster = 0;		
		msdosrootDirSectors = ((OSSwapLittleToHostInt16(b50->bpbRootDirEnts) * sizeof(struct direntry)) +
							   (msdosbps-1)) / msdosbps;
	} else {
		free (buf);
		return -1;
	}
	
	msdosclustersize = msdosbps * spc;

	msdoscurrent = ih;
	CacheInit(ih, msdosclustersize);
	free (buf);
	return 0;
}

static int
msdosreadcluster (CICell ih, uint8_t *buf, int size, off_t *cluster)
{
    off_t readOffset;
	char tmpbuf[8];
	off_t clusn;
	
	switch (msdosfatbits) {
		case 32:
			if (*cluster < CLUST_FIRST ||*cluster >= CLUST_RSRVD32)
				return 0;
			clusn = *cluster - CLUST_FIRST;
			break;
		case 16:
			if (*cluster < CLUST_FIRST ||*cluster >= CLUST_RSRVD16)
				return 0;
			clusn = *cluster - CLUST_FIRST;
			break;			
		case 12:
			if (*cluster < CLUST_FIRST ||*cluster >= CLUST_RSRVD12)
				return 0;
			clusn = *cluster - CLUST_FIRST;
			break;
		default:
			return 0;
	}
	
	/* Find sector where clusters start */
	readOffset = (msdosressector +
				  (msdosnfats * msdosfatsecs)+msdosrootDirSectors)*msdosbps;
	/* Find sector where "cluster" starts */
	readOffset += clusn * msdosclustersize;
	
	/* Read in "cluster" */
	if (buf)
	{
		Seek(ih, readOffset);
		Read(ih, (long)buf, size);
	}
	
	/* Find first sector of FAT */
	readOffset = msdosressector*msdosbps;
	/* Find sector containing "cluster" entry in FAT */
	readOffset += ((uint64_t)*cluster * (uint64_t)msdosfatbits)/8;
	
	/* Read one sector of the FAT */
	Seek(ih, readOffset);
	Read(ih, (long)tmpbuf, 4);
	
	switch (msdosfatbits) {
		case 32:
			*cluster = OSReadLittleInt32(tmpbuf, 0);
			*cluster &= 0x0FFFFFFF;	// ignore reserved upper bits
			return 1;
		case 16:
			*cluster = OSReadLittleInt16(tmpbuf, 0);
			return 1;			
		case 12:
			*cluster = OSReadLittleInt16(tmpbuf, 0)>>(((uint64_t)*cluster * (uint64_t)msdosfatbits)%8);
			*cluster &= 0xfff;
			return 1;
		default:
			return 0;
	}
}

struct msdosdirstate
{
	struct direntry *buf;
	uint8_t vfatchecksum;
	int root16;
	off_t cluster;
	int nument;
	int vfatnumber;
};

static struct direntry *
getnextdirent (CICell ih, uint16_t *longname, struct msdosdirstate *st)
{
	struct direntry *dirp;
	while (1)
	{
		if (st->root16)
		{
			if (st->cluster >= msdosrootDirSectors && st->nument == 0)
				return 0;
			if (st->nument == 0)
			{
				Seek(ih, (msdosressector +
						  (msdosnfats * msdosfatsecs)+st->cluster)*msdosbps);
				Read(ih, (long)st->buf, msdosbps);
				st->cluster++;
			}
		} else if (st->nument == 0 && !msdosreadcluster (ih, (uint8_t *)st->buf, msdosclustersize, &(st->cluster)))
			return 0;
		
		dirp=st->buf+st->nument;
		
		if (dirp->deName[0] == SLOT_EMPTY)
			return 0;
		else if (dirp->deName[0] == SLOT_DELETED)
			st->vfatnumber = 0;
		else if (dirp->deAttributes == ATTR_WIN95)
		{
			struct winentry *wdirp = (struct winentry *)dirp;
			int num;
			if (wdirp->weCnt & 0x80)
				continue;
			num=(wdirp->weCnt&0x3f);
			if (WIN_CHARS * num > WIN_MAXLEN)
				continue;
			if (st->vfatchecksum!=wdirp->weChksum)
			{
				st->vfatnumber = 0;
				st->vfatchecksum = wdirp->weChksum;
			}
			if (st->vfatnumber < num)
				st->vfatnumber = num;
			bcopy (&(wdirp->wePart1),longname+WIN_CHARS*(num-1),sizeof (wdirp->wePart1));
			bcopy (&(wdirp->wePart2),longname+WIN_CHARS*(num-1)+5,sizeof (wdirp->wePart2));
			bcopy (&(wdirp->wePart3),longname+WIN_CHARS*(num-1)+11,sizeof (wdirp->wePart3));
		} else {
			uint8_t labelchecksum;
			int i;
			longname[st->vfatnumber*WIN_CHARS]=0;
			
			labelchecksum=0;
			for(i=0;i<LABEL_LENGTH;i++)
				labelchecksum=(labelchecksum>>1)+(labelchecksum<<7)+dirp->deName[i];
			if (!(labelchecksum==st->vfatchecksum && st->vfatnumber))
				longname[0]=0;
			st->vfatnumber = 0;
			st->vfatchecksum = 0;
			st->vfatnumber = 0;
			st->nument++;
			if ((!st->root16 &&st->nument * sizeof (struct direntry)>=msdosclustersize)
				|| (st->root16 &&st->nument * sizeof (struct direntry)>=msdosbps))
				st->nument = 0;
			return dirp;
		}
		st->nument++;
		if ((!st->root16 &&st->nument * sizeof (struct direntry)>=msdosclustersize)
			|| (st->root16 &&st->nument * sizeof (struct direntry)>=msdosbps))
			st->nument = 0;		
	}
}

static void
initRoot (struct msdosdirstate *st)
{
	if (msdosrootDirSectors) {			/* FAT12 or FAT16 */
		st->root16 = 1;
		st->vfatchecksum = 0;
		st->nument = 0;
		st->cluster = 0;
		st->vfatnumber = 0;
	} else {	/* FAT32 */
		st->root16 = 0;
		st->vfatchecksum = 0;
		st->nument = 0;
		st->cluster = msdosrootcluster;
		st->vfatnumber = 0;
	}
}

/* First comes lowercase, then uppercase*/
static uint16_t cp850[128][2]=
{
{0x00E7,0x00C7},
{0x00FC,0x00DC},
{0x00E9,0x00C9},
{0x00E2,0x00C2},
{0x00E4,0x00C4},
{0x00E0,0x00C0},
{0x00E5,0x00C5},
{0x00E7,0x00C7},
{0x00EA,0x00CA},
{0x00EB,0x00CB},
{0x00E8,0x00C8},
{0x00EF,0x00CF},
{0x00EE,0x00CE},
{0x00EC,0x00CC},
{0x00E4,0x00C4},
{0x00E5,0x00C5},
{0x00E9,0x00C9},
{0x00E6,0x00C6},
{0x00E6,0x00C6},
{0x00F4,0x00D4},
{0x00F6,0x00D6},
{0x00F2,0x00D2},
{0x00FB,0x00DB},
{0x00F9,0x00D9},
{0x00FF,0x0178},
{0x00F6,0x00D6},
{0x00FC,0x00DC},
{0x00F8,0x00D8},
{0x00A3,0x00A3},
{0x00F8,0x00D8},
{0x00D7,0x00D7},
{0x0192,0x0191},
{0x00E1,0x00C1},
{0x00ED,0x00CD},
{0x00F3,0x00D3},
{0x00FA,0x00DA},
{0x00F1,0x00D1},
{0x00F1,0x00D1},
{0x00AA,0x00AA},
{0x00BA,0x00BA},
{0x00BF,0x00BF},
{0x00AE,0x00AE},
{0x00AC,0x00AC},
{0x00BD,0x00BD},
{0x00BC,0x00BC},
{0x00A1,0x00A1},
{0x00AB,0x00AB},
{0x00BB,0x00BB},
{0x2591,0x2591},
{0x2592,0x2592},
{0x2593,0x2593},
{0x2502,0x2502},
{0x2524,0x2524},
{0x00E1,0x00C1},
{0x00E2,0x00C2},
{0x00E0,0x00C0},
{0x00A9,0x00A9},
{0x2563,0x2563},
{0x2551,0x2551},
{0x2557,0x2557},
{0x255D,0x255D},
{0x00A2,0x00A2},
{0x00A5,0x00A5},
{0x2510,0x2510},
{0x2514,0x2514},
{0x2534,0x2534},
{0x252C,0x252C},
{0x251C,0x251C},
{0x2500,0x2500},
{0x253C,0x253C},
{0x00E3,0x00C3},
{0x00E3,0x00C3},
{0x255A,0x255A},
{0x2554,0x2554},
{0x2569,0x2569},
{0x2566,0x2566},
{0x2560,0x2560},
{0x2550,0x2550},
{0x256C,0x256C},
{0x00A4,0x00A4},
{0x00F0,0x00D0},
{0x00F0,0x00D0},
{0x00EA,0x00CA},
{0x00EB,0x00CB},
{0x00E8,0x00C8},
{0x0131,0x0049},
{0x00ED,0x00CD},
{0x00EE,0x00CE},
{0x00EF,0x00CF},
{0x2518,0x2518},
{0x250C,0x250C},
{0x2588,0x2588},
{0x2584,0x2584},
{0x00A6,0x00A6},
{0x00EC,0x00CC},
{0x2580,0x2580},
{0x00F3,0x00D3},
{0x00DF,0x00DF},
{0x00F4,0x00D4},
{0x00F2,0x00D2},
{0x00F5,0x00D5},
{0x00F5,0x00D5},
{0x00B5,0x00B5},
{0x00FE,0x00DE},
{0x00FE,0x00DE},
{0x00FA,0x00DA},
{0x00FB,0x00DB},
{0x00F9,0x00D9},
{0x00FD,0x00DD},
{0x00FD,0x00DD},
{0x00AF,0x00AF},
{0x00B4,0x00B4},
{0x00AD,0x00AD},
{0x00B1,0x00B1},
{0x2017,0x2017},
{0x00BE,0x00BE},
{0x00B6,0x00B6},
{0x00A7,0x00A7},
{0x00F7,0x00F7},
{0x00B8,0x00B8},
{0x00B0,0x00B0},
{0x00A8,0x00A8},
{0x00B7,0x00B7},
{0x00B9,0x00B9},
{0x00B3,0x00B3},
{0x00B2,0x00B2},
{0x25A0,0x25A0},
{0x00A0,0x00A0}
};

static int
checkname (uint16_t *ucsname, int ucslen, struct direntry *dirp, uint16_t *vfatname)
{
	uint16_t tmp[15];
	if (vfatname[0])
	{
		int i;
		for (i=0;vfatname[i];i++);
		return !FastUnicodeCompare (ucsname, ucslen, vfatname, i, OSLittleEndian);
	}
	else
	{
		int i, j, k;
		for (i=7;i>=0;i--)
			if (dirp->deName[i]!=' ')
				break;
		j=i+1;
		tmp[i+1]=0;
		for(;i>=0;i--)
			tmp[i]=SWAP_LE16((dirp->deName[i]>=128)?cp850[dirp->deName[i]-128][0]:tolower(dirp->deName[i]));
		for (i=2;i>=0;i--)
			if (dirp->deName[8+i]!=' ')
				break;
		if (i>=0)
		{
			tmp[j++]='.';
			tmp[j+i+1]=0;
			k=j+i+1;
			for(;i>=0;i--)
				tmp[j+i]=SWAP_LE16((dirp->deName[i+8]>=128)?cp850[dirp->deName[i+8]-128][0]:tolower(dirp->deName[i+8]));
			j=k;
		}
		return !FastUnicodeCompare (ucsname, ucslen, tmp, j, OSLittleEndian);
	}
	
}

static struct direntry *
getdirpfrompath (CICell ih, char *dirspec, uint8_t *buf)
{
	struct msdosdirstate st;
	struct direntry *dirp;
	uint8_t * ptr;
	uint8_t *slash;
	char c;
	uint16_t		vfatname[WIN_MAXLEN+2*WIN_CHARS];
	uint16_t ucsname[WIN_MAXLEN+1];
	uint16_t ucslen;
	int ucslenhost;
	initRoot (&st);
	st.buf = (struct direntry *)buf;
	ptr=(uint8_t*)dirspec;
	for (slash=ptr;*slash && *slash!='/';slash++);
	c=*slash;
	*slash=0;
	utf_decodestr (ptr, ucsname, &ucslen, WIN_MAXLEN, OSLittleEndian);
	ucslenhost = OSReadLittleInt16 (&ucslen,0);
	ucsname[ucslenhost]=0;
	*slash=c;	
	while ((dirp = getnextdirent (ih, vfatname, &st)))
	{
		if (checkname (ucsname, ucslenhost, dirp, vfatname))
		{
			for (;*ptr && *ptr!='/';ptr++);
			if (!*ptr)
				return dirp;
			ptr++;
			if (!*ptr)
				return dirp;
			for (slash=ptr;*slash && *slash!='/';slash++);
			c=*slash;
			*slash=0;
			utf_decodestr (ptr, ucsname, &ucslen, WIN_MAXLEN, OSLittleEndian);
			ucslenhost = OSReadLittleInt16 (&ucslen,0);
			ucsname[ucslenhost]=0;
			*slash=c;
			if (!(dirp->deAttributes & ATTR_DIRECTORY))
				return 0;
			st.root16 = 0;
			st.vfatchecksum = 0;
			st.nument = 0;
			st.cluster = OSReadLittleInt16 ((dirp->deStartCluster),0);
			if (msdosfatbits == 32)
				st.cluster |= ((uint32_t)OSReadLittleInt16 ((dirp->deHighClust),0)) <<16;
			st.vfatnumber = 0;
		}
	}
	return 0;
}

long MSDOSGetDirEntry(CICell ih, char * dirPath, long * dirIndex,
					  char ** name, long * flags, long * time,
					  FinderInfo * finderInfo, long * infoValid)
{
	struct msdosdirstate *st;
	struct direntry *dirp;
	uint16_t		vfatname[WIN_MAXLEN+2*WIN_CHARS];
	if (MSDOSInitPartition (ih)<0)
		return -1;
	if (dirPath[0] == '/')
		dirPath++;
	st =  (struct msdosdirstate *)*dirIndex;
	if (!st)
	{
		st=MALLOC (sizeof (*st));
		if (dirPath[0])
		{
			uint8_t *buf=MALLOC(msdosclustersize);
			dirp = getdirpfrompath (ih, dirPath, buf);
			if (!dirp || !(dirp->deAttributes & ATTR_DIRECTORY))
			{
				free (buf);
				free (st);
				return -1;
			}
			st->buf = (struct direntry *)buf;
			st->root16 = 0;
			st->vfatchecksum = 0;
			st->nument = 0;
			st->cluster = OSReadLittleInt16 ((dirp->deStartCluster),0);
			st->vfatnumber = 0;
			if (msdosfatbits == 32)
				st->cluster |= ((uint32_t)OSReadLittleInt16 ((dirp->deHighClust),0)) <<16;
		}
		else
			initRoot (st);
		*dirIndex = (long)st;
	}
	while((dirp = getnextdirent (ih, vfatname, st))&& (dirp->deAttributes & ATTR_VOLUME));
	if (!dirp)
	{
		free (st->buf);
		free (st);
		return -1;
	}
	if (vfatname[0])
	{
		int i;
		for (i=0;vfatname[i];i++);
		*name = MALLOC (256);
		utf_encodestr(vfatname, i, (u_int8_t *)*name, 255, OSLittleEndian );	
	}
	else
	{
		int i, j, k;
		uint16_t tmp[13];
		*name = MALLOC (26);
		for (i=7;i>=0;i--)
			if (dirp->deName[i]!=' ')
				break;
		j=i+1;
		tmp[i+1]=0;
		for(;i>=0;i--)
			tmp[i]=(dirp->deName[i]>=128)?cp850[dirp->deName[i]-128][0]:tolower(dirp->deName[i]);
		for (i=2;i>=0;i--)
			if (dirp->deName[8+i]!=' ')
				break;
		
		if (i>=0)
		{
			tmp[j++]='.';
			tmp[j+i+1]=0;
			k=j+i+1;
			for(;i>=0;i--)
				tmp[j+i]=(dirp->deName[i]>=128)?cp850[dirp->deName[i+8]-128][0]:tolower(dirp->deName[i+8]);
			j=k;
		}
		
		utf_encodestr(tmp, j, (uint8_t*)*name, 25, OSHostByteOrder() );
	}
	if (dirp->deAttributes & ATTR_DIRECTORY)
		*flags = kFileTypeDirectory;
	else
		*flags = kFileTypeFlat;
	
	// Calculate a fake timestamp using modification date and time values.
	*time = (dirp->deMDate & 0x7FFF) << 16 + dirp->deMTime;
	
	if (infoValid)
		*infoValid = 1;

	return 0;
}

long 
MSDOSReadFile(CICell ih, char * filePath, void *base, uint64_t offset, uint64_t length)
{
	uint8_t *buf;
	off_t cluster;
	uint64_t size;
	uint64_t nskip;
	int toread, wastoread;
	char *ptr = (char *)base;
	struct direntry *dirp;
	int i;
	if (MSDOSInitPartition (ih)<0)
		return -1;
	if (filePath[0] == '/')
		filePath++;
	buf = MALLOC(msdosclustersize);
	dirp = getdirpfrompath (ih, filePath, buf);
	
	if (!dirp || (dirp->deAttributes & ATTR_DIRECTORY))
	{
		free (buf);
		return -1;
	}
	cluster = OSReadLittleInt16 ((dirp->deStartCluster),0);
	if (msdosfatbits == 32)
		cluster |= ((uint32_t)OSReadLittleInt16 ((dirp->deHighClust),0)) <<16;
	size = (uint32_t)OSReadLittleInt32 ((dirp->deFileSize),0);
	if (size<=offset)
		return -1;
	nskip=offset/msdosclustersize;
	for (i=0;i<nskip;i++)
		msdosreadcluster (ih, 0, 0, &cluster);
	msdosreadcluster (ih, buf, msdosclustersize, &cluster);
	toread=length;
	if (length==0 || length>size-offset)
		toread=size-offset;
	wastoread=toread;
	bcopy (buf+(offset%msdosclustersize),ptr,min(msdosclustersize-(offset%msdosclustersize), toread));
	ptr+=msdosclustersize-(offset%msdosclustersize);
	toread-=msdosclustersize-(offset%msdosclustersize);
	while (toread>0 && msdosreadcluster (ih, (uint8_t *)ptr, min(msdosclustersize,toread), &cluster))
	{
		ptr+=msdosclustersize;
		toread-=msdosclustersize;
	}
	verbose("Loaded FAT%d file: [%s] %d bytes from %x.\n",
            msdosfatbits, filePath, (uint32_t)( toread<0 ) ? wastoread : wastoread-toread, ih);
	free (buf);
	if (toread<0)
		return wastoread;
	else
		return wastoread-toread;
}

long 
MSDOSGetFileBlock(CICell ih, char *filePath, unsigned long long *firstBlock)
{
	uint8_t *buf;
	off_t cluster;
	struct direntry *dirp;
	if (MSDOSInitPartition (ih)<0)
		return -1;
	if (filePath[0] == '/')
		filePath++;
	buf = MALLOC(msdosclustersize);
	dirp = getdirpfrompath (ih, filePath, buf);
	if (!dirp || (dirp->deAttributes & ATTR_DIRECTORY))
	{
		free (buf);
		return -1;
	}
	cluster = OSReadLittleInt16 ((dirp->deStartCluster),0);
	if (msdosfatbits == 32)
		cluster |= ((uint32_t)OSReadLittleInt16 ((dirp->deHighClust),0)) <<16;
	
	off_t clusn;
	
	switch (msdosfatbits) {
		case 32:
			if (cluster < CLUST_FIRST ||cluster >= CLUST_RSRVD32)
				return -1;
			clusn = cluster - CLUST_FIRST;
			break;
		case 16:
			if (cluster < CLUST_FIRST ||cluster >= CLUST_RSRVD16)
				return 0;
			clusn = cluster - CLUST_FIRST;
			break;			
		case 12:
			
			if (cluster < CLUST_FIRST ||cluster >= CLUST_RSRVD12)
				return 0;
			clusn = cluster - CLUST_FIRST;
			break;
		default:
			return 0;
	}
	
	*firstBlock = ((msdosressector +
				  (msdosnfats * msdosfatsecs))*msdosbps + clusn * msdosclustersize)/512;
	free (buf);
	return 0;
}


long MSDOSLoadFile(CICell ih, char * filePath)
{
    return MSDOSReadFile(ih, filePath, (void *)gFSLoadAddress, 0, 0);
}

/* Fix up volume label. */
static void
fixLabel(uint8_t *label, char *str, long strMaxLen)
{
    int			i, len;
	uint16_t	labelucs[13];
    //unsigned char	labelUTF8[LABEL_LENGTH*3];
	
    /* Convert leading 0x05 to 0xE5 for multibyte languages like Japanese */
    if (label[0] == 0x05)
        label[0] = 0xE5;
	
    /* Remove any trailing spaces */
    for (i=LABEL_LENGTH-1; i>=0; --i) {
        if (label[i] == ' ')
            label[i] = 0;
        else
            break;
    }
	labelucs[i++]=0;
	len=i;
	for (;i>=0;--i)
		labelucs[i]=label[i]>=128?cp850[label[i]-128][1]:(label[i]);
	
	utf_encodestr(labelucs, len, (uint8_t *)str, strMaxLen, OSHostByteOrder() );
}


void
MSDOSGetDescription(CICell ih, char *str, long strMaxLen)
{
    struct direntry     *dirp;
    uint8_t	        label[LABEL_LENGTH+1];
	uint16_t		vfatlabel[WIN_MAXLEN+2*WIN_CHARS];
	struct msdosdirstate st;
	int labelfound = 0;

	if (MSDOSInitPartition (ih)<0)
	{
		str[0]=0;
		return;
	}
    
	label[0] = '\0';
	
	initRoot (&st);
	st.buf = MALLOC(msdosclustersize);
	while ((dirp = getnextdirent (ih, vfatlabel, &st)))
		if (dirp->deAttributes & ATTR_VOLUME) {
			strncpy((char *)label, (char *)dirp->deName, LABEL_LENGTH);
			labelfound = 1;
			break;
		}
		
	free(st.buf);		

	if (vfatlabel[0] && labelfound)
	{
		int i;
		for (i=0;vfatlabel[i];i++);
		utf_encodestr(vfatlabel, i, (u_int8_t *)str, strMaxLen, OSLittleEndian );		
	}	
	else if (labelfound)
		fixLabel(label, str, strMaxLen);
	
    /* else look in the boot blocks */
    if (!labelfound || str[0] == '\0') {
		char *buf = MALLOC (512);
		union bootsector *bsp = (union bootsector *)buf;
		Seek(ih, 0);
		Read(ih, (long)buf, 512);
        if (msdosfatbits == 32) { /* It's FAT32 */
            strncpy((char *)label, (char *)((struct extboot *)bsp->bs710.bsExt)->exVolumeLabel, LABEL_LENGTH);
        }
        else if (msdosfatbits == 16) {
            strncpy((char *)label, (char *)((struct extboot *)bsp->bs50.bsExt)->exVolumeLabel, LABEL_LENGTH);
        }
		free (buf);
		fixLabel(label, str, strMaxLen);
    }

    return;
}

long 
MSDOSGetUUID(CICell ih, char *uuidStr)
{
	char *buf = MALLOC (512);
	union bootsector *bsp = (union bootsector *)buf;
	
	if (MSDOSInitPartition (ih)<0)
	{
		return -1;
	}
	bzero (uuidStr, 16);
	Seek(ih, 0);
	Read(ih, (long)buf, 512);
	if (msdosfatbits == 32) { /* It's FAT32 */
		memcpy(uuidStr+12, (char *)((struct extboot *)bsp->bs710.bsExt)->exVolumeID, 4);
	}
	else if (msdosfatbits == 16) {
		memcpy(uuidStr+12, (char *)((struct extboot *)bsp->bs50.bsExt)->exVolumeID, 4);
	}
	free (buf);
    return 0;
	
}
