#!/bin/bash

diskloader="/usr/standalone/i386/boot0"
partitionloader="/usr/standalone/i386/boot1h"
filesystemloader="/usr/standalone/i386/boot"
bootervolumename="EFI"
booterextensions="Extra/Extensions"

bootresources="${0%/*}"

diskmicrocodetype[1]="GRUB,47525542"
diskmicrocodetype[2]="LILO,4c494c4f"

start ()
{
# $1 volume

osxvolume="${@}"

if [ -z "${osxvolume}" ]; then
	echo
	echo "Cannot find the volume. Exiting."
	exit
fi

bootdev=$( df "${osxvolume}" | sed -n '2p' | awk '{print $1}' )

if [ "${bootdev}" = "${bootdev#*disk*s}" ]; then
	echo
	echo "ERROR Volume does not use slices."
	echo "Volume may be stored on a RAID array."
	echo
	exit
fi

partitiontable=$( dd 2>/dev/null if=${bootdev%s*} count=1 skip=1 | dd 2>/dev/null count=8 bs=1 | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )
if [ "${partitiontable:0:16}" == "4546492050415254" ]; then	
	partitiontable=$( dd 2>/dev/null if=${bootdev%s*} count=1 | dd 2>/dev/null count=64 bs=1 skip=446 | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )
	if [ "${partitiontable:8:2}" == "ee" ]; then
		if [ "${partitiontable:40:2}" == "00" ] && [ "${partitiontable:72:2}" == "00" ] && [ "${partitiontable:104:2}" == "00" ]; then
			partitiontable="GPT"
	 	else
			partitiontable="GPT/MBR"
		fi
	fi
else
	echo
	echo "ERROR Volume is not on a GPT partitioned disc."
	echo
	exit
fi

echo "${partitiontable} found."

echo "OS X Volume is ${osxvolume}"
echo "OX X Volume device is ${bootdev}"

bootvolume="/Volumes/$bootervolumename"
bootdev=${bootdev%s*}s1
bootrdev=${bootdev/disk/rdisk}
bootdisk=${bootdev%s*}
bootrdisk=${bootdisk/disk/rdisk}
bootslice=${bootdev#*disk*s}

echo "EFI Volume device is ${bootdev}"
echo "EFI Volume raw device is ${bootrdev}"
echo "EFI Volume slice is ${bootslice}"
echo "Disk device is ${bootdisk}"
echo "Disk raw device is ${bootrdisk}"
echo "Disk loader is ${diskloader}"
echo "Partition loader is ${partitionloader}"
echo "Filesystem loader is ${filesystemloader}"

}

checkdiskmicrocodetype ()
{
diskmicrocode=$( dd 2>/dev/null if=${bootdisk} count=1 | dd 2>/dev/null count=1 bs=437 | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )

diskmicrocodetypecounter=0
while [ ${diskmicrocodetypecounter} -lt ${#diskmicrocodetype[@]} ]; do
        diskmicrocodetypecounter=$(( ${diskmicrocodetypecounter} + 1 ))
        diskmicrocodetypeid=${diskmicrocodetype[${diskmicrocodetypecounter}]#*,}
        if [ ! "${diskmicrocode}" = "${diskmicrocode/${diskmicrocodetypeid}/}" ]; then
                echo "${diskmicrocodetype[${diskmicrocodetypecounter}]%,*} found."
        fi
done
}

checkdiskmicrocode ()
{
# 1 action ( check or set )

diskmicrocode=$( dd 2>/dev/null if=${bootdisk} count=1 | dd 2>/dev/null count=1 bs=437 | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )
diskmicrocodemd5=$( dd 2>/dev/null if=${bootdisk} count=1 | dd 2>/dev/null count=1 bs=437 | md5 )

if [ $( echo "${diskmicrocode}" | awk -F0 '{print NF-1}' ) = 874 ]; then
	if [ "${1}" = "set" ]; then
		echo "No disk microcode found. Updating."
		diskupdate=true
	else
		echo "No disk microcode found."
	fi
else
	if [ ${1} = set ]; then
		echo "Disk microcode found. Preserving."
	else
		echo "Disk microcode found."
	fi
	echo "Disk microcode MD5 is ${diskmicrocodemd5}"
fi
}

checkdisksignature ()
{
disksignature=$( dd 2>/dev/null if=${bootdisk} count=1 | dd 2>/dev/null count=4 bs=1 skip=440 | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )

if [ $( echo "${disksignature}" | awk -F0 '{print NF-1}' ) = 8 ]; then
	echo "No disk signature found."
else
	echo "Disk signature found."
	echo "Disk signature is 0x${disksignature}"
fi
}

checkpartitionbootcode ()
{
# 1 action ( check or set )

partitionbootcode=$( dd if=${bootrdev} count=1 2>/dev/null | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )
partitionbootcodeextended=$( dd if=${bootrdev} count=1 skip=1 2>/dev/null | perl -ne '@a=split"";for(@a){printf"%02x",ord}' )

if [ $( echo "${partitionbootcode}" | awk -F0 '{print NF-1}' ) = 1024 ]; then
	if [ "${1}" = "set" ]; then
		echo "No partition bootcode found. Updating."
	else
		echo "No partition bootcode found."
	fi
else
	if [ "${1}" = "set" ]; then
		echo "Partition bootcode found. Overwriting."
	else
		echo "Partition bootcode found."
	fi
	if [ $( echo "${partitionbootcodeextended}" | awk -F0 '{print NF-1}' ) = 1024 ]; then
		partitionbootcodemd5=$( dd 2>/dev/null if=${bootrdev} count=1 | md5 )
	else
		partitionbootcodemd5=$( dd 2>/dev/null if=${bootrdev} count=2 | md5 )
		echo "Partition bootcode is dual sector."
	fi
	echo "Partition bootcode MD5 is ${partitionbootcodemd5}"
fi
}

checkpartitionactive ()
{
partitionactive=$( fdisk -d ${bootrdisk} | grep -n "*" | awk -F: '{print $1}')

if [ -n "${partitionactive}" ]; then 
	echo "Partition flagged active is ${partitionactive}"
else
	echo "No partition flagged active."
fi

}

start ${3}

if [ "$( df | grep ${bootdev} )" ]; then
	umount -f ${bootdev}
fi 

if ! [ "$( fstyp ${bootdev} | grep hfs )" ]; then
	echo "${bootdev} isn't a HFS partition"
	echo "Executing command: newfs_hfs -v ${bootervolumename} ${bootdev}"
	newfs_hfs -v "${bootervolumename}" "${bootdev}"
else
	echo "${bootdev} is already a HFS partition (skipping)"
fi

diskupdate=false
checkdiskmicrocodetype
checkdiskmicrocode set
checkdisksignature
checkpartitionbootcode set
checkpartitionactive

if ${diskupdate}; then
	echo "Executing command: fdisk -u -f ${diskloader} -y ${bootdisk}"
	fdisk -u -f "${osxvolume}/${diskloader}" -y ${bootdisk}
fi

echo "Executing command: dd if=${partitionloader} of=${bootrdev}"
dd if="${osxvolume}/${partitionloader}" of=${bootrdev}

# If table is GPT make the first partition active (BadAxe compatibility).
[ "${partitiontable}" = "GPT" ] && bootslice=1
if [[ "${partitiontable}" = "GPT" ]]; then
	fdisk -e ${bootdisk} <<-MAKEACTIVE
	print
	flag ${bootslice}
	write
	y
	quit
	MAKEACTIVE
fi

checkdiskmicrocode check
checkdisksignature
checkpartitionbootcode check
checkpartitionactive

[ -d "${bootvolume}" ] || mkdir -p "${bootvolume}"
echo "Executing command: mount_hfs ${bootdev} ${bootvolume}"
mount_hfs "${bootdev}" "${bootvolume}"
       
echo "Executing command: cp ${osxvolume}${filesystemloader} ${bootvolume}/boot"
cp "${osxvolume}${filesystemloader}" "${bootvolume}/boot"
 
if ! [ -d "${bootvolume}/Extra/Extensions" ]; then
	echo "Executing command: mkdir -p ${bootvolume}/Extra/Extensions"
	mkdir -p "${bootvolume}/Extra/Extensions"
fi
# unpack any existing Extensions.mkext already on the booter volume
if [ -e "${bootvolume}/Extra/Extensions.mkext" ]; then
	echo "Executing command: mkextunpack -d ${bootvolume}/Extra/Extensions ${bootvolume}/Extra/Extensions.mkext"
	mkextunpack -d "${bootvolume}/Extra/Extensions" "${bootvolume}/Extra/Extensions.mkext"
	echo "Executing command: rm -R -f ${bootvolume}/Extra/Extensions.mkext"
	rm -R -f "${bootvolume}/Extra/Extensions.mkext"
fi	

# copy existing /Extra
if [ -d "${2}/Extra" ]; then
	[ -d "${bootvolume}/Extra/Extensions" ] || mkdir -p "${bootvolume}/Extra/Extensions"
	echo "Executing command: find ${2}/Extra -name '*.plist' -depth 1 -exec cp -f {} ${bootvolume}/Extra \;"
	find "${2}/Extra" -name '*.plist' -depth 1 -exec cp -f {} "${bootvolume}/Extra/" \;
	if [ -f "${2}/Extra/Extensions.mkext" ]; then
		echo "Executing command: mkextunpack -d ${2}/Extra/Extensions ${2}/Extra/Extensions.mkext"
		mkextunpack -d "${bootvolume}/Extra/Extensions" "${2}/Extra/Extensions.mkext"
	fi
	if [ -d "${2}/Extra/Extensions" ]; then
		echo "Executing command: find ${2}/Extra/Extensions -name '*.kext' -depth 1 -exec cp -R {} ${bootvolume}/Extra/Extensions \;"
		find "${2}/Extra/Extensions" -name '*.kext' -depth 1 -exec cp -R {} "${bootvolume}/Extra/Extensions" \;
	fi
fi

# setup link for extras
[ -h "${2}/.Chameleon" ] && unlink "${2}/.Chameleon"
echo "Executing command: ln -s /Volumes/${bootervolumename} ${2}/.Chameleon"
ln -s "/Volumes/${bootervolumename}" "${2}/.Chameleon"

exit
