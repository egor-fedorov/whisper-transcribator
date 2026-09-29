# Clean runtime verification of the exact exported archive, without rebuilding it.
FROM ubuntu:22.04@sha256:b8b6ee6aa931ecd9d0d952abc34dc0e5f7c6a30c6bb71b079fe399fde0329c02
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl jq \
    && rm -rf /var/lib/apt/lists/*
COPY bundle/ /opt/whisper-transcribator/
COPY tools/ /tools/
COPY checks/ /checks/
ENV LD_LIBRARY_PATH=/opt/whisper-transcribator/lib \
    NVIDIA_VISIBLE_DEVICES=all NVIDIA_DRIVER_CAPABILITIES=compute,utility \
    WT_PROCESS_RUNNER=/tools/wt-process-runner HOME=/tmp
USER 10001:10001
WORKDIR /tmp
