#!/bin/bash

# set com.apple.Boot.plist options

overide="ForceWake"
string="y"

main ()
{

	bootplist="${3}/.Chameleon/Extra/com.apple.Boot.plist"
	systemplist="/Library/Preferences/SystemConfiguration/com.apple.Boot.plist"

	bootoptionshdextra[1]="<key>${overide}</key>"
	bootoptionshdextra[2]="<string>${string}</string>"

	[ ! -d "${bootplist%/*}" ] && mkdir -p "${bootplist%/*}"
	
	if [ ! -f "${bootplist}" ]; then
		if [ -f "${systemplist}" ]; then
			cp  -f "${systemplist}" "${bootplist}"
		fi
	fi
	
	xmlvalue=$( getxmlvalue ${overide} "${bootplist}" | tr "[:upper:]" "[:lower:]" )
	
	case "${xmlvalue:0:1}" in
				
		y)	echo "${overide}=y already set in ${bootplist} skipping."
			;;

		n)	echo "${overide}=n is set in ${bootplist} leaving."
			;;
	
	 	*)	echo "Adding ${overide}=y to ${bootplist}"
	 		array=("${bootoptionshdextra[@]}")
			searchfilereplaceline "${bootplist}" "<key>Kernel Flags</key>" "" "" "2"
			;;
	esac

	chown "${USER}:20" "${bootplist}"
}

getxmlvalue ()
{
# 1 xml key
# 2 xml file
if [ -f "${2}" ]; then
	local value
	while read value; do
	if [ ! "${value}" = "${value/<key>${1}</key>/}" ]; then
		read value
		value="${value#*<}" ; value="<${value}" ; value="${value#*>}" ; value="${value# *}" ; value="${value%%<*}"
		echo "$value"
		break
	fi
	done < "${2}"
fi
}

searchfilereplaceline ()
{

mv "${1}" "${1}.orig"

prefunctionifs="${IFS}"

unset fileinput
unset fileoutput
unset find
unset replace
unset deletelines
unset deletelinesskip
unset insertlinesskip

fileinput="${1}.orig"
fileoutput="${1}"
find="${2}"
replace="${3}"
deletelines="${4%,*}"
insertlinesskip="${5}"

matchlinefound="0"

if [ "${#4}" = "${4#*,}" ]; then
        deletelinesskip="0"
	else
	deletelinesskip="${4#*,}"
fi

IFS="\n"
while read line
do
	{
	if [ ! "${line}" = "${line/${find}/}" ]; then
		{
		# Trim the longest match from the end for <*
		xmlelementindent="${line%%<*}"

		# Trim the longest match from the start for *<
		xmlelementtemp="${line#*<}"
		# Add back in stripped <
		xmlelement="<${xmlelementtemp}"

		# Trim the shortest match from the start for <
		xmltagtemp="${xmlelement#<}"
		# Trim the longest match from the end for >*
		xmltag="${xmltagtemp%%>*}"

		# Trim the shortest match from the start for *>
		xmltexttemp="${xmlelement#*>}"
		# Trim the longest match from the end for <*
		xmltext="${xmltexttemp%%<*}"

		if [ "${replace}" ]; then
			{
				echo "${xmlelementindent}<${xmltag}>${replace}</${xmltag}>" >>"${fileoutput}"
			}
			else
			{
				echo "${line}" >>"${fileoutput}"
			}
		fi
		matchlinefound="1"

		}
		else
		{

		if [ "${insertlinesskip}" ] && [ "${matchlinefound}" -gt 0 ] && [ "${matchlinefound}" -le "${insertlinesskip}" ]; then
			{
			if [ "${matchlinefound}" = "${insertlinesskip}" ]; then
				{
				arraysize=0
				while [ ${arraysize} -lt ${#array[@]} ];
				do
					echo "${xmlelementindent}${array[${arraysize}]}" >>"${fileoutput}"
				        let arraysize="${arraysize}+1"
				done
				}
			fi
			}
		fi

		if [ "${deletelines}" ] && [ "${matchlinefound}" -gt 0 ] && [ "${matchlinefound}" -lt $((${deletelines}+${deletelinesskip})) ] && [ "${matchlinefound}" -ge ${deletelinesskip} ]; then
			{
			:
			}
			else
			{
			echo "${line}" >>"${fileoutput}"
			}
		fi

		if [ "${matchlinefound}" -gt 0 ]; then
			let matchlinefound="${matchlinefound}+1"
		fi

		}
	fi
	}
done < "${fileinput}"

IFS=${prefunctionifs}

rm -f "${fileinput}"
}

main "${1}" "${2}" "${3}" "${4}"
