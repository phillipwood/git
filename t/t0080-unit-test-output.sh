#!/bin/sh

test_description='Test the output of the unit test framework'

. ./test-lib.sh

test_expect_success 'TAP output from unit tests' '
	cat >expect <<-EOF &&
	ok 1 - passing test
	ok 2 - passing test and assertion return 0
	# check "1 == 2" failed at t/unit-tests/t-basic.c:68
	#    left: 1
	#   right: 2
	not ok 3 - failing test
	ok 4 - failing test and assertion return -1
	not ok 5 - passing TEST_TODO() # TODO
	ok 6 - passing TEST_TODO() returns 0
	# todo check ${SQ}check(x)${SQ} succeeded at t/unit-tests/t-basic.c:17
	not ok 7 - failing TEST_TODO()
	ok 8 - failing TEST_TODO() returns -1
	# check "0" failed at t/unit-tests/t-basic.c:22
	# skipping test - missing prerequisite
	# skipping check ${SQ}1${SQ} at t/unit-tests/t-basic.c:24
	ok 9 - test_skip() # SKIP
	ok 10 - skipped test returns 0
	# skipping test - missing prerequisite
	ok 11 - test_skip() inside TEST_TODO() # SKIP
	ok 12 - test_skip() inside TEST_TODO() returns 0
	# check "0" failed at t/unit-tests/t-basic.c:40
	not ok 13 - TEST_TODO() after failing check
	ok 14 - TEST_TODO() after failing check returns -1
	# check "0" failed at t/unit-tests/t-basic.c:48
	not ok 15 - failing check after TEST_TODO()
	ok 16 - failing check after TEST_TODO() returns -1
	# check "!strcmp("\thello\\\\", "there\"\n")" failed at t/unit-tests/t-basic.c:53
	#    left: "\011hello\\\\"
	#   right: "there\"\012"
	# check "!strcmp("hello", NULL)" failed at t/unit-tests/t-basic.c:54
	#    left: "hello"
	#   right: NULL
	# check "${SQ}a${SQ} == ${SQ}\n${SQ}" failed at t/unit-tests/t-basic.c:55
	#    left: ${SQ}a${SQ}
	#   right: ${SQ}\012${SQ}
	# check "${SQ}\\\\${SQ} == ${SQ}\\${SQ}${SQ}" failed at t/unit-tests/t-basic.c:56
	#    left: ${SQ}\\\\${SQ}
	#   right: ${SQ}\\${SQ}${SQ}
	not ok 17 - messages from failing string and char comparison
	# BUG: test has no checks at t/unit-tests/t-basic.c:83
	not ok 18 - test with no checks
	ok 19 - test with no checks returns -1
	1..19
	EOF

	! "$GIT_BUILD_DIR"/t/unit-tests/t-basic >actual &&
	test_cmp expect actual
'

test_done
