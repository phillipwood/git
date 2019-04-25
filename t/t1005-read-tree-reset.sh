#!/bin/sh

test_description='read-tree -u --reset'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

# two-tree test

test_expect_success 'setup' '
	git init &&
	mkdir df &&
	echo content >df/file &&
	git add df/file &&
	git commit -m one &&
	git ls-files >expect &&
	rm -rf df &&
	echo content >df &&
	git add df &&
	echo content >new &&
	git add new &&
	git commit -m two &&
	git ls-files >expect-two
'

test_expect_success '--protect-untracked option sanity checks' '
	read_tree_u_must_fail --reset --protect-untracked HEAD &&
	read_tree_u_must_fail --reset --no-protect-untracked HEAD &&
	read_tree_u_must_fail -m -u --protect-untracked HEAD &&
	read_tree_u_must_fail -m -u --no-protect-untracked
'

test_expect_success 'exclude option sanity checks' '
	read_tree_u_must_fail --reset -u --exclude-standard HEAD &&
	read_tree_u_must_fail --reset --protect-untracked --exclude-standard &&
	read_tree_u_must_fail --reset -u --protect-untracked \
			      --exclude-standard \
			      --exclude-per-directory=.gitignore HEAD &&
	read_tree_u_must_fail --reset -u --protect-untracked \
			      --exclude-per-directory=gitignore \
			      --exclude-per-directory=.gitignore HEAD &&
	read_tree_u_must_fail --reset --exclude-per-directory=.gitignore HEAD &&
	read_tree_u_must_succeed --reset -u --exclude-per-directory=.gitignore \
				 HEAD
'

test_expect_success 'reset should reset worktree' '
	echo changed >df &&
	read_tree_u_must_succeed -u --reset HEAD^ &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'reset --protect-untracked protects untracked file' '
	echo changed >new &&
	read_tree_u_must_fail_save_err -u --reset --protect-untracked HEAD &&
	echo "error: Untracked working tree file '\'new\'' would be overwritten by merge." >expected-err &&
	test_cmp expected-err actual-err
'

test_expect_success 'reset --protect-untracked protects untracked directory' '
	rm new &&
	mkdir new &&
	echo untracked >new/untracked &&
	read_tree_u_must_fail_save_err -u --reset --protect-untracked HEAD &&
	echo "error: Updating '\'new\'' would lose untracked files in it" >expected-err &&
	test_cmp expected-err actual-err
'

test_expect_success 'reset --protect-untracked --exclude-standard overwrites ignored path' '
	test_when_finished "rm .git/info/exclude" &&
	echo missing >.git/info/exclude &&
	read_tree_u_must_fail -u --reset --protect-untracked \
			      --exclude-standard HEAD &&
	echo new >.git/info/exclude &&
	echo changed >df/file &&
	read_tree_u_must_succeed -u --reset --protect-untracked \
				 --exclude-standard HEAD &&
	git ls-files >actual &&
	test_cmp expect-two actual
'

test_expect_success 'reset --protect-untracked resets' '
	echo changed >df &&
	read_tree_u_must_succeed -u --reset --protect-untracked HEAD^ &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'reset should remove remnants from a failed merge' '
	read_tree_u_must_succeed --reset -u HEAD &&
	git ls-files -s >expect &&
	sha1=$(git rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | git update-index --index-info &&
	>old &&
	git ls-files -s &&
	read_tree_u_must_succeed --reset -u HEAD &&
	git ls-files -s >actual &&
	! test -f old
'

test_expect_success 'two-way reset should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	git ls-files -s >expect &&
	sha1=$(git rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | git update-index --index-info &&
	>old &&
	git ls-files -s &&
	read_tree_u_must_succeed --reset -u HEAD HEAD &&
	git ls-files -s >actual &&
	! test -f old
'

test_expect_success 'Porcelain reset should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	git ls-files -s >expect &&
	sha1=$(git rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | git update-index --index-info &&
	>old &&
	git ls-files -s &&
	git reset --hard &&
	git ls-files -s >actual &&
	! test -f old
'

test_expect_success 'Porcelain checkout -f should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	git ls-files -s >expect &&
	sha1=$(git rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | git update-index --index-info &&
	>old &&
	git ls-files -s &&
	git checkout -f &&
	git ls-files -s >actual &&
	! test -f old
'

test_expect_success 'Porcelain checkout -f HEAD should remove remnants too' '
	read_tree_u_must_succeed --reset -u HEAD &&
	git ls-files -s >expect &&
	sha1=$(git rev-parse :new) &&
	(
		echo "100644 $sha1 1	old" &&
		echo "100644 $sha1 3	old"
	) | git update-index --index-info &&
	>old &&
	git ls-files -s &&
	git checkout -f HEAD &&
	git ls-files -s >actual &&
	! test -f old
'

test_done
