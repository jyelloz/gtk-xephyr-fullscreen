#!/bin/sh

set -e
set -x

current_directory="${PWD}"
script_directory="$(dirname $0)"

cd "${script_directory}"

autoreconf -v -i -f

cd "${current_directory}"

exec "${script_directory}/configure" "$@"
