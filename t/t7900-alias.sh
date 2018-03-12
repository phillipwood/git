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

test_done
