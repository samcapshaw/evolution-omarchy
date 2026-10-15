#!/usr/bin/env bash
# Explicit opt-in build of the matching local dependency stack. No sudo or system installation.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
stack_dir="$repo_dir/build-stack"
install_prefix=${EVOLUTION_OMARCHY_PREFIX:-"$HOME/.local/opt/evolution-omarchy-next"}
case "$install_prefix" in "$HOME"/*) ;; *) echo 'Use a dedicated installation directory inside your home.' >&2; exit 1;; esac
command -v gperf >/dev/null || { echo 'gperf is required; install the documented prerequisites first.' >&2; exit 1; }
jobs=${BUILD_JOBS:-4}
mkdir -p "$stack_dir"
fetch_source() {
  local name=$1 sha=$2 destination="$stack_dir/$1"
  if [[ ! -d "$destination/.git" ]]; then
    git init "$destination"
    git -C "$destination" remote add origin "https://github.com/GNOME/$name.git"
  fi
  if [[ -n $(git -C "$destination" status --porcelain) ]]; then
    echo "Refusing to overwrite changes in $destination" >&2
    exit 1
  fi
  git -C "$destination" fetch --depth=1 origin "$sha"
  git -C "$destination" checkout --detach "$sha"
}
fetch_source evolution-data-server 757672184698ec9f46cc16cf8860ec312d1e9d36
fetch_source evolution-ews 82921380d1f629b230906f800c064133bde6d6ee
export PKG_CONFIG_PATH="$install_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$install_prefix/lib:$install_prefix/lib/evolution-data-server${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake -S "$stack_dir/evolution-data-server" -B "$stack_dir/eds-build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$install_prefix" -DCMAKE_SHARED_LINKER_FLAGS="-L$install_prefix/lib" \
  -DCMAKE_MODULE_LINKER_FLAGS="-L$install_prefix/lib" -DCMAKE_EXE_LINKER_FLAGS="-L$install_prefix/lib" -DENABLE_GTK_DOC=OFF \
  -DENABLE_INSTALLED_TESTS=OFF -DENABLE_VALA_BINDINGS=OFF -DENABLE_INTROSPECTION=OFF \
  -DENABLE_GOA=OFF -DENABLE_GOOGLE=ON -DENABLE_WEATHER=OFF \
  -DENABLE_OAUTH2_WEBKITGTK4=OFF -DWITH_OPENLDAP=OFF -DWITH_LIBDB=OFF -DENABLE_CANBERRA=OFF
cmake --build "$stack_dir/eds-build" --parallel "$jobs"
cmake --install "$stack_dir/eds-build"
cmake -S "$repo_dir" -B "$stack_dir/mail-build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$install_prefix" -DCMAKE_SHARED_LINKER_FLAGS="-L$install_prefix/lib" \
  -DCMAKE_MODULE_LINKER_FLAGS="-L$install_prefix/lib" -DCMAKE_EXE_LINKER_FLAGS="-L$install_prefix/lib" -DENABLE_GTK_DOC=OFF -DWITH_HELP=OFF \
  -DENABLE_INSTALLED_TESTS=OFF -DENABLE_TEXT_HIGHLIGHT=OFF
cmake --build "$stack_dir/mail-build" --parallel "$jobs"
cmake --install "$stack_dir/mail-build"
cmake -S "$stack_dir/evolution-ews" -B "$stack_dir/ews-build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="$install_prefix" -DCMAKE_SHARED_LINKER_FLAGS="-L$install_prefix/lib" \
  -DCMAKE_MODULE_LINKER_FLAGS="-L$install_prefix/lib" -DCMAKE_EXE_LINKER_FLAGS="-L$install_prefix/lib" -DENABLE_GTK_DOC=OFF -DENABLE_TESTS=OFF
cmake --build "$stack_dir/ews-build" --parallel "$jobs"
cmake --install "$stack_dir/ews-build"
printf 'Built the local stack in %s. Your current launcher and account settings were not changed.\n' "$install_prefix"
