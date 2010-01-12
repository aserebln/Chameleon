#!/bin/bash

# $1 Path to store built package

packagesidentity="org.chameleon"

packagename="Chameleon"

pkgroot="${0%/*}"

#version=$( grep I386BOOT_CHAMELEONVERSION sym/i386/vers.h | awk '{ print $3 }' | tr -d '\"' )
version=$( cat version )
stage=${version##*-}
revision=$( grep I386BOOT_CHAMELEONREVISION sym/i386/vers.h | awk '{ print $3 }' | tr -d '\"' )
builddate=$( grep I386BOOT_BUILDDATE sym/i386/vers.h | awk '{ print $3,$4 }' | tr -d '\"' )
timestamp=$( date -j -f "%Y-%m-%d %H:%M:%S" "${builddate}" "+%s" )

distributioncount=0
xmlindent=0

indent[0]="\t"
indent[1]="\t\t"
indent[2]="\t\t\t"
indent[3]="\t\t\t\t"

main ()
{

# clean up the destination path

	rm -R -f "${1}"
	
	echo "Building $packagename Install Package v${version%%-*} $stage r$revision $builddate"
	
	outline[$((outlinecount++))]="${indent[$xmlindent]}<choices-outline>"

# build core package

	mkdir -p ${1}/Core/Root/usr/sbin
	mkdir -p ${1}/Core/Root/usr/local/bin
	mkdir -p ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/boot ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/boot0 ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/boot1f32 ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/boot1h ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/boot1he ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/boot1hp ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/cdboot ${1}/Core/Root/usr/standalone/i386
	cp -f ${1%/*}/i386/chain0 ${1}/Core/Root/usr/standalone/i386
	fixperms "${1}/Core/Root/"
	cp -f ${pkgroot}/fdisk ${1}/Core/Root/usr/sbin
	local coresize=$( du -hkc "${1}/Core/Root" | tail -n1 | awk {'print $1'} )
	buildpackage "${1}/Core" "/" "0" "start_visible=\"false\" start_selected=\"true\""

# build standard package 

	mkdir -p ${1}/Standard/Root
	mkdir -p ${1}/Standard/Scripts/Tools
	cp -f ${pkgroot}/Scripts/Standard/* ${1}/Standard/Scripts
	ditto --arch i386 `which SetFile` ${1}/Standard/Scripts/Tools/SetFile
	buildpackage "${1}/Standard" "/" "${coresize}" "start_enabled=\"true\" start_selected=\"upgrade_allowed()\" selected=\"exclusive(choices['EnhancedHFS']) &amp;&amp; exclusive(choices['EnhancedFAT'])\""

# build efi fat32 package 

	mkdir -p ${1}/EnhancedFAT/Root
	mkdir -p ${1}/EnhancedFAT/Scripts/Tools
	cp -f ${pkgroot}/Scripts/FAT/* ${1}/EnhancedFAT/Scripts
	ditto --arch i386 `which SetFile` ${1}/EnhancedFAT/Scripts/Tools/SetFile
	buildpackage "${1}/EnhancedFAT" "/" "${coresize}" "start_visible=\"systemHasGPT()\" start_selected=\"false\" selected=\"exclusive(choices['Standard']) &amp;&amp; exclusive(choices['EnhancedHFS'])\""

# build efi hfs package 

	mkdir -p ${1}/EnhancedHFS/Root
	mkdir -p ${1}/EnhancedHFS/Scripts/Tools
	cp -f ${pkgroot}/Scripts/HFS/* ${1}/EnhancedHFS/Scripts
	ditto --arch i386 `which SetFile` ${1}/EnhancedHFS/Scripts/Tools/SetFile
	buildpackage "${1}/EnhancedHFS" "/" "${coresize}" "start_visible=\"systemHasGPT()\" start_selected=\"false\" selected=\"exclusive(choices['Standard']) &amp;&amp; exclusive(choices['EnhancedFAT'])\""

# build options packages

	outline[$((outlinecount++))]="${indent[$xmlindent]}\t<line choice=\"Options\">"
	choices[$((choicescount++))]="<choice\n\tid=\"Options\"\n\ttitle=\"Options_title\"\n\tdescription=\"Options_description\"\n>\n</choice>\n"
	((xmlindent++))
	packagesidentity="org.chameleon"
	options=($( find "${pkgroot}/Scripts/Options" -type d -depth 1 -not -name '.svn' ))
	for (( i = 0 ; i < ${#options[@]} ; i++ )) 
	do
		mkdir -p "${1}/${options[$i]##*/}/Root"
		mkdir -p "${1}/${options[$i]##*/}/Scripts"
		
		ditto --noextattr --noqtn "${options[$i]}/postinstall" "${1}/${options[$i]##*/}/Scripts/postinstall"
		
		buildpackage "${1}/${options[$i]##*/}" "/" "" "start_selected=\"false\""
	done
	((xmlindent--))
	outline[$((outlinecount++))]="${indent[$xmlindent]}\t</line>"

# build theme packages

	outline[$((outlinecount++))]="${indent[$xmlindent]}\t<line choice=\"Themes\">"
	choices[$((choicescount++))]="<choice\n\tid=\"Themes\"\n\ttitle=\"Themes_title\"\n\tdescription=\"Themes_description\"\n>\n</choice>\n"
	((xmlindent++))
	packagesidentity="org.chameleon.theme"
	artwork="${1%/*}"
	themes=($( find "${artwork%/*}/artwork/themes" -type d -depth 1 -not -name '.svn' ))
	for (( i = 0 ; i < ${#themes[@]} ; i++ )) 
	do
		theme=$( echo ${themes[$i]##*/} | awk 'BEGIN{OFS=FS=""}{$1=toupper($1);print}' )
		mkdir -p "${1}/${theme}/Root/"
		ditto --noextattr --noqtn "${themes[$i]}" "${1}/${themes[$i]##*/}/Root/${theme}"
		find "${1}/${themes[$i]##*/}" -name '.DS_Store' -or -name '.svn' -exec rm -R {} \+
		find "${1}/${themes[$i]##*/}" -type f -exec chmod 644 {} \+

		buildpackage "${1}/${theme}" "/.Chameleon/Extra/Themes" ""
		rm -R -f "${1}/${i##*/}"
	done

	((xmlindent--))
	outline[$((outlinecount++))]="${indent[$xmlindent]}\t</line>"

	outline[$((outlinecount++))]="${indent[$xmlindent]}\t<line choice=\"Extras\">"
	choices[$((choicescount++))]="<choice\n\tid=\"Extras\"\n\ttitle=\"Extras_title\"\n\tdescription=\"Extras_description\"\n>\n</choice>\n"

	((xmlindent++))

# build kext packages 

	outline[$((outlinecount++))]="${indent[$xmlindent]}\t<line choice=\"Kexts\">"
	choices[$((choicescount++))]="<choice\n\tid=\"Kexts\"\n\ttitle=\"Kexts_title\"\n\tdescription=\"Kexts_description\"\n>\n</choice>\n"
	((xmlindent++))
	packagesidentity="org.chameleon"
	kexts=($( find "${pkgroot}/Kexts" -type d -name '*.kext' -depth 1 ))
	for (( i = 0 ; i < ${#kexts[@]} ; i++ )) 
	do
		filename="${kexts[$i]##*/}"
		mkdir -p "${1}/${filename%.kext}/Root/"
		ditto --noextattr --noqtn --arch i386 "${kexts[$i]}" "${1}/${filename%.kext}/Root/${filename}"
		find "${1}/${filename%.kext}" -name '.DS_Store' -or -name '.svn' -exec rm -R -f {} \; 2>/dev/null
		fixperms "${1}/${filename%.kext}/Root/"
		chown 501:20 "${1}/${filename%.kext}/Root/"
		buildpackage "${1}/${filename%.kext}" "/.Chameleon/Extra/Extensions" ""
		rm -R -f "${1}/${filename%.kext}"
	done

	((xmlindent--))
	outline[$((outlinecount++))]="${indent[$xmlindent]}\t</line>"

	((xmlindent--))
	outline[$((outlinecount++))]="${indent[$xmlindent]}\t</line>"

# build post install package 

	mkdir -p ${1}/Post/Root
	mkdir -p ${1}/Post/Scripts
	cp -f ${pkgroot}/Scripts/Post/* ${1}/Post/Scripts
	buildpackage "${1}/Post" "/" "" "start_visible=\"false\" start_selected=\"true\""
	outline[$((outlinecount++))]="${indent[$xmlindent]}</choices-outline>"

# build meta package

	makedistribution "${1}" "${2}" "${3}" "${4}" "${5}"

# clean up 

	rm -R -f "${1}"

}

fixperms ()
{
	# $1 path
	find "${1}" -type f -exec chmod 644 {} \;
	find "${1}" -type d -exec chmod 755 {} \;
	chown -R 0:0 "${1}"
}

buildpackage ()
{
#  $1 Path to package to build containing Root and or Scripts
#  $2 Install Location
#  $3 Size
#  $4 Options

if [ -d "${1}/Root" ] && [ "${1}/Scripts" ]; then

	local packagename="${1##*/}"
	local identifier=$( echo ${packagesidentity}.${packagename//_/.} | tr [:upper:] [:lower:] )
	find "${1}" -name '.DS_Store' -delete
	local filecount=$( find "${1}/Root" | wc -l )
	if [ "${3}" ]; then
		local installedsize="${3}"
	else
		local installedsize=$( du -hkc "${1}/Root" | tail -n1 | awk {'print $1'} )
	fi
	local header="<?xml version=\"1.0\"?>\n<pkg-info format-version=\"2\" "

	#[ "${3}" == "relocatable" ] && header+="relocatable=\"true\" "		

	header+="identifier=\"${identifier}\" "
	header+="version=\"${version}\" "

	[ "${2}" != "relocatable" ] && header+="install-location=\"${2}\" "

	header+="auth=\"root\">\n"
	header+="\t<payload installKBytes=\"${installedsize##* }\" numberOfFiles=\"${filecount##* }\"/>\n"
	rm -R -f "${1}/Temp"

	[ -d "${1}/Temp" ] || mkdir -m 777 "${1}/Temp"
	[ -d "${1}/Root" ] && mkbom "${1}/Root" "${1}/Temp/Bom"

	if [ -d "${1}/Scripts" ]; then 
		header+="\t<scripts>\n"
		for script in $( find "${1}/Scripts" -type f \( -name 'pre*' -or -name 'post*' \) )
		do
			header+="\t\t<${script##*/} file=\"./${script##*/}\"/>\n"
		done
		header+="\t</scripts>\n"
		chown -R 0:0 "${1}/Scripts"
		pushd "${1}/Scripts" >/dev/null
		find . -print | cpio -o -z -H cpio > "../Temp/Scripts"
		popd >/dev/null
	fi

	header+="</pkg-info>"
	echo -e "${header}" > "${1}/Temp/PackageInfo"
	pushd "${1}/Root" >/dev/null
	find . -print | cpio -o -z -H cpio > "../Temp/Payload"
	popd >/dev/null
	pushd "${1}/Temp" >/dev/null

	xar -c -f "${1%/*}/${packagename// /}.pkg" --compression none .

	popd >/dev/null

	outline[$((outlinecount++))]="${indent[$xmlindent]}\t<line choice=\"${packagename// /}\"/>"

	if [ "${4}" ]; then
		local choiceoptions="${indent[$xmlindent]}${4}\n"	
	fi
	choices[$((choicescount++))]="<choice\n\tid=\"${packagename// /}\"\n\ttitle=\"${packagename}_title\"\n\tdescription=\"${packagename}_description\"\n${choiceoptions}>\n\t<pkg-ref id=\"${identifier}\" installKBytes='${installedsize}' version='${version}.0.0.${timestamp}' auth='root'>#${packagename// /}.pkg</pkg-ref>\n</choice>\n"

	rm -R -f "${1}"
fi
}

makedistribution ()
{
	rm -f "${1%/*}/${packagename// /}"*.pkg

	find "${1}" -type f -name '*.pkg' -depth 1 | while read component
	do
		mkdir -p "${1}/${packagename}/${component##*/}"
		pushd "${1}/${packagename}/${component##*/}" >/dev/null
		xar -x -f "${1%}/${component##*/}"
		popd >/dev/null
	done

	ditto --noextattr --noqtn "${pkgroot}/Distribution" "${1}/${packagename}/Distribution"
	ditto --noextattr --noqtn "${pkgroot}/Resources" "${1}/${packagename}/Resources"

	find "${1}/${packagename}/Resources" -type d -name '.svn' -exec rm -R -f {} \; 2>/dev/null

	for (( i=0; i < ${#outline[*]} ; i++));
		do
			echo -e "${outline[$i]}" >> "${1}/${packagename}/Distribution"
		done

	for (( i=0; i < ${#choices[*]} ; i++));
		do
			echo -e "${choices[$i]}" >> "${1}/${packagename}/Distribution"
		done

	echo "</installer-gui-script>"  >> "${1}/${packagename}/Distribution"

	perl -i -p -e "s/%CHAMELEONVERSION%/${version%%-*}/g" `find "${1}/${packagename}/Resources" -type f`
	perl -i -p -e "s/%CHAMELEONREVISION%/${revision}/g" `find "${1}/${packagename}/Resources" -type f`

	stage=${stage/RC/Release Candidate }
	stage=${stage/FINAL/2.0 Final}
	perl -i -p -e "s/%CHAMELEONSTAGE%/${stage}/g" `find "${1}/${packagename}/Resources" -type f`

	find "${1}/${packagename}" -name '.DS_Store' -delete
	pushd "${1}/${packagename}" >/dev/null
	xar -c -f "${1%/*}/${packagename// /}-${version}-r${revision}.pkg" --compression none .
	popd >/dev/null

	md5=$( md5 "${1%/*}/${packagename// /}-${version}-r${revision}.pkg" | awk {'print $4'} )
	echo "MD5 (${packagename// /}-${version}-r${revision}.pkg) = ${md5}" > "${1%/*}/${packagename// /}-${version}-r${revision}.pkg.md5"
}

main "${1}" "${2}" "${3}" "${4}" "${5}"

