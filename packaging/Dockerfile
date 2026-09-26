ARG BUILD_IMAGE=ubuntu:22.04@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02
ARG RUNTIME_IMAGE=ubuntu:22.04@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02
FROM ${BUILD_IMAGE} AS dependencies
ARG DEBIAN_FRONTEND=noninteractive
RUN sed -n 's/^deb /deb-src /p' /etc/apt/sources.list > /etc/apt/sources.list.d/sources.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl cmake ninja-build g++ pkg-config nasm patchelf xz-utils git \
    libcurl4-openssl-dev libssl-dev dpkg-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
RUN curl -fsSL https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz -o ffmpeg.tar.xz \
    && echo '05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41  ffmpeg.tar.xz' | sha256sum -c - \
    && tar -xf ffmpeg.tar.xz
WORKDIR /src/ffmpeg-8.0.1
RUN ./configure --prefix=/opt/media --disable-everything --disable-autodetect \
    --disable-programs --disable-doc --disable-debug --disable-network \
    --disable-static --enable-shared --enable-swresample --enable-protocol=file \
    --enable-demuxer=mov,matroska,wav,mp3,ogg,flac,aac,aiff,avi,asf,mpegps,mpegts \
    --enable-parser=aac,aac_latm,mpegaudio,ac3,opus,vorbis,flac \
    --enable-decoder=aac,aac_latm,mp1,mp2,mp3,flac,opus,vorbis,alac,ac3,eac3,wmav1,wmav2,wmapro,wmalossless,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,pcm_s16be,pcm_s24be,pcm_s32be,pcm_u8,pcm_alaw,pcm_mulaw \
    && make -j4 && make install
RUN mkdir -p /opt/cuda-stubs \
    && ln -s /usr/local/cuda/lib64/stubs/libcuda.so /opt/cuda-stubs/libcuda.so.1
ENV PKG_CONFIG_PATH=/opt/media/lib/pkgconfig LD_LIBRARY_PATH=/opt/media/lib:/opt/cuda-stubs
FROM dependencies AS build
WORKDIR /app
COPY CMakeLists.txt CMakePresets.json LICENSE ./
COPY cmake/ cmake/
COPY src/ src/
COPY tests/ tests/
ARG WT_CUDA=OFF
ARG CUDA_ARCHITECTURES=75;80;86;89;90
RUN cmake -S . -B .build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWT_CUDA=${WT_CUDA} \
    -DGGML_NATIVE=OFF -DWT_WERROR=ON \
    -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES}" \
    && cmake --build .build/release -j4 \
    && ctest --test-dir .build/release --output-on-failure
COPY packaging/ packaging/
RUN . .build/release/generated/package.env \
    && apt-get update && bash packaging/package.sh .build/release /bundle \
    && cp /src/ffmpeg.tar.xz /bundle/sources/ffmpeg-8.0.1.tar.xz \
    && cp /src/ffmpeg-8.0.1/COPYING.LGPLv2.1 /bundle/licenses/FFmpeg-LGPL-2.1.txt \
    && cp /src/ffmpeg-8.0.1/ffbuild/config.log /bundle/sources/ffmpeg-config.log \
    && tar -C .build/release/_deps/whisper-src -czf "/bundle/sources/whisper.cpp-${WT_WHISPER_REVISION}.tar.gz" . \
    && cp .build/release/_deps/whisper-src/LICENSE /bundle/licenses/whisper.cpp-MIT.txt \
    && cp .build/release/_deps/nlohmann_json-src/LICENSE.MIT /bundle/licenses/nlohmann-json-MIT.txt \
    && tar -C .build/release/_deps/nlohmann_json-src -czf "/bundle/sources/nlohmann-json-${WT_JSON_VERSION}.tar.gz" . \
    && tar -C .build/release/_deps/cli11-src -czf "/bundle/sources/CLI11-${WT_CLI11_VERSION}.tar.gz" . \
    && cp .build/release/_deps/cli11-src/LICENSE /bundle/licenses/CLI11-BSD.txt \
    && cp LICENSE /bundle/licenses/whisper-transcribator-MIT.txt \
    && tar -czf "/bundle/sources/whisper-transcribator-${WT_PACKAGE_VERSION}.tar.gz" CMakeLists.txt CMakePresets.json cmake src tests packaging LICENSE
RUN . .build/release/generated/package.env && mkdir -p /export \
    && if [ "${WT_CUDA}" = ON ]; then flavor=cuda; else flavor=cpu; fi \
    && tar -C /bundle -czf "/export/whisper-transcribator-${WT_PACKAGE_VERSION}-linux-x86_64-${flavor}.tar.gz" . \
    && cd /export && sha256sum *.tar.gz > SHA256SUMS
FROM scratch AS archive
COPY --from=build /export/ /
FROM ${RUNTIME_IMAGE} AS runtime
COPY --from=build /bundle/ /opt/whisper-transcribator/
RUN groupadd --gid 10001 whisper && useradd --uid 10001 --gid 10001 --no-create-home whisper \
    && mkdir -p /models /data && chown 10001:10001 /models /data
ENV WHISPER_DOWNLOAD_ROOT=/models NVIDIA_VISIBLE_DEVICES=all NVIDIA_DRIVER_CAPABILITIES=compute,utility
USER 10001:10001
WORKDIR /data
ENTRYPOINT ["/opt/whisper-transcribator/bin/whisper-transcribator"]
CMD ["--help"]
