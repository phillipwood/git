#!/bin/sh

. "${0%/*}/lib.sh"

#Install dependencies for sanitizer jobs
dnf -yq update >/dev/null &&
dnf -yq install make git findutils diffutils perl python3 gettext zlib-devel \
		expat-devel openssl-devel curl-devel pcre2-devel httpd \
		$PACKAGES >/dev/null
