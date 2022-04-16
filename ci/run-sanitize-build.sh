#!/bin/sh
#
# Build and test Git inside container
#
# Usage:
#   run-docker-build.sh <host-user-id>
#

set -ex

if test $# -ne 1 || test -z "$1"
then
	echo >&2 "usage: run-docker-build.sh (--build|--test)"
	exit 1
fi

. "${0%/*}/lib.sh"

# If this script runs inside a docker container, then all commands are
# usually executed as root. Consequently, the host user might not be
# able to access the test output files.
# If a non 0 host user id is given, then create a user "ci" with that
# user id to make everything accessible to the host user.
HOST_UID=1001
CI_USER=ci
if test "$1" = "--build"
then
	if test "$(id -u $CI_USER 2>/dev/null)" = $HOST_UID
	then
		echo "user '$CI_USER' already exists with the requested ID $HOST_UID"
	else
		adduser -u $HOST_UID $CI_USER
	fi
fi

# sed re to make export statement
make_export="s/^\([^=]*\)=\(.*\)/export \1='\2'/p"

# Build and test
command $switch_cmd su -l $CI_USER -c "
	set -ex
	$(env | sed -n -e "\
		s/'/'\\\\''/g
		/^GITHUB_/$make_export
		/^jobname/$make_export")
	cd '$PWD'
	'${0%/*}/run-build-and-tests.sh' $1
"
