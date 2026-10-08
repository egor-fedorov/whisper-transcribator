ARG BUILD_IMAGE=ubuntu:22.04@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02
ARG RUNTIME_IMAGE=ubuntu:22.04@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02
FROM ${BUILD_IMAGE} AS dependencies
ARG DEBIAN_FRONTEND=noninteractive
# GCC 11 cannot compile ggml's armv9.2 (SME) CPU variants; arm64 uses Ubuntu's clang-15.
RUN sed -n 's/^deb /deb-src /p' /etc/apt/sources.list > /etc/apt/sources.list.d/sources.list \
    && apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl cmake ninja-build g++ pkg-config nasm patchelf xz-utils git \
    libcurl4-openssl-dev libssl-dev dpkg-dev \
    && if [ "$(dpkg --print-architecture)" = arm64 ]; then \
        apt-get install -y --no-install-recommends clang-15 libstdc++-12-dev; fi \
    && rm -rf /var/lib/apt/lists/*
# The same decoding-only FFmpeg as the macOS archive; the script lists the enabled formats.
COPY packaging/ffmpeg.sh /src/ffmpeg.sh
RUN bash /src/ffmpeg.sh /src /opt/media
ARG WT_VULKAN=OFF
COPY packaging/vulkan-sdk.json /src/vulkan-sdk.json
COPY packaging/linux/vulkan-sdk.sh /src/linux/vulkan-sdk.sh
RUN if [ "${WT_VULKAN}" = ON ]; then \
        test "$(dpkg --print-architecture)" = amd64 \
        && apt-get update && apt-get install -y --no-install-recommends libvulkan-dev jq \
        && bash /src/linux/vulkan-sdk.sh /opt/vulkan; fi
RUN mkdir -p /opt/cuda-stubs \
    && ln -s /usr/local/cuda/lib64/stubs/libcuda.so /opt/cuda-stubs/libcuda.so.1
ENV PKG_CONFIG_PATH=/opt/media/lib/pkgconfig LD_LIBRARY_PATH=/opt/media/lib:/opt/cuda-stubs
FROM dependencies AS build
WORKDIR /app
COPY CMakeLists.txt CMakePresets.json vcpkg.json .gitattributes LICENSE ./
COPY cmake/ cmake/
COPY src/ src/
COPY tests/ tests/
ARG WT_CUDA=OFF
ARG CUDA_ARCHITECTURES=75;80;86;89;90
ARG WT_SOURCE_REVISION=unknown
ARG WT_SOURCE_DIRTY=unknown
ARG BUILD_JOBS=4
# Clang's OpenMP runtime would add libomp and the LLVM source package to the bundle;
# arm64 uses ggml's own thread pool instead.
RUN if [ "$(dpkg --print-architecture)" = arm64 ]; then \
        if [ "${WT_CUDA}" = ON ]; then echo 'CUDA archives are x86_64-only' >&2; exit 1; fi; \
        export CC=clang-15 CXX=clang++-15; set -- -DGGML_OPENMP=OFF; \
    else set --; fi \
    && if [ "${WT_VULKAN}" = ON ]; then \
        test "${WT_CUDA}" = OFF && export VULKAN_SDK=/opt/vulkan/sdk \
        && set -- "$@" -DVulkan_INCLUDE_DIR=/opt/vulkan/sdk/include \
            -DVulkan_GLSLC_EXECUTABLE=/opt/vulkan/sdk/bin/glslc \
            -DVulkan_LIBRARY=/usr/lib/x86_64-linux-gnu/libvulkan.so; fi \
    && cmake -S . -B .build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWT_CUDA=${WT_CUDA} -DWT_VULKAN=${WT_VULKAN} \
    -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON -DWT_WERROR=ON \
    -DCMAKE_CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES}" \
    -DWT_SOURCE_REVISION="${WT_SOURCE_REVISION}" -DWT_SOURCE_DIRTY="${WT_SOURCE_DIRTY}" "$@" \
    && cmake --build .build/release --parallel "${BUILD_JOBS}" \
    && ctest --test-dir .build/release --output-on-failure
COPY packaging/ packaging/
RUN . .build/release/generated/package.env \
    && apt-get update && bash packaging/package.sh .build/release /bundle \
    && cp /src/ffmpeg.tar.xz /bundle/sources/ffmpeg-8.0.1.tar.xz \
    && cp /src/ffmpeg-8.0.1/COPYING.LGPLv2.1 /bundle/licenses/FFmpeg-LGPL-2.1.txt \
    && cp /src/ffmpeg-8.0.1/ffbuild/config.log /bundle/sources/ffmpeg-config.log \
    && if [ "${WT_VULKAN}" = ON ]; then set -- -DWT_VULKAN_SDK=/opt/vulkan/sdk; else set --; fi \
    && cmake -DWT_SOURCE_DIR=/app -DWT_BUILD_DIR=/app/.build/release -DWT_BUNDLE_DIR=/bundle "$@" \
        -P packaging/cmake/sources.cmake
RUN . .build/release/generated/package.env && mkdir -p /export \
    && tar -C /bundle -czf "/export/whisper-transcribator-${WT_PACKAGE_VERSION}-linux-${WT_TARGET_ARCH}-${WT_PACKAGE_FLAVOR}.tar.gz" . \
    && cd /export && sha256sum *.tar.gz > SHA256SUMS
FROM scratch AS archive
COPY --from=build /export/ /
FROM scratch AS cuda-release
# Test helpers travel separately; they are never part of the release archive.
COPY --from=build /export/ /cuda/
COPY --from=build /app/.build/release/tests/wt-audio-fixture /tools/wt-audio-fixture
COPY --from=build /app/.build/release/tests/wt-process-runner /tools/wt-process-runner
FROM scratch AS vulkan-release
COPY --from=build /export/ /vulkan/
COPY --from=build /app/.build/release/tests/wt-audio-fixture /tools/wt-audio-fixture
COPY --from=build /app/.build/release/tests/wt-process-runner /tools/wt-process-runner
FROM ${RUNTIME_IMAGE} AS runtime-base
RUN groupadd --gid 10001 whisper && useradd --uid 10001 --gid 10001 --no-create-home whisper \
    && mkdir -p /models /data && chown 10001:10001 /models /data
ENV WHISPER_DOWNLOAD_ROOT=/models NVIDIA_VISIBLE_DEVICES=all NVIDIA_DRIVER_CAPABILITIES=compute,utility
USER 10001:10001
WORKDIR /data
ENTRYPOINT ["/opt/whisper-transcribator/bin/whisper-transcribator"]
CMD ["--help"]
FROM runtime-base AS runtime-bundle
# The build context is a verified unpacked archive, not the source checkout.
COPY . /opt/whisper-transcribator/
FROM runtime-base AS runtime
COPY --from=build /bundle/ /opt/whisper-transcribator/
