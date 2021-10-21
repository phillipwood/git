#!/bin/sh

test_description='patience diff algorithm'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-alternative.sh

test_expect_success '--ignore-space-at-eol with a single appended character' '
	printf "a\nb\nc\n" >pre &&
	printf "a\nbX\nc\n" >post &&
	test_must_fail git diff --no-index \
		--patience --ignore-space-at-eol pre post >diff &&
	grep "^+.*X" diff
'

test_diff_frobnitz "patience"

test_diff_unique "patience"

test_expect_success 'non unique context between deletion and addition' '
	test_write_lines a b a b c d c d >file &&
	git add file &&
	test_write_lines a b c d e c d >file &&
	git diff --diff-algorithm=patience file >actual &&
	sed -ne "/^@@/,\$p" actual >hunk &&
	cat >expect <<-\EOF &&
	@@ -1,8 +1,7 @@
	 a
	 b
	-a
	-b
	 c
	 d
	+e
	 c
	 d
	EOF
	test_cmp expect hunk
'

test_expect_success 'non unique context between additon and deletion' '
	test_write_lines a b a b c d c d >file &&
	git add file &&
	test_write_lines a b e a b c d >file &&
	git diff --diff-algorithm=patience file >actual &&
	sed -ne "/^@@/,\$p" actual >hunk &&
	cat >expect <<-\EOF &&
	@@ -1,8 +1,7 @@
	 a
	 b
	+e
	 a
	 b
	 c
	 d
	-c
	-d
	EOF
	test_cmp expect hunk
'

test_done
