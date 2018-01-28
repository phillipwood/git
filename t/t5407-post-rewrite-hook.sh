#!/bin/sh
#
# Copyright (c) 2010 Thomas Rast
#

test_description='Test the post-rewrite hook.'
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit A foo A &&
	test_commit B foo B &&
	test_commit C foo C &&
	test_commit D foo D &&
	git checkout A^0 &&
	test_commit E bar E &&
	test_commit F foo F &&
	git checkout master &&

	write_script amend-head <<-\EOS
	git commit --amend --only --allow-empty -m "$1"
	EOS
'

mkdir .git/hooks

cat >.git/hooks/post-rewrite <<EOF
#!/bin/sh
echo \$@ > "$TRASH_DIRECTORY"/post-rewrite.args
cat > "$TRASH_DIRECTORY"/post-rewrite.data
EOF
chmod u+x .git/hooks/post-rewrite

clear_hook_input () {
	rm -f post-rewrite.args post-rewrite.data
}

verify_hook_input () {
	test_cmp expected.args "$TRASH_DIRECTORY"/post-rewrite.args &&
	test_cmp expected.data "$TRASH_DIRECTORY"/post-rewrite.data
}

test_expect_success 'git commit --amend' '
	clear_hook_input &&
	echo "D new message" > newmsg &&
	oldsha=$(git rev-parse HEAD^0) &&
	git commit -Fnewmsg --amend &&
	echo amend > expected.args &&
	echo $oldsha $(git rev-parse HEAD^0) > expected.data &&
	verify_hook_input
'

test_expect_success 'git commit --amend --no-post-rewrite' '
	clear_hook_input &&
	echo "D new message again" > newmsg &&
	git commit --no-post-rewrite -Fnewmsg --amend &&
	test ! -f post-rewrite.args &&
	test ! -f post-rewrite.data
'

test_expect_success 'git rebase --apply' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --apply --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase --apply --skip' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --apply --onto A B &&
	test_must_fail git rebase --skip &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase --apply --skip the last one' '
	git reset --hard F &&
	clear_hook_input &&
	test_must_fail git rebase --apply --onto D A &&
	git rebase --skip &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse E) $(git rev-parse HEAD)
	$(git rev-parse F) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -m' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase -m --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -m --skip' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase -m --onto A B &&
	test_must_fail git rebase --skip &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase with implicit use of merge backend' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --keep-empty --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase --skip with implicit use of merge backend' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail git rebase --keep-empty --onto A B &&
	test_must_fail git rebase --skip &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

# Helper to work around the lack of one-shot exporting for
# test_must_fail (as it is a shell function)
test_fail_interactive_rebase () {
	(
		FAKE_LINES="$1" &&
		shift &&
		export FAKE_LINES &&
		test_must_fail git rebase -i "$@"
	)
}

test_expect_success 'git rebase -i (unchanged)' '
	git reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "1 2" --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (skip)' '
	git reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "2" --onto A B &&
	echo D > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (squash)' '
	git reset --hard D &&
	clear_hook_input &&
	test_fail_interactive_rebase "1 squash 2" --onto A B &&
	echo C > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (fixup without conflict)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="1 fixup 2" git rebase -i B &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (double edit)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="edit 1 edit 2" git rebase -i B &&
	git rebase --continue &&
	echo something > foo &&
	git add foo &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (exec)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="edit 1 exec_false 2" git rebase -i B &&
	echo something >bar &&
	git add bar &&
	# Fails because of exec false
	test_must_fail git rebase --continue &&
	git rebase --continue &&
	echo rebase >expected.args &&
	cat >expected.data <<-EOF &&
	$(git rev-parse C) $(git rev-parse HEAD^)
	$(git rev-parse D) $(git rev-parse HEAD)
	EOF
	verify_hook_input
'

test_expect_success 'git rebase -i (exec amends commit)' '
	git reset --hard D &&
	clear_hook_input &&
	test_must_fail env FAKE_LINES="1 \
		exec_./amend-head_edited-1a \
		exec_./amend-head_edited-1b \
		2 \
		exec_false \
		3 \
		break" git rebase -i A &&
	./amend-head edited-2 &&
	git rebase --continue &&
	./amend-head edited-3 &&
	git rebase --continue &&
	echo rebase >expected.args &&
	printf "%s %s\n%s %s\n%s %s\n%s %s\n%s %s\n%s %s\n" >expected.data \
		$(git rev-parse B        HEAD@{6} \
				HEAD@{6} HEAD^^   \
				C        HEAD@{4} \
				HEAD@{4} HEAD^    \
				D        HEAD@{2} \
				HEAD@{2} HEAD) &&

	verify_hook_input
'

test_expect_success 'git rebase -i (exec amends onto)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="exec_./amend-head_edited 1 \
		exec_git_commit_--allow-empty_-m_empty \
		exec_./amend-head_edited-empty" git rebase -i B &&
	echo rebase >expected.args &&
	printf "%s %s\n%s %s\n" >expected.data \
		$(git rev-parse B HEAD^^ \
				C HEAD^) &&
	verify_hook_input
'

test_expect_success 'git rebase -i (fixup after exec)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="1 exec_true fixup 2 squash 3" git rebase -i A &&
	echo rebase >expected.args &&
	printf "%s %s\n%s %s\n%s %s\n%s %s\n" >expected.data \
		$(git rev-parse B        HEAD@{2} \
				HEAD@{2} HEAD     \
				C        HEAD     \
				D        HEAD) &&
	verify_hook_input
'

test_expect_success 'git rebase -i (exec after reset)' '
	git reset --hard D &&
	clear_hook_input &&
	FAKE_LINES="reset_C \
		exec_./amend-head_edited 3" git rebase -i A &&
	echo rebase >expected.args &&
	printf "%s %s\n%s %s\n" >expected.data \
		$(git rev-parse C HEAD^ \
				D HEAD) &&
	verify_hook_input
'

test_done
