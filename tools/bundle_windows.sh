#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 3 ]]; then
    echo "Usage: bash tools/bundle_windows.sh BUILD_DIR NEW_BUNDLE_DIR GTK_PREFIX" >&2
    exit 2
fi
build=$(cd "$1" && pwd)
prefix=$(cd "$3" && pwd)
destination=$2
if [[ -e "$destination" ]]; then
    echo "Refusing to overwrite existing bundle: $destination" >&2
    exit 1
fi
for binary in liarsofttool.exe liarsofttool-gui.exe; do
    [[ -f "$build/$binary" ]] || { echo "Missing executable: $build/$binary" >&2; exit 1; }
done
for binary in glib-compile-schemas.exe gdk-pixbuf-query-loaders.exe update-mime-database.exe; do
    [[ -f "$prefix/bin/$binary" ]] || { echo "Missing GTK utility: $binary" >&2; exit 1; }
done
objdump=${OBJDUMP:-objdump}
command -v "$objdump" >/dev/null
# Cross-build checks may run the Windows cache generators through Wine.
runner=()
if [[ -n ${EXE_RUNNER:-} ]]; then runner=("$EXE_RUNNER"); fi
mkdir -p "$destination/bin" "$destination/lib" "$destination/share"
destination=$(cd "$destination" && pwd)
cp "$build/liarsofttool.exe" "$build/liarsofttool-gui.exe" "$destination/bin/"
cp "$prefix/bin/glib-compile-schemas.exe" "$prefix/bin/gdk-pixbuf-query-loaders.exe" "$prefix/bin/update-mime-database.exe" "$destination/bin/"
for directory in lib/gdk-pixbuf-2.0 lib/gtk-3.0 share/glib-2.0 share/icons share/themes share/licenses share/fontconfig share/mime etc/fonts; do
    if [[ -d "$prefix/$directory" ]]; then
        mkdir -p "$destination/$(dirname "$directory")"
        cp -R "$prefix/$directory" "$destination/$(dirname "$directory")/"
    fi
done
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cp "$root/README.md" "$root/COPYING" "$destination/"
mkdir -p "$destination/share/doc/LiarsoftTool"
cp "$root/docs/THIRD_PARTY_AUDIO_LICENSES.md" "$destination/share/doc/LiarsoftTool/"

mapfile -d '' queue < <(find "$destination" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
for ((i=0; i<${#queue[@]}; ++i)); do
    # Scan each copied DLL too; direct dependencies alone are insufficient.
    imports=$("$objdump" -p "${queue[i]}" | awk '/DLL Name:/ {print $3}')
    while IFS= read -r dll; do
        [[ -n "$dll" ]] || continue
        [[ -n $(find "$destination/bin" -maxdepth 1 -iname "$dll" -print -quit) ]] && continue
        source=$(find "$prefix/bin" -maxdepth 1 -iname "$dll" -print -quit)
        if [[ -n "$source" ]]; then
            cp "$source" "$destination/bin/$dll"
            queue+=("$destination/bin/$dll")
            continue
        fi
        case "${dll,,}" in
            api-ms-win-*|ext-ms-win-*|kernel32.dll|ntdll.dll|msvcrt.dll|ucrtbase.dll|\
            user32.dll|gdi32.dll|gdiplus.dll|opengl32.dll|dwrite.dll|dwmapi.dll|\
            imm32.dll|comctl32.dll|comdlg32.dll|usp10.dll|msimg32.dll|uxtheme.dll|\
            advapi32.dll|shell32.dll|shlwapi.dll|ole32.dll|oleaut32.dll|rpcrt4.dll|\
            ws2_32.dll|winmm.dll|setupapi.dll|cfgmgr32.dll|hid.dll|winspool.drv|\
            version.dll|bcrypt.dll|bcryptprimitives.dll|crypt32.dll|dnsapi.dll|\
            iphlpapi.dll|secur32.dll|netapi32.dll|userenv.dll|wtsapi32.dll|\
            winhttp.dll|normaliz.dll|wldap32.dll)
                ;;
            *) echo "Unresolved non-system dependency: $dll (${queue[i]})" >&2; exit 1 ;;
        esac
    done <<< "$imports"
done
(
    cd "$destination"
    "${runner[@]}" bin/glib-compile-schemas.exe share/glib-2.0/schemas
    "${runner[@]}" bin/gdk-pixbuf-query-loaders.exe > lib/gdk-pixbuf-2.0/2.10.0/loaders.cache
    "${runner[@]}" bin/update-mime-database.exe share/mime
)
echo "Windows GTK bundle ready: $destination (launch bin/liarsofttool-gui.exe)"
