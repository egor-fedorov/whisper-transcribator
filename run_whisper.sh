#!/bin/bash

set -euo pipefail

DIR="$PWD"
MODEL="${MODEL:-large-v3-turbo}"
THREADS="${THREADS:-1}"
CONTAINER="${CONTAINER:-whisper-transcribator}"
PREFIX="${PREFIX:-result}"
OVERWRITE="${OVERWRITE:-0}"
MAP_FILE="${MAP_FILE:-$DIR/${PREFIX}_files.tsv}"

printf "index\toutput\tinput\n" > "$MAP_FILE"

index=1
while IFS= read -r -d '' video; do
        input_name=$(basename "$video")
        output_name=$(printf "%s_%03d.txt" "$PREFIX" "$index")
        output="$DIR/$output_name"

        printf "%03d\t%s\t%s\n" "$index" "$output_name" "$input_name" >> "$MAP_FILE"

        if [[ "$OVERWRITE" != "1" && -f "$output" ]]; then
            echo "Пропускаем, уже есть: $output"
            index=$((index + 1))
            continue
        fi

        echo "Обрабатываем: $input_name -> $output_name"

        docker run --rm \
            -e OMP_NUM_THREADS="$THREADS" \
            -e OPENBLAS_NUM_THREADS="$THREADS" \
            -e MKL_NUM_THREADS="$THREADS" \
            -v "$DIR:/lectures/multithreading" \
            -v whisper-models:/models \
            "$CONTAINER" \
            "/lectures/multithreading/$input_name" \
            -o "/lectures/multithreading/$output_name" \
            --model "$MODEL" \
            --language ru \
            --device cpu \
            --compute-type int8 \
            --cpu-threads "$THREADS"

        echo "Готово: $output"
        echo "---"

        index=$((index + 1))
done < <(find "$DIR" -maxdepth 1 -type f -name '*.mp4' -print0 | LC_ALL=C sort -z)

echo "Все файлы обработаны!"
echo "Соответствие файлов: $MAP_FILE"
