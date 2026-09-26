ARG BUILD_IMAGE=nvidia/cuda:12.8.1-devel-ubuntu22.04@sha256:a99a1860ba8e2916e5c3e73b72ec4c4301653a84586e05bfc9a2aa2d58027e97
FROM ${BUILD_IMAGE} AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl cmake make g++ pkg-config nasm patchelf xz-utils git \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
RUN curl -fsSL https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz -o ffmpeg.tar.xz \
    && echo '05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41  ffmpeg.tar.xz' | sha256sum -c - \
    && tar -xf ffmpeg.tar.xz
WORKDIR /src/ffmpeg-8.0.1
RUN ./configure --prefix=/opt/media --disable-everything --disable-autodetect \
    --disable-programs --disable-doc --disable-debug --disable-network \
    --disable-static --enable-shared --enable-swresample --enable-protocol=file \
    --enable-demuxer=mov,matroska,wav,mp3,ogg,flac,aac,aiff \
    --enable-decoder=aac,mp3,flac,opus,vorbis,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_s16be \
    && make -j4 && make install
RUN mkdir -p /opt/cuda-stubs \
    && ln -s /usr/local/cuda/lib64/stubs/libcuda.so /opt/cuda-stubs/libcuda.so.1
ENV PKG_CONFIG_PATH=/opt/media/lib/pkgconfig LD_LIBRARY_PATH=/opt/media/lib:/opt/cuda-stubs
WORKDIR /app
COPY experiments/whisper_cpp/CMakeLists.txt experiments/whisper_cpp/main.cpp experiments/whisper_cpp/audio.cpp ./
ARG NATIVE_CUDA=OFF
ARG CUDA_ARCHITECTURES=75;80;86;89;90
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DNATIVE_CUDA=${NATIVE_CUDA} \
    -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES}" \
    && cmake --build /build -j4
RUN ctest --test-dir /build --output-on-failure
COPY experiments/whisper_cpp/package.sh ./
COPY LICENSE ./LICENSE
RUN bash package.sh /build /bundle \
    && mkdir -p /bundle/sources /bundle/licenses \
    && cp /src/ffmpeg.tar.xz /bundle/sources/ffmpeg-8.0.1.tar.xz \
    && cp /src/ffmpeg-8.0.1/COPYING.LGPLv2.1 /bundle/licenses/FFmpeg-LGPL-2.1.txt \
    && cp /src/ffmpeg-8.0.1/ffbuild/config.log /bundle/sources/ffmpeg-config.log \
    && tar -C /build/_deps/whisper-src -czf /bundle/sources/whisper.cpp-927cfce.tar.gz . \
    && cp /build/_deps/whisper-src/LICENSE /bundle/licenses/whisper.cpp-MIT.txt \
    && cp /build/_deps/whisper-src/examples/json.hpp /bundle/licenses/json-header-with-license.hpp \
    && cp -aL /usr/share/doc/libstdc++6 /usr/share/doc/libgcc-s1 /usr/share/doc/libgomp1 /bundle/licenses/ \
    && cp LICENSE /bundle/licenses/whisper-transcribator-MIT.txt \
    && tar -czf /bundle/sources/whisper-transcribator-native.tar.gz CMakeLists.txt main.cpp audio.cpp package.sh LICENSE \
    && if [ "${NATIVE_CUDA}" = ON ]; then \
         for directory in /usr/share/doc/libcublas-* /usr/share/doc/cuda-cudart-*; do \
           [ ! -d "$directory" ] || cp -aL "$directory" /bundle/licenses/; \
         done; \
       fi
RUN mkdir -p /export && tar -C /bundle -czf /export/whisper-transcribator-native-linux-x86_64.tar.gz .
FROM scratch AS archive
COPY --from=build /export/ /
