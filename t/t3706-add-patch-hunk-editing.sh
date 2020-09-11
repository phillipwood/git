#!/bin/sh
#
# Copyright (c) 2020 Phillip Wood
#

test_description='add --patch hunk editing tests'
. ./test-lib.sh

# Setup an editor that can edit a hunk multiple times.
#
# If SED_CMD_<n> is set in the environment then it is used to edit the
# file the n'th time that the editor is called. Multiple commands are
# separated by ';' overriding sed's ';' handling so it can be used
# with all commands
#
# If HUNK_FILE_<n> is set in the environment then it contains a path
# to a hunk to use as the edited hunk the n'th time that the editor
# is called.
#
# Otherwise the editor leaves the hunk untouched

test_expect_success 'setup' '
	write_script editor <<-\EOF &&
	test -f editor-state && read n <editor-state || n=0
	n=$((n+1)) &&
	echo $n >editor-state &&
	file="$1" &&
	cat "$file" >unedited-hunk-$n &&
	eval sed_cmd=\"\$SED_CMD_$n\" &&
	eval hunk_file=\"\$HUNK_FILE_$n\" &&
	if test -n "$sed_cmd"
	then
		printf "sed args:" &&
		IFS=";" &&
		set -- $sed_cmd &&
		IFS=" " &&
		count=$# &&
		i=0 &&
		while test $i -lt $count
		do
			case "$1" in
				-*) printf %s " $1" &&
				    x="$1"
				    ;;
				 *) break
				    ;;
			esac &&
			shift &&
			set -- "$@" "$x" &&
			i=$((i+1))
		done &&
		while test $i -lt $count
		do
			printf " -e'\''%s'\''" "$1" &&
			x="-e$1" &&
			shift &&
			set -- "$@" "$x" &&
			i=$((i+1))
		done &&
		printf "\n" &&
		sed "$@" "$file" >"$file.tmp" &&
		mv "$file.tmp" "$file"
	elif test -n "$hunk_file"
	then
		cat "$hunk_file" >"$file"
	fi &&
	if test -s "$file"
	then
		echo "edited hunk:" &&
		sed "s/^/	/" "$file"
	else
		echo "edited hunk is empty"
	fi
	EOF

	test_set_editor "$(pwd)/editor" &&
	test_write_lines a b c a b c a b c >file &&
	git add file &&
	git commit -minitial &&
	test_tick
'

cleanup() {
	rm -f editor-state unedited-hunk-* &&
	git read-tree -u --reset HEAD
}

check_staged_contents() {
	git cat-file blob :"$1" >actual &&
	shift &&
	test_write_lines "$@" >expect &&
	test_cmp expect actual
}

# usage check_unedited_hunk <n>
# checks that unedited-hunk-<n> matches the hunk read from stdin
check_unedited_hunk() {
	n="$1" &&
	cat >expected-hunk
	sed -n "/^# error:/p;/^#/!p" unedited-hunk-$n >actual-hunk &&
	test_cmp expected-hunk actual-hunk
}

# Check that (i) there is only one hunk header in unedited-hunk-<n>,
# (ii) the hunk header is the first uncommented line and (iii) it
# matches the hunk header from the original hunk.
check_hunk_header() {
	n="$1"
	sed -n '# print the first uncommented line and all other line beginning with '@'
	/^#/!{
		p
		:loop
		n
		/^@/p
		b loop
	}' unedited-hunk-$n >actual-header &&
	grep ^@ unedited-hunk-1 >expected-header &&
	test_cmp expected-header actual-header
}

test_expect_success 'leaving hunk empty asks again' '
	test_when_finished cleanup &&
	test_write_lines a x c a b c a y b c >file &&
	# 1 - Comment out hunk
	# 2 - Delete hunk
	# 3 - Delete everything
	# 4 - Split hunk and stage second half
	test_write_lines e e e s n y  q |
	SED_CMD_1="s/^[^#]/#&/" \
	SED_CMD_2="/^[^#]/d" \
	SED_CMD_3="d" \
	git add -p &&

	# Check hunk is restored before each edit
	test_cmp unedited-hunk-1 unedited-hunk-2 &&
	test_cmp unedited-hunk-1 unedited-hunk-3 &&

	check_staged_contents file a b c a b c a y b c
'

test_expect_success 'detect bad lines' '
	test_when_finished cleanup &&
	test_write_lines a x y b z c a b c a b c >file &&
	# 1 - Create some bad lines and delete some of the help text
	# 2 - Fix the bad lines
	test_write_lines e y q |
	SED_CMD_1="/^+x/c\;@@ -5,2 +6,3 @@;/^+z/c\;@@ z;s/^ c/c/;/^#.*[+-]/d" \
	SED_CMD_2="/^@@ -5/d;/^@@ z/c\;+z;s/^c/ c/" \
	git add -p &&

	# Check that the instructions are the same the second time the hunk
	# is edited
	grep "^#" unedited-hunk-1 >expected-instructions &&
	sed -n "/^# error:/d;/^#/p" unedited-hunk-2 >actual-instructions &&
	test_cmp expected-instructions actual-instructions &&

	# Check the error comments
	cat <<-\EOF | check_unedited_hunk 2 &&
	@@ -1,5 +1,8 @@
	 a
	# error: can only handle a single hunk
	@@ -5,2 +6,3 @@
	+y
	 b
	# error: invalid line
	@@ z
	# error: invalid line
	c
	 a
	 b
	EOF

	check_staged_contents file a y b z c a b c a b c
'

test_expect_success 'badly placed hunk header' '
	test_when_finished cleanup &&
	test_write_lines a x y b z c a b c a b c >file &&
	# 1 - Move the hunk header
	# 2 - Delete any hunk headers after the first context line
	test_write_lines e y q |
	SED_CMD_1="/^@/h;/^@/d;/^+x/g" \
	SED_CMD_2="/^ /{;:loop;n;/^@/d;b loop;}" \
	git add -p &&

	# Check the error comments
	cat <<-\EOF | check_unedited_hunk 2 &&
	@@ -1,5 +1,8 @@
	 a
	# error: hunk header must be the first line
	@@ -1,5 +1,8 @@
	+y
	 b
	+z
	 c
	 a
	 b
	EOF


	check_staged_contents file a y b z c a b c a b c
'

test_expect_success 'replace deleted hunk header when re-editing hunk' '
	test_when_finished cleanup &&
	test_write_lines a x y b z c a b c a b c >file &&
	# 1 - Create a bad line and delete the hunk header
	# 2 - Fix the bad line and delete the hunk header
	test_write_lines e y q |
	SED_CMD_1="s/^+x/x/;/^@/d" \
	SED_CMD_2="/^[x@]/d;" \
	git add -p &&

	check_hunk_header 2 &&
	check_staged_contents file a y b z c a b c a b c
'

test_expect_success 'replace bad hunk header when re-editing hunk' '
	test_when_finished cleanup &&
	test_write_lines a x y b z c a b c a b c >file &&
	# 1 - Create a bad line and bad hunk header
	# 2 - Fix the bad line but keep the bad hunk header
	test_write_lines e y q |
	SED_CMD_1="s/^+x/x/;/^@/c\;@@ -3/" \
	SED_CMD_2="/^x/d;/^@/c\;@@ -3" \
	git add -p &&

	check_hunk_header 2 &&
	check_staged_contents file a y b z c a b c a b c
'

test_done
