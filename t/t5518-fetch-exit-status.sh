#!/bin/sh
#
# Copyright (c) 2008 Dmitry V. Levin
#

test_description='fetch exit status test'

if ! test_lazy_prereq WITH_BREAKING_CHANGES
then
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
	export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
fi

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	git commit -m initial &&

	git checkout -b side &&
	echo side >file &&
	git commit -a -m side &&

	git checkout main &&
	echo next >file &&
	git commit -a -m next
'

test_expect_success 'non-fast-forward fetch' '

	test_must_fail git fetch . main:side

'

test_expect_success 'forced update' '

	git fetch . +main:side

'

test_done
