FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    WHISPER_MODEL=small \
    WHISPER_DOWNLOAD_ROOT=/models

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libgomp1 \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /models

WORKDIR /app

COPY pyproject.toml README.md ./
COPY whisper_transcribator ./whisper_transcribator

RUN pip install --upgrade pip \
    && pip install .

ENTRYPOINT ["python", "-m", "whisper_transcribator"]
