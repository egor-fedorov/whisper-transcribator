#!/bin/bash

set -euo pipefail

DIR="$PWD"
MODEL="${MODEL:-large-v3-turbo}"
THREADS="${THREADS:-1}"
JOBS="${JOBS:-1}"
CONTAINER="${CONTAINER:-whisper-transcribator}"
DEVICE="${DEVICE:-cpu}"
COMPUTE_TYPE="${COMPUTE_TYPE:-auto}"
PREFIX="${PREFIX:-result}"
OVERWRITE="${OVERWRITE:-0}"
MAP_FILE="${MAP_FILE:-$DIR/${PREFIX}_files.tsv}"

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ || ! "$THREADS" =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS и THREADS должны быть положительными целыми числами." >&2
    exit 1
fi
if [[ ! "$PREFIX" =~ ^[A-Za-z0-9_-]+$ || ! "$OVERWRITE" =~ ^[01]$ ]]; then
    echo "PREFIX: только латиница, цифры, _ и -; OVERWRITE: 0 или 1." >&2
    exit 1
fi
case "$DEVICE" in cpu|cuda) ;; *) echo "DEVICE: cpu или cuda." >&2; exit 1 ;; esac

map_tmp=$(mktemp)
files_tmp=$(mktemp)
trap 'rm -f "$map_tmp" "$files_tmp"' EXIT
find "$DIR" -maxdepth 1 -type f -iname '*.mp4' -print0 | LC_ALL=C sort -z > "$files_tmp"
mapfile -d '' -t files < "$files_tmp"
printf "index\toutput\tinput\n" > "$map_tmp"
for i in "${!files[@]}"; do
    input_name=${files[$i]##*/}
    if [[ "$input_name" == *$'\t'* || "$input_name" == *$'\n'* ]]; then
        echo "Имя файла содержит табуляцию или перевод строки: $input_name" >&2
        exit 1
    fi
    printf "%03d\t%s_%03d.txt\t%s\n" "$((i + 1))" "$PREFIX" "$((i + 1))" "$input_name" >> "$map_tmp"
done
if [[ -e "$MAP_FILE" ]] && ! cmp -s "$map_tmp" "$MAP_FILE"; then
    echo "Список файлов изменился. Прежняя карта сохранена: $MAP_FILE. Используйте новый PREFIX." >&2
    exit 1
fi
if [[ ! -e "$MAP_FILE" ]]; then
    if compgen -G "$DIR/${PREFIX}_[0-9]*.txt" > /dev/null; then
        echo "Есть результаты без карты файлов. Используйте новый PREFIX." >&2
        exit 1
    fi
    cp -- "$map_tmp" "$MAP_FILE"
fi

docker_args=()
whisper_args=()
if [[ "$DEVICE" == cuda ]]; then
    docker_args+=(--gpus all)
    for node in /dev/nvidia-uvm /dev/nvidia-uvm-tools; do
        [[ ! -c "$node" ]] || docker_args+=(--device "$node")
    done
fi
[[ "$OVERWRITE" != 1 ]] || whisper_args+=(--overwrite)
pids=()
failed=0
for i in "${!files[@]}"; do
        video=${files[$i]}
        input_name=$(basename "$video")
        output_name=$(printf "%s_%03d.txt" "$PREFIX" "$((i + 1))")
        output="$DIR/$output_name"

        if [[ "$OVERWRITE" != "1" && -s "$output" ]]; then
            echo "Пропускаем, уже есть: $output"
            continue
        fi

        echo "Обрабатываем: $input_name -> $output_name"

        (
            if docker run --rm "${docker_args[@]}" \
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
                --device "$DEVICE" \
                --compute-type "$COMPUTE_TYPE" \
                --cpu-threads "$THREADS" "${whisper_args[@]}" && [[ -s "$output" ]]; then
                echo "Готово: $output"
            else
                echo "Ошибка обработки: $input_name" >&2
                exit 1
            fi
        ) &

        pids+=("$!")
        if [[ "${#pids[@]}" -ge "$JOBS" ]]; then
            wait "${pids[0]}" || failed=1
            pids=("${pids[@]:1}")
        fi
done
for pid in "${pids[@]}"; do
    wait "$pid" || failed=1
done
if [[ "$failed" == 1 ]]; then
    echo "Некоторые файлы не обработаны; смотрите ошибки выше." >&2
    exit 1
fi

echo "Все файлы обработаны!"
echo "Соответствие файлов: $MAP_FILE"
