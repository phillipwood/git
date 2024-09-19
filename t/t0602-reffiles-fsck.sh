#!/bin/sh

test_description='Test reffiles backend consistency check'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=files
export GIT_TEST_DEFAULT_REF_FORMAT
TEST_PASSES_SANITIZE_LEAK=true

. ./test-lib.sh

test_expect_success 'ref name should be checked' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&

	git commit --allow-empty -m initial &&
	git checkout -b branch-1 &&
	git tag tag-1 &&
	git commit --allow-empty -m second &&
	git checkout -b branch-2 &&
	git tag tag-2 &&
	git tag multi_hierarchy/tag-2 &&

	cp $branch_dir_prefix/branch-1 $branch_dir_prefix/.branch-1 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/.branch-1: badRefName: invalid refname format
	EOF
	rm $branch_dir_prefix/.branch-1 &&
	test_cmp expect err &&

	cp $branch_dir_prefix/branch-1 $branch_dir_prefix/@ &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/@: badRefName: invalid refname format
	EOF
	rm $branch_dir_prefix/@ &&
	test_cmp expect err &&

	cp $tag_dir_prefix/multi_hierarchy/tag-2 $tag_dir_prefix/multi_hierarchy/@ &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/tags/multi_hierarchy/@: badRefName: invalid refname format
	EOF
	rm $tag_dir_prefix/multi_hierarchy/@ &&
	test_cmp expect err &&

	cp $tag_dir_prefix/tag-1 $tag_dir_prefix/tag-1.lock &&
	git refs verify 2>err &&
	rm $tag_dir_prefix/tag-1.lock &&
	test_must_be_empty err &&

	cp $tag_dir_prefix/tag-1 $tag_dir_prefix/.lock &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/tags/.lock: badRefName: invalid refname format
	EOF
	rm $tag_dir_prefix/.lock &&
	test_cmp expect err
'

test_expect_success 'ref name check should be adapted into fsck messages' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	git commit --allow-empty -m initial &&
	git checkout -b branch-1 &&
	git tag tag-1 &&
	git commit --allow-empty -m second &&
	git checkout -b branch-2 &&
	git tag tag-2 &&

	cp $branch_dir_prefix/branch-1 $branch_dir_prefix/.branch-1 &&
	git -c fsck.badRefName=warn refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/.branch-1: badRefName: invalid refname format
	EOF
	rm $branch_dir_prefix/.branch-1 &&
	test_cmp expect err &&

	cp $branch_dir_prefix/branch-1 $branch_dir_prefix/@ &&
	git -c fsck.badRefName=ignore refs verify 2>err &&
	test_must_be_empty err
'

test_expect_success 'regular ref content should be checked (individual)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	git refs verify 2>err &&
	test_must_be_empty err &&

	printf "%s" "$(git rev-parse main)" >$branch_dir_prefix/branch-no-newline &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-no-newline: refMissingNewline: missing newline
	EOF
	rm $branch_dir_prefix/branch-no-newline &&
	test_cmp expect err &&

	printf "%s garbage" "$(git rev-parse main)" >$branch_dir_prefix/branch-garbage &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-garbage: trailingRefContent: trailing garbage in ref
	EOF
	rm $branch_dir_prefix/branch-garbage &&
	test_cmp expect err &&

	printf "%s\n\n\n" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-1 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/tags/tag-garbage-1: trailingRefContent: trailing garbage in ref
	EOF
	rm $tag_dir_prefix/tag-garbage-1 &&
	test_cmp expect err &&

	printf "%s\n\n\n  garbage" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-2 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/tags/tag-garbage-2: trailingRefContent: trailing garbage in ref
	EOF
	rm $tag_dir_prefix/tag-garbage-2 &&
	test_cmp expect err &&

	printf "%s    garbage\n\na" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-3 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/tags/tag-garbage-3: trailingRefContent: trailing garbage in ref
	EOF
	rm $tag_dir_prefix/tag-garbage-3 &&
	test_cmp expect err &&

	printf "%s garbage" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-4 &&
	test_must_fail git -c fsck.trailingRefContent=error refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/tags/tag-garbage-4: trailingRefContent: trailing garbage in ref
	EOF
	rm $tag_dir_prefix/tag-garbage-4 &&
	test_cmp expect err &&

	printf "%sx" "$(git rev-parse main)" >$tag_dir_prefix/tag-bad-1 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/tags/tag-bad-1: badRefContent: invalid ref content
	EOF
	rm $tag_dir_prefix/tag-bad-1 &&
	test_cmp expect err &&

	printf "xfsazqfxcadas" >$tag_dir_prefix/tag-bad-2 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/tags/tag-bad-2: badRefContent: invalid ref content
	EOF
	rm $tag_dir_prefix/tag-bad-2 &&
	test_cmp expect err &&

	printf "xfsazqfxcadas" >$branch_dir_prefix/a/b/branch-bad &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/a/b/branch-bad: badRefContent: invalid ref content
	EOF
	rm $branch_dir_prefix/a/b/branch-bad &&
	test_cmp expect err
'

test_expect_success 'regular ref content should be checked (aggregate)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	printf "%s" "$(git rev-parse main)" >$branch_dir_prefix/branch-no-newline &&
	printf "%s garbage" "$(git rev-parse main)" >$branch_dir_prefix/branch-garbage &&
	printf "%s\n\n\n" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-1 &&
	printf "%s\n\n\n  garbage" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-2 &&
	printf "%s    garbage\n\na" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-3 &&
	printf "%s garbage" "$(git rev-parse main)" >$tag_dir_prefix/tag-garbage-4 &&
	printf "%sx" "$(git rev-parse main)" >$tag_dir_prefix/tag-bad-1 &&
	printf "xfsazqfxcadas" >$tag_dir_prefix/tag-bad-2 &&
	printf "xfsazqfxcadas" >$branch_dir_prefix/a/b/branch-bad &&

	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/a/b/branch-bad: badRefContent: invalid ref content
	error: refs/tags/tag-bad-1: badRefContent: invalid ref content
	error: refs/tags/tag-bad-2: badRefContent: invalid ref content
	warning: refs/heads/branch-garbage: trailingRefContent: trailing garbage in ref
	warning: refs/heads/branch-no-newline: refMissingNewline: missing newline
	warning: refs/tags/tag-garbage-1: trailingRefContent: trailing garbage in ref
	warning: refs/tags/tag-garbage-2: trailingRefContent: trailing garbage in ref
	warning: refs/tags/tag-garbage-3: trailingRefContent: trailing garbage in ref
	warning: refs/tags/tag-garbage-4: trailingRefContent: trailing garbage in ref
	EOF
	sort err >sorted_err &&
	test_cmp expect sorted_err
'

test_expect_success 'textual symref content should be checked (individual)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	printf "ref: refs/heads/branch\n" >$branch_dir_prefix/branch-good &&
	git refs verify 2>err &&
	rm $branch_dir_prefix/branch-good &&
	test_must_be_empty err &&

	printf "ref: refs/heads/branch" >$branch_dir_prefix/branch-no-newline-1 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-no-newline-1: refMissingNewline: missing newline
	EOF
	rm $branch_dir_prefix/branch-no-newline-1 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch     " >$branch_dir_prefix/a/b/branch-trailing-1 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-trailing-1: refMissingNewline: missing newline
	warning: refs/heads/a/b/branch-trailing-1: trailingRefContent: trailing garbage in ref
	EOF
	rm $branch_dir_prefix/a/b/branch-trailing-1 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch\n\n" >$branch_dir_prefix/a/b/branch-trailing-2 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-trailing-2: trailingRefContent: trailing garbage in ref
	EOF
	rm $branch_dir_prefix/a/b/branch-trailing-2 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch \n" >$branch_dir_prefix/a/b/branch-trailing-3 &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-trailing-3: trailingRefContent: trailing garbage in ref
	EOF
	rm $branch_dir_prefix/a/b/branch-trailing-3 &&
	test_cmp expect err &&

	printf "ref: refs/heads/branch \n  " >$branch_dir_prefix/a/b/branch-complicated &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/a/b/branch-complicated: refMissingNewline: missing newline
	warning: refs/heads/a/b/branch-complicated: trailingRefContent: trailing garbage in ref
	EOF
	rm $branch_dir_prefix/a/b/branch-complicated &&
	test_cmp expect err &&

	printf "ref: refs/heads/.branch\n" >$branch_dir_prefix/branch-bad-1 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/branch-bad-1: badReferentName: points to refname with invalid format
	EOF
	rm $branch_dir_prefix/branch-bad-1 &&
	test_cmp expect err &&

	printf "ref: reflogs/heads/main\n" >$branch_dir_prefix/branch-bad-2 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/branch-bad-2: escapeReferent: points to ref outside the refs directory
	EOF
	rm $branch_dir_prefix/branch-bad-2 &&
	test_cmp expect err &&

	printf "ref: refs/heads/a\n" >$branch_dir_prefix/branch-bad-3 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/branch-bad-3: badReferentFiletype: points to the directory
	EOF
	rm $branch_dir_prefix/branch-bad-3 &&
	test_cmp expect err
'

test_expect_success 'textual symref content should be checked (aggregate)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	printf "ref: refs/heads/branch\n" >$branch_dir_prefix/branch-good &&
	printf "ref: refs/heads/branch" >$branch_dir_prefix/branch-no-newline-1 &&
	printf "ref: refs/heads/branch     " >$branch_dir_prefix/a/b/branch-trailing-1 &&
	printf "ref: refs/heads/branch\n\n" >$branch_dir_prefix/a/b/branch-trailing-2 &&
	printf "ref: refs/heads/branch \n" >$branch_dir_prefix/a/b/branch-trailing-3 &&
	printf "ref: refs/heads/branch \n  " >$branch_dir_prefix/a/b/branch-complicated &&
	printf "ref: refs/heads/.branch\n" >$branch_dir_prefix/branch-bad-1 &&
	printf "ref: reflogs/heads/main\n" >$branch_dir_prefix/branch-bad-2 &&
	printf "ref: refs/heads/a\n" >$branch_dir_prefix/branch-bad-3 &&

	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/branch-bad-1: badReferentName: points to refname with invalid format
	error: refs/heads/branch-bad-2: escapeReferent: points to ref outside the refs directory
	error: refs/heads/branch-bad-3: badReferentFiletype: points to the directory
	warning: refs/heads/a/b/branch-complicated: refMissingNewline: missing newline
	warning: refs/heads/a/b/branch-complicated: trailingRefContent: trailing garbage in ref
	warning: refs/heads/a/b/branch-trailing-1: refMissingNewline: missing newline
	warning: refs/heads/a/b/branch-trailing-1: trailingRefContent: trailing garbage in ref
	warning: refs/heads/a/b/branch-trailing-2: trailingRefContent: trailing garbage in ref
	warning: refs/heads/a/b/branch-trailing-3: trailingRefContent: trailing garbage in ref
	warning: refs/heads/branch-no-newline-1: refMissingNewline: missing newline
	EOF
	sort err >sorted_err &&
	test_cmp expect sorted_err
'

test_expect_success SYMLINKS 'symlink symref content should be checked (individual)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	ln -sf ./main $branch_dir_prefix/branch-symbolic-good &&
	git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-good: symlinkRef: use deprecated symbolic link for symref
	EOF
	rm $branch_dir_prefix/branch-symbolic-good &&
	test_cmp expect err &&

	ln -sf ../../../../branch $branch_dir_prefix/branch-symbolic-1 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-1: symlinkRef: use deprecated symbolic link for symref
	error: refs/heads/branch-symbolic-1: escapeReferent: point to target outside gitdir
	EOF
	rm $branch_dir_prefix/branch-symbolic-1 &&
	test_cmp expect err &&

	ln -sf ../../logs/branch-bad $branch_dir_prefix/branch-symbolic-2 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-2: symlinkRef: use deprecated symbolic link for symref
	error: refs/heads/branch-symbolic-2: escapeReferent: points to ref outside the refs directory
	EOF
	rm $branch_dir_prefix/branch-symbolic-2 &&
	test_cmp expect err &&

	ln -sf ./"branch   space" $branch_dir_prefix/branch-symbolic-3 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/heads/branch-symbolic-3: symlinkRef: use deprecated symbolic link for symref
	error: refs/heads/branch-symbolic-3: badReferentName: points to refname with invalid format
	EOF
	rm $branch_dir_prefix/branch-symbolic-3 &&
	test_cmp expect err &&

	ln -sf ./".tag" $tag_dir_prefix/tag-symbolic-1 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/tags/tag-symbolic-1: symlinkRef: use deprecated symbolic link for symref
	error: refs/tags/tag-symbolic-1: badReferentName: points to refname with invalid format
	EOF
	rm $tag_dir_prefix/tag-symbolic-1 &&
	test_cmp expect err &&

	ln -sf ./ $tag_dir_prefix/tag-symbolic-2 &&
	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	warning: refs/tags/tag-symbolic-2: symlinkRef: use deprecated symbolic link for symref
	error: refs/tags/tag-symbolic-2: badReferentFiletype: points to the directory
	EOF
	rm $tag_dir_prefix/tag-symbolic-2 &&
	test_cmp expect err
'

test_expect_success SYMLINKS 'symlink symref content should be checked (aggregate)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	branch_dir_prefix=.git/refs/heads &&
	tag_dir_prefix=.git/refs/tags &&
	cd repo &&
	test_commit default &&
	mkdir -p "$branch_dir_prefix/a/b" &&

	ln -sf ./main $branch_dir_prefix/branch-symbolic-good &&
	ln -sf ../../../../branch $branch_dir_prefix/branch-symbolic-1 &&
	ln -sf ../../logs/branch-bad $branch_dir_prefix/branch-symbolic-2 &&
	ln -sf ./"branch   space" $branch_dir_prefix/branch-symbolic-3 &&
	ln -sf ./".tag" $tag_dir_prefix/tag-symbolic-1 &&
	ln -sf ./ $tag_dir_prefix/tag-symbolic-2 &&

	test_must_fail git refs verify 2>err &&
	cat >expect <<-EOF &&
	error: refs/heads/branch-symbolic-1: escapeReferent: point to target outside gitdir
	error: refs/heads/branch-symbolic-2: escapeReferent: points to ref outside the refs directory
	error: refs/heads/branch-symbolic-3: badReferentName: points to refname with invalid format
	error: refs/tags/tag-symbolic-1: badReferentName: points to refname with invalid format
	error: refs/tags/tag-symbolic-2: badReferentFiletype: points to the directory
	warning: refs/heads/branch-symbolic-1: symlinkRef: use deprecated symbolic link for symref
	warning: refs/heads/branch-symbolic-2: symlinkRef: use deprecated symbolic link for symref
	warning: refs/heads/branch-symbolic-3: symlinkRef: use deprecated symbolic link for symref
	warning: refs/heads/branch-symbolic-good: symlinkRef: use deprecated symbolic link for symref
	warning: refs/tags/tag-symbolic-1: symlinkRef: use deprecated symbolic link for symref
	warning: refs/tags/tag-symbolic-2: symlinkRef: use deprecated symbolic link for symref
	EOF
	sort err >sorted_err &&
	test_cmp expect sorted_err
'

test_done
