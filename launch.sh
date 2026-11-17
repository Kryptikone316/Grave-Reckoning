#!/bin/sh
set -u

cd "$(dirname "$0")"
export LD_LIBRARY_PATH="$(pwd):/mnt/sdcard/cubegm/lib:/mnt/sdcard/cubegm/usr/lib:/usr/lib:${LD_LIBRARY_PATH:-}"
rc=0
./gravereckoning.mipsel > log.txt 2>&1 || rc=$?
echo "EXIT CODE: $rc" >> log.txt
exit "$rc"
