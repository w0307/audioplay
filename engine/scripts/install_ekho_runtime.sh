#!/usr/bin/env bash
# Install the already-built Ekho v8.9 executable/archive and prepared Mandarin data.
# Usage: sudo bash scripts/install_ekho_runtime.sh /home/xf/code/ekho /opt/ekho-8.9
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo "Usage: $0 EKHO_SOURCE_AND_BUILD_DIR INSTALL_PREFIX" >&2
    exit 2
fi
ekho_source_dir=$(realpath "$1")
ekho_install_prefix=$(realpath -m "$2")
if [[ "$ekho_install_prefix" == / || "$ekho_install_prefix" == "$ekho_source_dir" ]]; then
    echo "Use a separate installation prefix, for example /opt/ekho-8.9" >&2
    exit 2
fi
python3 - "$ekho_source_dir" <<'PY'
import pathlib,re,sys
root=pathlib.Path(sys.argv[1])
config=(root/'config.h').read_text()
if not re.search(r'^#define\s+PACKAGE_VERSION\s+"8\.9"\s*$',config,re.M):
    raise SystemExit('Expected an Ekho v8.9 build.')
for name in ('ekho','libekho.a','ekho-data/zh.dict','ekho-data/pinyin.index','ekho-data/pinyin.voice'):
    if not (root/name).is_file() or (root/name).stat().st_size==0:
        raise SystemExit('Missing '+name+'; build Ekho and generate Mandarin indexes first (README).')
PY
install -d "$ekho_install_prefix/bin" "$ekho_install_prefix/lib" \
    "$ekho_install_prefix/share/ekho-data" "$ekho_install_prefix/share/doc/ekho"
install -m 0755 "$ekho_source_dir/ekho" "$ekho_install_prefix/bin/ekho"
install -m 0644 "$ekho_source_dir/libekho.a" "$ekho_install_prefix/lib/libekho.a"
for name in zh.dict zh_list zh_listx zh_patch pinyin.index pinyin.voice; do
    if [[ -f "$ekho_source_dir/ekho-data/$name" ]]; then
        install -m 0644 "$ekho_source_dir/ekho-data/$name" "$ekho_install_prefix/share/ekho-data/$name"
    fi
done
# Dereference pinyin's source-tree symlink so deployment is self-contained.
for name in pinyin alphabet; do
    if [[ -d "$ekho_source_dir/ekho-data/$name" ]]; then
        if [[ -L "$ekho_install_prefix/share/ekho-data/$name" ]]; then
            echo "Refusing to install into a destination symlink: $name" >&2
            exit 2
        fi
        install -d "$ekho_install_prefix/share/ekho-data/$name"
        cp -aL "$ekho_source_dir/ekho-data/$name/." "$ekho_install_prefix/share/ekho-data/$name/"
    fi
done
for name in COPYING LICENSE; do
    if [[ -f "$ekho_source_dir/$name" ]]; then
        install -m 0644 "$ekho_source_dir/$name" "$ekho_install_prefix/share/doc/ekho/$name"
    fi
done
for path in "$ekho_source_dir"/ekho-data/COPYING*; do
    [[ ! -f "$path" ]] || install -m 0644 "$path" "$ekho_install_prefix/share/doc/ekho/"
done
echo "Installed Ekho and Mandarin data to: $ekho_install_prefix"
echo "Runtime data path: $ekho_install_prefix/share/ekho-data"
