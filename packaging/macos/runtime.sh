#!/usr/bin/env bash
# Private Mach-O closure, relocation and signing for the macOS packaging entrypoint.
# Libraries a Mach-O file loads, and its rpaths.
wt_macos_load_commands() {
    otool -l "$1" | awk '/cmd LC_(LOAD|LOAD_WEAK|REEXPORT|LOAD_UPWARD)_DYLIB$/ { load = 1 }
        load && $1 == "name" { print $2; load = 0 }'
}
wt_macos_rpaths() {
    otool -l "$1" | awk '/cmd LC_RPATH$/ { rpath = 1 } rpath && $1 == "path" { print $2; rpath = 0 }'
}
# Prints the build output a dependency refers to; returns 1 for macOS libraries, 2 on errors.
wt_macos_resolve() {
    local work=$1 build=$2 name=$3 directory
    case $name in
        /usr/lib/* | /System/Library/*) return 1 ;;
        @rpath/*) name=${name#@rpath/} ;;
        "$work"/*) name=$(basename "$name") ;;
        *)
            echo "Only macOS system libraries may be linked outside the archive: $3" >&2
            return 2
            ;;
    esac
    for directory in "$build/bin" "$work/media/lib"; do
        if [[ -e $directory/$name ]]; then
            printf '%s\n' "$directory/$name"
            return 0
        fi
    done
    echo "Cannot resolve dependency: $3" >&2
    return 2
}

wt_macos_stage_runtime() {
    local work=$1 build=$2 bundle=$3 module changed file dependency status origin target duplicates rpath
    local -a changes
    mkdir -p "$bundle"/{bin,lib,licenses,sources,share}
    cp "$build/whisper-transcribator" "$bundle/bin/"
    cp "$build/generated/package.env" "$bundle/share/build-metadata.env"
    while IFS= read -r module; do
        cp "$module" "$bundle/lib/"
        basename "$module" >>"$bundle/share/backends.txt"
    done < <(find "$build/bin" -maxdepth 1 -type f -name 'libggml-*.so' | sort)
    test -s "$bundle/share/backends.txt"
    # Copy dependencies until the set is closed; references keep the file names they use.
    changed=true
    while $changed; do
        changed=false
        for file in "$bundle"/bin/whisper-transcribator "$bundle"/lib/*; do
            while IFS= read -r dependency; do
                status=0
                origin=$(wt_macos_resolve "$work" "$build" "$dependency") || status=$?
                case $status in
                    0) ;;
                    1) continue ;;
                    *) return 1 ;;
                esac
                target="$bundle/lib/$(basename "$dependency")"
                if [[ ! -e $target ]]; then
                    cp -L "$origin" "$target"
                    chmod u+w "$target"
                    changed=true
                fi
            done < <(wt_macos_load_commands "$file")
        done
    done
    # One copy per library: a second one would load a separate instance.
    duplicates=$(cd "$bundle/lib" && shasum -a 256 -- * | awk '{ print $1 }' | sort | uniq -d)
    if [[ -n $duplicates ]]; then
        echo "Library bundled under several names: $duplicates" >&2
        return 1
    fi
    for file in "$bundle"/bin/whisper-transcribator "$bundle"/lib/*; do
        changes=()
        while IFS= read -r dependency; do
            case $dependency in /usr/lib/* | /System/Library/*) continue ;; esac
            changes+=(-change "$dependency" "@rpath/$(basename "$dependency")")
        done < <(wt_macos_load_commands "$file")
        while IFS= read -r rpath; do
            changes+=(-delete_rpath "$rpath")
        done < <(wt_macos_rpaths "$file")
        case $file in
            */bin/*) changes+=(-add_rpath @loader_path/../lib) ;;
            *.dylib) changes+=(-id "@rpath/$(basename "$file")" -add_rpath @loader_path) ;;
            *) changes+=(-add_rpath @loader_path) ;;
        esac
        install_name_tool "${changes[@]}" "$file" 2>&1 | { grep -v 'will invalidate the code signature' || true; } >&2
        codesign --force --sign - "$file"
    done
    for file in "$bundle"/bin/whisper-transcribator "$bundle"/lib/*; do
        printf '# %s\n' "${file#"$bundle"/}"
        wt_macos_load_commands "$file"
    done >"$bundle/share/linked-libraries.txt"
}
