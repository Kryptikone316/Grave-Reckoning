Checks the music player's group switching and fades without the device: builds the real
src/music.c against a stub audio device (stub_hwaudio.c) that logs the loudest sample per
quarter second, then runs it under qemu. Build with the same cross compiler as
build_mips.sh:  $CC ... -Isrc -o musictest main.c stub_hwaudio.c ../../src/music.c
  -lvorbisidec -logg -lpthread -lm
Run from a folder that contains Music/:  qemu-mipsel-static -L <sysroot> ./musictest Music
