#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
install_prefix=${EVOLUTION_OMARCHY_PREFIX:-"$HOME/.local/opt/evolution-omarchy"}
case "$install_prefix" in /*) ;; *) echo 'Installation prefix must be absolute.' >&2; exit 1;; esac
cmake -S "$repo_dir" -B "$repo_dir/build-omarchy" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$install_prefix" \
  -DENABLE_GTK_DOC=OFF -DWITH_HELP=OFF -DENABLE_INSTALLED_TESTS=OFF \
  -DENABLE_BOGOFILTER=OFF -DENABLE_SPAMASSASSIN=OFF -DENABLE_TEXT_HIGHLIGHT=OFF
cmake --build "$repo_dir/build-omarchy" --parallel "${BUILD_JOBS:-6}"
