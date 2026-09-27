#!/usr/bin/env bash
# Download, verify and build the decoding-only LGPL FFmpeg of every release archive:
#   ffmpeg.sh WORK_DIRECTORY PREFIX [EXTRA_CONFIGURE_ARGUMENT...]
# The source tarball and build tree stay in WORK_DIRECTORY for the archive's sources/ directory.
set -euo pipefail
version=8.0.1
checksum=05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41
work=$1
prefix=$2
shift 2
mkdir -p "$work"
cd "$work"
curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" -o ffmpeg.tar.xz
if command -v sha256sum >/dev/null; then
    printf '%s  ffmpeg.tar.xz\n' "$checksum" | sha256sum -c -
else
    printf '%s  ffmpeg.tar.xz\n' "$checksum" | shasum -a 256 -c -
fi
rm -rf "ffmpeg-$version"
tar -xf ffmpeg.tar.xz
cd "ffmpeg-$version"
./configure --prefix="$prefix" --disable-everything --disable-autodetect \
    --disable-programs --disable-doc --disable-debug --disable-network \
    --disable-static --enable-shared --enable-swresample --enable-protocol=file \
    --enable-demuxer=mov,matroska,wav,mp3,ogg,flac,aac,aiff,avi,asf,mpegps,mpegts \
    --enable-parser=aac,aac_latm,mpegaudio,ac3,opus,vorbis,flac \
    --enable-decoder=aac,aac_latm,mp1,mp2,mp3,flac,opus,vorbis,alac,ac3,eac3,wmav1,wmav2,wmapro,wmalossless,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,pcm_s16be,pcm_s24be,pcm_s32be,pcm_u8,pcm_alaw,pcm_mulaw \
    "$@"
make -j"$(getconf _NPROCESSORS_ONLN)"
make install
