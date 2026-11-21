#!/bin/sh
# Cross-compile ArchiveTV for the Miyoo Mini (armhf, glibc) with clang + lld, no sysroot needed.
set -e
cd "$(dirname "$0")"
LIBS=${LIBS:-libs}          # FFmpeg 2.4 .so files: copy them from your SD card's .tmp_update/lib/parasyte
T="--target=armv7a-linux-gnueabihf -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
CF="$T -O2 -ffreestanding -nostdlibinc -fno-stack-protector -fno-pie -fno-pic -Wall -Wno-unused-function"
mkdir -p build
clang $CF -c src/atv.c -o build/atv.o
clang $CF -c src/rt.c -o build/rt.o
clang $CF -c src/ui.c -o build/ui.o
# stub libraries for the system libs (resolved on the device at run time)
llvm-nm -u build/atv.o build/rt.o build/ui.o | awk 'NF==2{print $2} NF==1{print $1}' | grep -v ':' | sort -u > build/undef.txt
for l in $LIBS/libavcodec.so.56 $LIBS/libavformat.so.56 $LIBS/libavutil.so.54 $LIBS/libswresample.so.1; do llvm-nm -D --defined-only "$l" | awk '{print $3}' | sed 's/@.*//'; done | sort -u > build/ffdef.txt
llvm-nm --defined-only build/rt.o build/atv.o build/ui.o | awk 'NF==3{print $3}' | sort -u > build/rtdef.txt
comm -23 build/undef.txt build/ffdef.txt | comm -23 - build/rtdef.txt > build/sys.txt
grep '^SDL_' build/sys.txt | sed 's/.*/void &(void){}/' > build/sdl_stub.c
grep '^pthread_' build/sys.txt | sed 's/.*/void &(void){}/' > build/pth_stub.c
grep -v -e '^SDL_' -e '^pthread_' build/sys.txt | sed 's/.*/void &(void){}/' > build/c_stub.c
SO="$T -w -fno-builtin -shared -nostdlib -fuse-ld=lld -fPIC"
clang $SO -Wl,-soname,libSDL-1.2.so.0 -o build/libSDL-1.2.so.0 build/sdl_stub.c
clang $SO -Wl,-soname,libpthread.so.0 -o build/libpthread.so.0 build/pth_stub.c
clang $SO -Wl,-soname,libc.so.6 -o build/libc.so.6 build/c_stub.c
clang $T -nostdlib -fuse-ld=lld -no-pie -Wl,--no-pie -Wl,-dynamic-linker,/lib/ld-linux-armhf.so.3 \
  -Wl,--hash-style=both -Wl,--allow-shlib-undefined -Wl,-z,noexecstack -Wl,--as-needed -o build/archivetv \
  build/rt.o build/atv.o build/ui.o \
  $LIBS/libavformat.so.56 $LIBS/libavcodec.so.56 $LIBS/libswresample.so.1 $LIBS/libavutil.so.54 \
  build/libSDL-1.2.so.0 build/libpthread.so.0 build/libc.so.6
llvm-strip build/archivetv
echo built build/archivetv
