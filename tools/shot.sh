#!/bin/sh
# Renders one screen of the MIPS build under qemu: tools/shot.sh out.bmp --screen game --stage 8 ...
# (qemu runs on the host, so go through flatpak-spawn when inside the sandbox)
DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1; shift
SR=/home/kryptik/handhelds/sf3000-work/buildroot/output/host/mipsel-buildroot-linux-gnu/sysroot
FW=/home/kryptik/handhelds/Development/Datafrog/SF3500/_readonly/firmware/base/cubegm/usr/lib
RUN="cd $DIR/build/gravereckoning && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH=$DIR/build/gravereckoning:$FW:$SR/usr/lib timeout 60 qemu-mipsel-static -L $SR ./gravereckoning.mipsel --screenshot $OUT $*"
if command -v flatpak-spawn >/dev/null 2>&1; then flatpak-spawn --host sh -c "$RUN"; else sh -c "$RUN"; fi
