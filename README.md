# whisper_transcribator

CLI-утилита для транскрибации медиафайлов через `faster-whisper`. Неважно, видео это или аудио: для нас важно только то, что входной файл может быть декодирован библиотекой PyAV, которую использует `faster-whisper`.

## Что умеет

- принимает любой медиафайл, который может декодировать `faster-whisper`
- сохраняет результат в `.txt`, `.srt` или `.json`, либо во всех форматах за один проход
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

- новые модели скачиваются в стандартный snapshot-кеш Hugging Face внутри `WHISPER_DOWNLOAD_ROOT`; прерванные загрузки можно повторить
- полные старые каталоги `${WHISPER_DOWNLOAD_ROOT}/small` продолжают работать; пустой каталог больше не считается готовой моделью
- в Docker-образе `WHISPER_DOWNLOAD_ROOT=/models`, поэтому достаточно примонтировать volume `whisper-models:/models`
- если передать путь в `--model`, например `/models/custom-large-v3-ct2`, CLI использует локальную модель без скачивания
- если нужен строго offline-режим, используй `--local-files-only`
- локальная модель должна содержать непустые `model.bin`, `config.json` и `tokenizer.json`; без tokenizer CLI останавливается, не пытаясь скачать его неявно

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
- `--format` — `text`, `srt`, `json` или `all`; `all` требует `--output-dir` и несовместим с `-o`
- `--overwrite` — разрешает заменять готовые транскрипты; исходные файлы нельзя перезаписывать даже с этим флагом
- `--model` — имя faster-whisper модели, HF repo id или путь к локальной converted model directory
- `--download-root` — куда скачивать named-модели
- `--local-files-only` — не ходить в сеть за моделями
- `--device` — `cpu`, `cuda` или `auto`
- `--compute-type` — например `int8`, `float16`, `int8_float16`
- `--cpu-threads` — количество CPU-потоков; `0` оставляет значение библиотеки
- `--batch-size` — если `>1`, включает `BatchedInferencePipeline`
- `--beam-size` — beam size декодирования
- `--word-timestamps` — добавляет word-level timestamps в JSON и segment data
- `--no-vad` — отключает VAD; требует `--batch-size 1`
- `--vad-min-silence-ms` — настраивает `min_silence_duration_ms`
- `--list-models` — показывает поддерживаемые модели и их cache status

## Практические замечания

- По умолчанию CLI использует модель `small`.
- Для CPU по умолчанию выбирается `compute_type=int8`, для CUDA — `float16`.
- Для русских лекций обычно разумный компромисс — `small` или `medium`.
- Обычная сборка создаёт CPU-образ; для GPU есть цель `cuda`, описанная ниже.
- `json` удобен, если потом захочешь строить поверх транскрипта конспект, summary или пост-обработку по сегментам.
- CLI заранее проверяет все входы, коллизии выходных имён и доступность каталога результатов. Существующие файлы требуют `--overwrite`.
- Прогресс выводится в stderr примерно раз в 30 секунд после начала выдачи сегментов; декодирование и VAD могут занять время до первого сегмента.
- Результат публикуется только после полного распознавания, атомарно для каждого файла. В режиме `all` три файла публикуются по отдельности. При остановке в середине лекции нужно повторить её обработку; автоматического продолжения с таймкода нет.
- `faster-whisper` декодирует аудио целиком: длинные записи расходуют RAM и на GPU. Для нескольких лекций предпочтителен один процесс с последовательной обработкой.

## Запуск на GPU

Нужны NVIDIA-драйвер и NVIDIA Container Toolkit на хосте. Образ содержит Python 3.13,
cuBLAS CUDA 12 и cuDNN 9. Python-зависимости зафиксированы в `requirements.lock`,
CUDA-пакеты и базовый образ зафиксированы в Dockerfile. Модели не входят в образ.

```bash
docker build --target cuda -t whisper-transcribator:cuda .
```

Проверка вычислений CUDA, а не только доступности `nvidia-smi`:

```bash
docker run --rm --gpus all \
  --entrypoint python whisper-transcribator:cuda \
  -c 'from whisper_transcribator.cli import choose_device; print(choose_device("cuda"))'
```

Если `nvidia-smi` работает, но CUDA выдаёт `unknown error`, на некоторых конфигурациях
нужно дополнительно передать `--device /dev/nvidia-uvm --device /dev/nvidia-uvm-tools`.
Это проверено на локальной RTX 4060 Ti; скрипты добавляют существующие UVM-устройства
при GPU-запуске. Если проверка всё ещё не проходит, сначала устраните проблему runtime.

Три файла последовательно с одной загрузкой модели, исходники только для чтения:

```bash
mkdir -p transcripts
docker run --rm --gpus all \
  -v "$PWD:/input:ro" \
  -v "$PWD/transcripts:/output" \
  -v whisper-models:/models \
  whisper-transcribator:cuda \
  /input/lecture1.mp4 /input/lecture2.mp4 /input/seminar.mp4 \
  --output-dir /output --format all \
  --model large-v3 --language ru \
  --device cuda --compute-type float16 --batch-size 1 --beam-size 5
```

Для повторной обработки существующих результатов добавьте `--overwrite`.
Если GPU явно запрошена и недоступна, CLI завершается с ошибкой, не переходя на CPU.

## Простой Запуск Каталога

`run_whisper.sh` ищет MP4 в текущем каталоге, сортирует через `LC_ALL=C sort`,
создаёт `result_001.txt`, `result_002.txt` и карту `result_files.tsv`.

```bash
./run_whisper.sh
JOBS=2 THREADS=3 ./run_whisper.sh
DEVICE=cuda CONTAINER=whisper-transcribator:cuda MODEL=large-v3 ./run_whisper.sh
OVERWRITE=1 ./run_whisper.sh
```

`JOBS` управляет числом одновременно обрабатываемых файлов; `THREADS` задаёт
CPU-потоки каждого процесса. По умолчанию оба равны `1`. На одной GPU начинайте
с `JOBS=1`. Тип вычислений можно задать через `COMPUTE_TYPE`.

Если список файлов изменился, скрипт остановится и сохранит старую карту:
выберите новый `PREFIX`, например `PREFIX=akos_v2 ./run_whisper.sh`.
Пустой результат не пропускается как готовый; для его замены нужен `OVERWRITE=1`.
Ошибки любых фоновых заданий приводят к ненулевому коду завершения скрипта.

`transcribe_dir.sh <directory>` поддерживает больше расширений и использует
исходные basename. Для GPU задайте `WHISPER_DEVICE=cuda`,
`WHISPER_COMPUTE_TYPE=float16`, `WHISPER_IMAGE=whisper-transcribator:cuda`.

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
