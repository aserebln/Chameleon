#!/bin/bash

temp="/tmp/Chameleon"

# source com.apple.boot.plist
if ! [ -f "${2}/.Chameleon/Extra/com.apple.Boot.plist" ]; then
	if [ -f "${2}/Library/Preferences/SystemConfiguration/com.apple.Boot.plist" ]; then
		cp -f "${2}/Library/Preferences/SystemConfiguration/com.apple.Boot.plist" "${2}/.Chameleon/Extra"
	fi
fi

# fix kext permissions
find "${2}/.Chameleon/Extra/Extensions" -type f -exec chmod 644 {} \;
find "${2}/.Chameleon/Extra/Extensions" -type d -exec chmod 755 {} \;
chown -R 0:0 "${2}/.Chameleon/Extra/Extensions"

# build mkext for extras
[ -d "${temp}" ] && rm -R -f "${temp}"
mkdir -p "${temp}/Extensions"
ditto --noextattr --noqtn --arch i386 "${2}/.Chameleon/Extra/Extensions" "${temp}/Extensions"
find "${temp}" -type f -exec chmod 644 {} \;
find "${temp}" -type d -exec chmod 755 {} \;
chown -R 0:0 "${temp}"
kextcache -m "${temp}/Extensions.mkext" "${temp}/Extensions"
cp -f "${temp}/Extensions.mkext" "${2}/.Chameleon/Extra"
rm -R -f "${temp}"

# remove link for extras install
unlink "${2}/.Chameleon"

# umount efi partition
if [ -d /Volumes/EFI ]; then
	umount -f /Volumes/EFI
	rm -R -f /Volumes/EFI
fi

exit 0