#!/bin/bash -e

function firstmajor() {
	declare -i n j=$1
	test $j -gt 0 || return -1
	for j in $@; do
		if [ $j -gt $1 ]; then
			echo $j
			return 0
		fi
	done
	return 1
}; export -f firstmajor

function print_h() {
	declare -i i c=$2 s=$3 e=$4
	local str="$(echo "$1" | head -n$c | tail -n1 | sed -e "s,config[\t ]*,,")"
	echo -n "#define help_"$str" \"" | tr [A-Z] [a-z]
	echo -n "$1\\n" | head -n$e | tail -n$[e-s+1] | sed -e "s,\$,\r," | tr \\n\\r n\\
	echo \"
}; export -f print_h

file="$1"
if test "$0" != "bash"; then
	if test -r "$file"; then
#		echo "$file..." >&2
		filetxt="$(sed -e "s,^[\t ]*,," -e "s,\([\"\\\\]\),\\\\\\1,g" "$file")"
		helps=$(echo "$filetxt" | egrep -ne "^help *" -e "^---help--- *" | cut -d\:  -f1)
		configs=$(echo "$filetxt" | egrep -ne "^config *" | cut -d\:  -f1)
		endmenus=$(echo "$filetxt" | egrep -ne "^endmenu *" | cut -d\:  -f1)
		let last=$(echo "$filetxt" | wc -l)+2
		
		declare -i i c s e
		for i in $configs; do
#			echo -n "c:$i" >&2
			s=$(firstmajor $i $helps)
			test $s -gt 0
			e=$(firstmajor $s $configs || firstmajor $s $endmenus $last)
			let s++ e-=2
			test $e -ge $s
#			echo " s:$s e:$e" >&2
			print_h "$filetxt" $i $s $e
		done 
		for fle in $(cat "$file" | egrep -e "^[ \t]*source " | sed -e "s,^[ \t]*source *\(.*\),\\1,"); do
			$0 $fle
		done
	else
		echo
		echo "USAGE EXAMPLE: $(basename $0) Config.in > generated/help.h"
		echo
		false
	fi | sed -e "s,\\\\n\\\\n\"$,\\\\n\","
fi

