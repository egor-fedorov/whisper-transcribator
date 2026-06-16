# whisper_transcribator

CLI-утилита для транскрибации медиафайлов через `faster-whisper`. Неважно, видео это или аудио: для нас важно только то, что входной файл может быть декодирован библиотекой PyAV, которую использует `faster-whisper`.

## Что умеет

- принимает любой медиафайл, который может декодировать `faster-whisper`
- сохраняет результат в `.txt`, `.srt` или `.json`
- работает с именами моделей (`small`, `medium`, `large-v3`, `turbo`, `distil-large-v3`) или с локальной директорией уже конвертированной модели
- умеет автоматически скачивать named-модели в локальный cache
- умеет batch-режим, VAD и word-level timestamps

## Быстрый старт через Docker

Собрать образ:

```bash
docker build -t whisper-transcribator .
```

Транскрибировать файл из текущей директории:

```bash
docker run --rm \
  -v "$PWD:/work" \
  -v whisper-models:/models \
  whisper-transcribator \
  /work/lecture.mkv \
  -o /work/lecture.txt \
  --model small \
  --language ru
```

С таймкодами в `srt`:

```bash
docker run --rm \
  -v "$PWD:/work" \
  -v whisper-models:/models \
  whisper-transcribator \
  /work/lecture.wav \
  -o /work/lecture.srt \
  --model medium \
  --format srt
```

Если файлов несколько, удобнее писать в каталог:

```bash
docker run --rm \
  -v "$PWD:/work" \
  -v whisper-models:/models \
  whisper-transcribator \
  /work/lecture1.mp3 /work/lecture2.m4a \
  --output-dir /work/transcripts \
  --model small
```

## Работа с моделями

Показать поддерживаемые модели и текущий cache status:

```bash
docker run --rm \
  -v whisper-models:/models \
  whisper-transcribator \
  --list-models
```

Как это работает:

- если `--model small`, `faster-whisper`-модель будет при первом запуске скачана в `${WHISPER_DOWNLOAD_ROOT}/small`
- в Docker-образе `WHISPER_DOWNLOAD_ROOT=/models`, поэтому достаточно примонтировать volume `whisper-models:/models`
- если передать путь в `--model`, например `/models/custom-large-v3-ct2`, CLI использует локальную модель без скачивания
- если нужен строго offline-режим, используй `--local-files-only`

Примеры:

```bash
docker run --rm \
  -v "$PWD:/work" \
  -v whisper-models:/models \
  whisper-transcribator \
  /work/lecture.flac \
  --model large-v3
```

```bash
docker run --rm \
  -v "$PWD:/work" \
  -v "$PWD/models:/ext-models" \
  whisper-transcribator \
  /work/lecture.ogg \
  --model /ext-models/my-whisper-ct2
```

## Аргументы CLI

```bash
docker run --rm whisper-transcribator --help
```

Основные флаги:

- `-o, --output` — путь к выходному файлу для одного input
- `--output-dir` — директория для batch-режима
- `--format` — `text`, `srt` или `json`
- `--model` — имя faster-whisper модели, HF repo id или путь к локальной converted model directory
- `--download-root` — куда скачивать named-модели
- `--local-files-only` — не ходить в сеть за моделями
- `--device` — `cpu`, `cuda` или `auto`
- `--compute-type` — например `int8`, `float16`, `int8_float16`
- `--batch-size` — если `>1`, включает `BatchedInferencePipeline`
- `--beam-size` — beam size декодирования
- `--word-timestamps` — добавляет word-level timestamps в JSON и segment data
- `--no-vad` — отключает VAD
- `--vad-min-silence-ms` — настраивает `min_silence_duration_ms`
- `--list-models` — показывает поддерживаемые модели и их cache status

## Практические замечания

- По умолчанию CLI использует модель `small`.
- Для CPU по умолчанию выбирается `compute_type=int8`, для CUDA — `float16`.
- Для русских лекций обычно разумный компромисс — `small` или `medium`.
- Текущий Docker-образ CPU-first. Для полноценного `--device cuda` внутри контейнера понадобится отдельная CUDA/cuDNN runtime-конфигурация, как и рекомендует документация `faster-whisper`.
- `json` удобен, если потом захочешь строить поверх транскрипта конспект, summary или пост-обработку по сегментам.

## Локальный запуск без Docker

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -U pip
pip install .
python -m whisper_transcribator lecture.wav -o lecture.txt --model small --language ru
```

## Тесты

```bash
python3 -m unittest discover -s tests
```
