# whisper-transcribator

[English](README.md)

Локальный CLI для транскрибации аудио и видео через faster-whisper. Linux,
CPU и NVIDIA GPU. TXT без переноса каждой реплики на новую строку, SRT и JSON.
Это распознавание речи, не автоматическое составление конспекта.

## Запуск

```bash
docker build -t whisper-transcribator .
docker run --rm -v "$PWD:/work" -v whisper-models:/models \
  whisper-transcribator transcribe /work/lecture.mp4 \
  --output-dir /work/results --format all --model small --language ru
```

Без Docker: `pip install .`, затем `whisper-transcribator --help`.
Python 3.10 и 3.13 проверяются CI; runtime Docker закреплён на 3.13.

```bash
whisper-transcribator transcribe --input-dir ./lectures --output-dir ./results \
  --format all --skip-existing --model large-v3 --device cuda --language ru
whisper-transcribator models download large-v3
whisper-transcribator models list
whisper-transcribator doctor --device cuda --json
```

Каталог обрабатывается без рекурсии, с сортировкой имён. Явно перечисленные
файлы сохраняют заданный порядок. Для нумерации: `--naming numbered --prefix result`.
Карта `result_files.json` защищает номера от незаметной смены входного списка.
При изменении списка выбирайте новый префикс. Старые TSV-карты не импортируются.

`--jobs` задаёт число CPU-процессов, каждый держит свою модель. На GPU допускается
`--jobs 1`; `--batch-size` управляет внутренним batching, а не числом файлов.
По умолчанию один процесс переиспользует модель для всех записей.

`--skip-existing` пропускает только полный набор непустых результатов. Проверки
актуальности исходника/настроек нет. Частичный набор заменяется только с
`--overwrite`. По умолчанию ошибка останавливает обработку;
`--continue-on-error` продолжает остальные файлы, сохраняя ненулевой итоговый код.

## GPU и ограничения

```bash
docker build --target cuda -t whisper-transcribator:cuda .
docker run --rm --gpus all -v "$PWD:/work" -v whisper-models:/models \
  whisper-transcribator:cuda transcribe --input-dir /work --output-dir /work/results \
  --model large-v3 --device cuda --compute-type float16 --language ru
```

Нужны NVIDIA-драйвер и Container Toolkit. Если CUDA недоступна, хотя `nvidia-smi`
работает, проверьте `--device /dev/nvidia-uvm --device /dev/nvidia-uvm-tools`.
`doctor` проверяет доступность runtime, но не заменяет реальный inference-тест.

Длинные записи декодируются целиком и могут расходовать много RAM даже на GPU.
Автоматического resume нет. Каждый выходной файл публикуется атомарно, но три
формата не являются одной транзакцией. Существующие исходники защищены от замены.
Текст требует проверки: возможны пропуски, повторы и ошибки терминов.

`run_whisper.sh` и `transcribe_dir.sh` теперь только запускают Docker. Переменные
JOBS/THREADS/MODEL/DEVICE у первого сохранены; MAP_FILE удалён. Выбор файлов,
нумерация и проверки находятся в Python, а не дублируются в Bash.

Исследование бинарной поставки: [C++-прототип](experiments/whisper_cpp/README.md).
Полный переход на него пока не принят. Лицензия собственного кода: [MIT](LICENSE).
