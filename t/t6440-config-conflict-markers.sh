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

test_done
