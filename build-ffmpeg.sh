#!/bin/sh
# Builds the FFmpeg 2.4.14 libraries ArchiveTV ships in App/ArchiveTV/libs (LGPL 2.1+, no GPL/nonfree parts).
# Needs the Miyoo Mini toolchain: https://github.com/shauninman/miyoomini-toolchain-buildroot/releases
#   sh build-ffmpeg.sh /path/to/miyoomini-toolchain
set -e
TC=${1:?usage: build-ffmpeg.sh /path/to/miyoomini-toolchain}
[ -d ffmpeg-src ] || git clone --depth 1 --branch n2.4.14 https://github.com/FFmpeg/FFmpeg.git ffmpeg-src
cd ffmpeg-src
./configure --prefix="$PWD/../ffout" --enable-cross-compile --cross-prefix="$TC/bin/arm-linux-gnueabihf-" \
  --arch=arm --cpu=cortex-a7 --target-os=linux --enable-shared --disable-static --disable-programs --disable-doc \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc --disable-avresample --disable-network \
  --disable-debug --disable-iconv --disable-xlib --disable-everything \
  --enable-decoder=h264,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,mpeg1video,mpeg2video,h263,h263p,theora,vp3,vp6,vp6f,vp8,flv,mjpeg,wmv1,wmv2,wmv3,vc1,aac,aac_latm,mp3,mp3float,mp2,mp1,vorbis,opus,ac3,eac3,flac,alac,pcm_s16le,pcm_s16be,pcm_u8,pcm_s24le,adpcm_ms,adpcm_ima_wav,wmav1,wmav2,cinepak,indeo3,svq1,svq3,rv10,rv20,rv30,rv40,cook,ra_144,ra_288 \
  --enable-demuxer=mov,matroska,avi,ogg,mpegts,mpegps,flv,mp3,aac,wav,asf,mpegvideo,h264,m4v,rm,dv,ac3,flac \
  --enable-parser=h264,mpeg4video,mpegvideo,mpegaudio,aac,aac_latm,vorbis,vp3,vp8,ac3,flac,opus,h263,vc1,cook \
  --enable-protocol=file,pipe --enable-bsf=aac_adtstoasc,h264_mp4toannexb \
  --enable-neon --extra-cflags="-mfpu=neon-vfpv4 -mfloat-abi=hard -O2"
make -j"$(nproc)" && make install
mkdir -p ../App/ArchiveTV/libs
for l in libavcodec.so.56 libavformat.so.56 libavutil.so.54 libswresample.so.1; do cp -L "../ffout/lib/$l" ../App/ArchiveTV/libs/; done
echo "libs ready in App/ArchiveTV/libs"
