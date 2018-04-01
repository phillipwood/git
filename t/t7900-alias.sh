#!/bin/sh
#
# Copyright (c) 2020 Phillip Wood
#

test_description='git alias

Test alias builtin command
'
. ./test-lib.sh

SQ="'"

test_expect_success 'getting alias works' '
	test_config alias.co checkout &&
	git alias co >actual &&
	echo checkout >expect &&
	test_cmp expect actual
'

test_expect_success 'getting non-existent alias fails' '
	test_must_fail git alias does-not-exist 2>actual &&
	echo "error: alias ${SQ}does-not-exist${SQ} does not exist" >expect &&
	test_cmp expect actual
'

test_expect_success 'getting empty alias fails' '
	test_config alias.empty "" &&
	git alias empty 2>actual &&
	echo "warning: alias ${SQ}empty${SQ} is empty" >expect &&
	test_cmp expect actual
'

test_expect_success 'setting a new alias works' '
	test_when_finished "git config --global --unset alias.lp" &&
	git alias lp log --pretty="%s %h" --patch  &&
	git config --get --global alias.lp >actual &&
	echo "log \"--pretty=%s %h\" --patch" >expect &&
	test_cmp expect actual
'

test_expect_success 'updating an alias overwrites existing definition' '
	test_when_finished "rm included-config" &&
	git config --file=included-config alias.co checkout &&
	test_config include.path "$TRASH_DIRECTORY"/included-config &&
	git alias co commit &&
	git config --get --file=included-config alias.co >actual &&
	echo commit >expect &&
	test_cmp expect actual
'

test_expect_success 'setting an alias to the name of a builtin fails' '
	test_must_fail git alias checkout some other command 2>actual &&
	echo "error: ${SQ}checkout${SQ} is a git command" >expect &&
	test_cmp expect actual
'
test_expect_success 'setting an alias to the name of an external script' '
	write_script git-mycmd <<-\EOF &&
	true
	EOF
	test_must_fail env PATH="$TRASH_DIRECTORY:$PATH" \
		git alias mycmd checkout 2>actual &&
	echo "error: ${SQ}mycmd${SQ} is a git command" >expect &&
	test_cmp expect actual
'

test_expect_success 'setting an empty alias fails' '
	test_must_fail git alias mycmd "   " 2>actual &&
	echo "error: alias definition is empty" >expect &&
	test_cmp expect actual
'

test_done
