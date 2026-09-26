FROM python:3.13-slim-bookworm@sha256:2325bb286ec344af3e5898cc224b5844e2707ac6e26b1632516fd3edc84a5e26 AS python

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    WHISPER_MODEL=small \
    WHISPER_DOWNLOAD_ROOT=/models

FROM python AS base
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libgomp1 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /models

WORKDIR /app

COPY requirements.lock ./
RUN pip install -r requirements.lock

ENTRYPOINT ["python", "-m", "whisper_transcribator"]

FROM python AS cuda-libraries
RUN pip install --target=/opt/cuda nvidia-cublas-cu12==12.8.4.1 nvidia-cudnn-cu12==9.7.1.26

FROM base AS package
COPY pyproject.toml README.md LICENSE ./
COPY whisper_transcribator ./whisper_transcribator
RUN pip wheel --no-deps --no-build-isolation --wheel-dir /wheels .

FROM base AS cuda
COPY --from=cuda-libraries /opt/cuda /opt/cuda
ENV PYTHONPATH=/opt/cuda \
    LD_LIBRARY_PATH=/opt/cuda/nvidia/cublas/lib:/opt/cuda/nvidia/cudnn/lib
COPY --from=package /wheels /wheels
RUN pip install --no-deps /wheels/*.whl && rm -rf /wheels && pip check

FROM base AS cpu
COPY --from=package /wheels /wheels
RUN pip install --no-deps /wheels/*.whl && rm -rf /wheels && pip check
