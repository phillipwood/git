#!/bin/sh

test_description='merge style conflict markers configurations'

. ./test-lib.sh

fill () {
	for i
	do
		echo "$i"
	done
}

test_expect_success 'merge' '
	test_create_repo merge &&
	(
		cd merge &&

		fill 1 2 3 >content &&
		git add content &&
		git commit -m base &&

		git checkout -b r &&
		echo six >>content &&
		git commit -a -m right &&

		git checkout master &&
		echo 7 >>content &&
		git commit -a -m left &&

		test_must_fail git merge r &&
		! grep -E "\|+" content &&

		git reset --hard &&
		test_must_fail git -c merge.conflictstyle=diff3 merge r &&
		grep -E "\|+" content &&

		git reset --hard &&
		test_must_fail git -c merge.conflictstyle=merge merge r &&
		! grep -E "\|+" content
	)
'

test_expect_success 'merge-tree' '
	test_create_repo merge-tree &&
	(
		cd merge-tree &&

		test_commit initial initial-file initial &&
		test_commit r content r &&
		git reset --hard initial &&
		test_commit l content l &&

		git merge-tree initial r l >actual &&
		! grep -E "\|+" actual &&

		git -c merge.conflictstyle=diff3 merge-tree initial r l >actual &&
		grep -E "\|+" actual &&

		git -c merge.conflictstyle=merge merge-tree initial r l >actual &&
		! grep -E "\|+" actual
	)
'

test_expect_success 'notes' '
	test_create_repo notes &&
	(
		test_commit initial &&

		git -c core.notesRef=refs/notes/b notes add -m b initial &&

		git update-ref refs/notes/r refs/notes/b &&
		git -c core.notesRef=refs/notes/r notes add -f -m r initial &&

		git update-ref refs/notes/l refs/notes/b &&
		git config core.notesRef refs/notes/l &&
		git notes add -f -m l initial &&

		test_must_fail git notes merge r &&
		! grep -E "\|+" .git/NOTES_MERGE_WORKTREE/* &&

		git notes merge --abort &&
		test_must_fail git -c merge.conflictstyle=diff3 notes merge r &&
		grep -E "\|+" .git/NOTES_MERGE_WORKTREE/* &&

		git notes merge --abort &&
		test_must_fail git -c merge.conflictstyle=merge notes merge r &&
		! grep -E "\|+" .git/NOTES_MERGE_WORKTREE/*
	)
'

test_expect_success 'checkout' '
	test_create_repo checkout &&
	(
		test_commit checkout &&

		fill a b c d e >content &&
		git add content &&
		git commit -m initial &&

		git checkout -b simple master &&
		fill a c e >content &&
		git commit -a -m simple &&

		fill b d >content &&
		git checkout --merge master &&
		! grep -E "\|+" content &&

		git config merge.conflictstyle merge &&

		git checkout -f simple &&
		fill b d >content &&
		git checkout --merge --conflict=diff3 master &&
		grep -E "\|+" content &&

		git checkout -f simple &&
		fill b d >content &&
		git checkout --merge --conflict=merge master &&
		! grep -E "\|+" content
	)
'

test_done
