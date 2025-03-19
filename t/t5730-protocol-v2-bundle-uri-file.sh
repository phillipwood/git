#!/bin/sh

test_description="Test bundle-uri with protocol v2 and 'file://' transport"

TEST_NO_CREATE_REPO=1

if ! test_lazy_prereq WITH_BREAKING_CHANGES
then
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
	export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
fi

. ./test-lib.sh

# Test protocol v2 with 'file://' transport
#
BUNDLE_URI_PROTOCOL=file
. "$TEST_DIRECTORY"/lib-bundle-uri-protocol.sh

test_done
