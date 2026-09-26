#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./transcribe_dir.sh <directory> [extra whisper args...]

Environment variables:
  WHISPER_IMAGE=whisper-transcribator
  WHISPER_MODEL=small
  WHISPER_LANGUAGE=ru
  WHISPER_FORMAT=text  # text, srt, json, all
  WHISPER_DEVICE=cpu
  WHISPER_COMPUTE_TYPE=int8
  WHISPER_CPU_THREADS=<auto>
  WHISPER_BATCH_SIZE=1
  WHISPER_MODELS_VOLUME=whisper-models
  WHISPER_OVERWRITE=0
  DOCKER_BIN=docker

Examples:
  ./transcribe_dir.sh ~/lectures
  ./transcribe_dir.sh ~/lectures --device cpu --word-timestamps
  WHISPER_MODEL=medium WHISPER_FORMAT=srt WHISPER_CPU_THREADS=16 ./transcribe_dir.sh ~/lectures
EOF
}

detect_cpu_threads() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi

  getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" || $# -lt 1 ]]; then
  usage
  exit 0
fi

input_dir_arg="$1"
shift

if [[ ! -d "$input_dir_arg" ]]; then
  echo "Directory not found: $input_dir_arg" >&2
  exit 1
fi

input_dir="$(cd "$input_dir_arg" && pwd)"
extra_args=("$@")

image="${WHISPER_IMAGE:-whisper-transcribator}"
model="${WHISPER_MODEL:-small}"
language="${WHISPER_LANGUAGE:-ru}"
format="${WHISPER_FORMAT:-text}"
device="${WHISPER_DEVICE:-cpu}"
compute_type="${WHISPER_COMPUTE_TYPE:-int8}"
cpu_threads="${WHISPER_CPU_THREADS:-$(detect_cpu_threads)}"
batch_size="${WHISPER_BATCH_SIZE:-1}"
models_volume="${WHISPER_MODELS_VOLUME:-whisper-models}"
overwrite="${WHISPER_OVERWRITE:-0}"
docker_bin="${DOCKER_BIN:-docker}"

case "$format" in
  text) output_ext="txt" ;;
  srt) output_ext="srt" ;;
  json) output_ext="json" ;;
  all) output_ext="json" ;;
  *)
    echo "Unsupported WHISPER_FORMAT: $format" >&2
    exit 1
    ;;
esac

files=()
while IFS= read -r -d '' file; do
  files+=("$file")
done < <(
  find "$input_dir" -maxdepth 1 -type f \
    \( \
      -iname '*.aac' -o \
      -iname '*.aiff' -o \
      -iname '*.avi' -o \
      -iname '*.flac' -o \
      -iname '*.m4a' -o \
      -iname '*.m4b' -o \
      -iname '*.m4v' -o \
      -iname '*.mkv' -o \
      -iname '*.mov' -o \
      -iname '*.mp3' -o \
      -iname '*.mp4' -o \
      -iname '*.mpeg' -o \
      -iname '*.mpg' -o \
      -iname '*.oga' -o \
      -iname '*.ogg' -o \
      -iname '*.opus' -o \
      -iname '*.wav' -o \
      -iname '*.webm' -o \
      -iname '*.wma' -o \
      -iname '*.wmv' \
    \) \
    -print0
)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No media files found in: $input_dir" >&2
  exit 0
fi

pending_inputs=()

for file_path in "${files[@]}"; do
  file_name="$(basename "$file_path")"
  base_name="${file_name%.*}"
  output_name="${base_name}.${output_ext}"
  output_path="${input_dir}/${output_name}"

  if [[ "$overwrite" != "1" && -s "$output_path" ]] && \
     { [[ "$format" != all ]] || { [[ -s "${input_dir}/${base_name}.txt" ]] && [[ -s "${input_dir}/${base_name}.srt" ]]; }; }; then
    echo "Skipping existing transcript: $output_name" >&2
    continue
  fi

  pending_inputs+=("/work/${file_name}")
done

if [[ ${#pending_inputs[@]} -eq 0 ]]; then
  echo "No files to transcribe." >&2
  exit 0
fi

echo "Transcribing ${#pending_inputs[@]} file(s) with ${cpu_threads} CPU thread(s)." >&2
docker_args=()
if [[ "$device" == cuda ]]; then
  docker_args+=(--gpus all)
  for node in /dev/nvidia-uvm /dev/nvidia-uvm-tools; do
    [[ ! -c "$node" ]] || docker_args+=(--device "$node")
  done
fi
[[ "$overwrite" != 1 ]] || extra_args+=(--overwrite)
"$docker_bin" run --rm "${docker_args[@]}" \
  -v "${input_dir}:/work" \
  -v "${models_volume}:/models" \
  "$image" \
  "${pending_inputs[@]}" \
  --output-dir /work \
  --model "$model" \
  --language "$language" \
  --format "$format" \
  --device "$device" \
  --compute-type "$compute_type" \
  --cpu-threads "$cpu_threads" \
  --batch-size "$batch_size" \
  "${extra_args[@]}"
