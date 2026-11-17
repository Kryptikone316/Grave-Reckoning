#!/bin/bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT:-/home/kryptik/handhelds/sf3000-work/buildroot/output/host}
TC=${TC:-$TOOLCHAIN_ROOT/bin}
if [ ! -x "$TC/mips-mti-linux-gnu-gcc" ]; then
    TC="$TOOLCHAIN_ROOT/opt/ext-toolchain/bin"
fi

CC=${CC:-$TC/mips-mti-linux-gnu-gcc}
CXX=${CXX:-$TC/mips-mti-linux-gnu-g++}
READELF=${READELF:-$TC/mips-mti-linux-gnu-readelf}
SYSROOT=${SYSROOT:-$($CC -EL -print-sysroot)}
SYSROOT_BASE=${SYSROOT_BASE:-$(dirname "$SYSROOT")}
SYSROOT_BASE=$(cd "$SYSROOT_BASE" && pwd)
SDL_SYSROOT=${SDL_SYSROOT:-$TOOLCHAIN_ROOT/mipsel-buildroot-linux-gnu/sysroot}
SDL_FW=${SDL_FW:-/home/kryptik/handhelds/Development/Datafrog/SF3500/_readonly/firmware/base/cubegm/usr/lib}
OUT="$ROOT/build/gravereckoning"
OBJ="$ROOT/build/obj/gravereckoning"

mkdir -p "$OUT" "$OBJ"
COMMON="-mips32r2 -mtune=74kc -mhard-float -EL -O2 -Wall -Wextra -fno-pie -no-pie --sysroot=$SYSROOT_BASE"
INCLUDES="-I$ROOT/src -I$SDL_SYSROOT/usr/include"

"$CC" $COMMON $INCLUDES -c "$ROOT/src/hwdisp.c" -o "$OBJ/hwdisp.o"
"$CC" $COMMON $INCLUDES -c "$ROOT/src/joykey.c" -o "$OBJ/joykey.o"
"$CC" $COMMON $INCLUDES -c "$ROOT/src/hwaudio.c" -o "$OBJ/hwaudio.o"
"$CC" $COMMON $INCLUDES -c "$ROOT/src/music.c" -o "$OBJ/music.o"
"$CC" $COMMON $INCLUDES -c "$ROOT/src/sfxmix.c" -o "$OBJ/sfxmix.o"
"$CC" $COMMON $INCLUDES -c "$ROOT/src/sfx.c" -o "$OBJ/sfx.o"
"$CXX" $COMMON $INCLUDES -std=c++11 -c "$ROOT/src/gravereckoning.cpp" -o "$OBJ/gravereckoning.o"
"$CXX" $COMMON "$OBJ/gravereckoning.o" "$OBJ/hwdisp.o" "$OBJ/joykey.o" \
    "$OBJ/hwaudio.o" "$OBJ/music.o" "$OBJ/sfxmix.o" "$OBJ/sfx.o" \
    "$SDL_FW/libSDL2_ttf.so" "$SDL_SYSROOT/usr/lib/libSDL2_image.so" \
    "$SDL_SYSROOT/usr/lib/libSDL2.so" -lvorbisidec -logg -lpthread -ldl -lrt -lm \
    -o "$OUT/gravereckoning.mipsel"

cp -a "$ROOT/assets/Art" "$OUT/"
cp -a "$ROOT/assets/Arenas" "$OUT/"
cp -a "$ROOT/assets/Fonts" "$OUT/"
cp -a "$ROOT/assets/Story" "$OUT/"
cp -a "$ROOT/assets/Title" "$OUT/"
cp "$ROOT/assets/boxart.png" "$OUT/"
cp -a "$ROOT/assets/Music" "$OUT/"
cp -a "$ROOT/assets/Sfx" "$OUT/"
cp "$ROOT/assets/Vera.ttf" "$OUT/"
cp "$ROOT/assets/Vera-COPYRIGHT.TXT" "$OUT/"
cp -L "$TOOLCHAIN_ROOT/mipsel-buildroot-linux-gnu/sysroot/usr/lib/libogg.so.0.8.4" "$OUT/libogg.so.0"
cp -L "$TOOLCHAIN_ROOT/mipsel-buildroot-linux-gnu/sysroot/usr/lib/libvorbisidec.so.1.0.3" "$OUT/libvorbisidec.so.1"
cp "$ROOT/launch.sh" "$OUT/gravereckoning.sh"
cp "$ROOT/Grave Reckoning.sh" "$ROOT/build/Grave Reckoning.sh"
chmod 755 "$OUT/gravereckoning.sh" "$OUT/gravereckoning.mipsel"
chmod 755 "$ROOT/build/Grave Reckoning.sh"

echo "Built $OUT/gravereckoning.mipsel"
"$READELF" -h "$OUT/gravereckoning.mipsel" | rg 'Class:|Data:|Machine:'
"$READELF" -d "$OUT/gravereckoning.mipsel" | rg 'NEEDED|RPATH|RUNPATH' || true
