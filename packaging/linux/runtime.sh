#!/usr/bin/env bash
# Private ELF dependency collection for the Ubuntu packaging entrypoint.
wt_linux_dependencies() {
    local root
    for root in "$@"; do
        printf '# %s\n' "$root"
        ldd "$root"
    done
}
wt_linux_owner_of() {
    local path owner
    for path in "$1" "$(readlink -f "$1")" "${1/#\/lib\//\/usr\/lib\/}"; do
        owner=$(dpkg-query -S "$path" 2>/dev/null | head -n1 | sed 's/: \/.*//' || true)
        if [[ -n $owner ]]; then
            printf '%s\n' "$owner"
            return
        fi
    done
}

wt_linux_stage_runtime() {
    local build=$1 destination=$2 plugin library owner source package documentation notice target version
    local -a roots upgrades
    mkdir -p "$destination"/{bin,lib,licenses,sources,share}
    roots=("$build/whisper-transcribator")
    while IFS= read -r -d '' plugin; do
        roots+=("$plugin")
        cp "$plugin" "$destination/lib/"
        basename "$plugin" >>"$destination/share/backends.txt"
    done < <(find "$build/bin" -maxdepth 1 -type f -name 'libggml-*.so' -print0)
    # Base images may contain superseded distro builds whose source packages are no
    # longer in the mirror. Update only redistributable runtime dependencies first.
    mapfile -t upgrades < <(
        while IFS= read -r library; do
            owner=$(wt_linux_owner_of "$library")
            [[ -n $owner ]] || continue
            source=$(dpkg-query -W -f='${source:Package}' "$owner")
            case "$source" in glibc|gcc-*|cuda-*|libcublas*|libnccl*|nccl*) continue ;; esac
            printf '%s\n' "$owner"
        done < <(wt_linux_dependencies "${roots[@]}" | awk '/=> \// { print $3 }' | sort -u)
    )
    if ((${#upgrades[@]})); then
        apt-get install -y --only-upgrade --no-install-recommends "${upgrades[@]}"
    fi
    cp "$build/whisper-transcribator" "$destination/bin/"
    cp "$build/generated/package.env" "$destination/share/build-metadata.env"
    wt_linux_dependencies "${roots[@]}" >"$destination/share/linked-libraries.txt"
    if grep -q 'not found' "$destination/share/linked-libraries.txt"; then
        echo 'Unresolved runtime dependency' >&2
        exit 1
    fi
    while IFS= read -r library; do
        owner=$(wt_linux_owner_of "$library")
        if [[ -n $owner ]] && [[ $(dpkg-query -W -f='${source:Package}' "$owner") = glibc ]]; then
            continue
        fi
        case "$(basename "$library")" in
            libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|ld-linux*|libcuda.so.*) continue ;;
        esac
        cp -L "$library" "$destination/lib/"
        if [[ -n $owner ]]; then
            package=${owner%%:*}
            documentation="/usr/share/doc/$package"
            while IFS= read -r -d '' notice; do
                target="$destination/licenses/$package/${notice#"$documentation/"}"
                mkdir -p "$(dirname "$target")"
                cp -L "$notice" "$target"
            done < <(find -L "$documentation" -type f -print0)
            test -d "$destination/licenses/$package"
            dpkg-query -W -f='${source:Package}\t${source:Version}\n' "$owner" >>"$destination/share/system-sources.tsv"
        else
            case "$(basename "$library")" in
                libav*.so.*|libswresample.so.*|libwhisper.so.*|libggml*.so.*|libggml*.so) ;;
                *) echo "Missing package/license owner: $library" >&2; exit 1 ;;
            esac
        fi
    done < <(awk '/=> \// { print $3 }' "$destination/share/linked-libraries.txt" | sort -u)
    # The dynamic loader, not Bash, expands ORIGIN.
    # shellcheck disable=SC2016
    patchelf --set-rpath '$ORIGIN/../lib' "$destination/bin/whisper-transcribator"
    for library in "$destination"/lib/*; do
        # shellcheck disable=SC2016
        patchelf --set-rpath '$ORIGIN' "$library"
    done
    cp /etc/ssl/certs/ca-certificates.crt "$destination/share/cacert.pem"
    sort -u -o "$destination/share/system-sources.tsv" "$destination/share/system-sources.tsv"
    # Include source packages for redistributed distro libraries. GCC runtimes use
    # the Runtime Library Exception; CUDA/NCCL retain their vendor notices instead.
    mkdir -p "$destination/sources/system"
    while IFS=$'\t' read -r package version; do
        case "$package" in gcc-*|cuda-*|libcublas*|libnccl*|nccl*) continue ;; esac
        (cd "$destination/sources/system" && apt-get source --download-only "$package=$version")
    done <"$destination/share/system-sources.tsv"
}
