#!/usr/bin/env bash
# Build and flash the firmware for the Waveshare ESP32-S3-Touch-LCD-5.
#
#   tools/fw.sh build      apply our SDK patches, then build
#   tools/fw.sh flash      build, then flash the board over USB
#   tools/fw.sh monitor    show the board's console (Ctrl-] to quit)
#
# Needs ESP-IDF v6.0.1 (~/esp/esp-idf-v6.0.1, or IDF_EXPORT=/path/to/export.sh)
# and your SDK token in sdk_token.local (one line, mgst_...; git-ignored).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/muse-gadget-sdk"
ESP32="$SDK/esp32"
PATCHES="$ROOT/patches/muse-gadget-sdk"
BOARD=lcd5
BUILD="$ESP32/build-muse-waveshare-s3-lcd5"
TOKEN_FILE="$ROOT/sdk_token.local"

die() { echo "fw.sh: $*" >&2; exit 1; }

# Our changes to the SDK live as patches in this repo (the SDK is a pinned
# submodule, not a fork). Apply each one unless it's already in place.
apply_patches() {
    git -C "$ROOT" submodule update --init third_party/muse-gadget-sdk
    local p
    for p in "$PATCHES"/*.patch; do
        if git -C "$SDK" apply --reverse --check "$p" 2>/dev/null; then
            continue # already applied
        fi
        git -C "$SDK" apply --check "$p" 2>/dev/null \
            || die "$(basename "$p") doesn't apply: the SDK checkout has other local changes"
        git -C "$SDK" apply "$p"
        echo "fw.sh: applied $(basename "$p")"
    done
}

# Hand the token to the build without printing it or letting git see it.
# A fresh build takes it from a private overlay; an existing sdkconfig keeps
# its own values over defaults, so update the line in place there.
token_overlay() {
    [[ -s "$TOKEN_FILE" ]] || die "put your SDK token in $TOKEN_FILE (gadgets.muse.ai > Account > SDK tokens)"
    mkdir -p "$BUILD"
    python3 - "$TOKEN_FILE" "$BUILD" <<'PY'
import os, re, sys
token_file, build = sys.argv[1], sys.argv[2]
token = open(token_file).read().strip()
if not re.fullmatch(r"mgst_[A-Za-z0-9_-]*[AEIMQUYcgkosw048]", token) or len(token) != 48:
    sys.exit("fw.sh: sdk_token.local doesn't look like an SDK token (48 characters, mgst_...)")
line = f'CONFIG_GADGET_SDK_TOKEN="{token}"\n'
overlay = os.path.join(build, "sdkconfig.token")
fd = os.open(overlay, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w") as f:
    f.write(line)
cfg = os.path.join(build, "sdkconfig")
if os.path.exists(cfg):
    text = open(cfg).read()
    new = re.sub(r'^CONFIG_GADGET_SDK_TOKEN=.*\n', line, text, flags=re.M)
    if new == text and line not in text:
        new = text + line
    if new != text:
        open(cfg, "w").write(new)
PY
    export MUSE_EXTRA_DEFAULTS="$ROOT/firmware/sdkconfig.todo;$BUILD/sdkconfig.token"
}

idf_env() {
    if [[ -n "${IDF_EXPORT:-}" ]]; then
        # shellcheck disable=SC1090
        . "$IDF_EXPORT" >/dev/null 2>&1
    elif ! command -v idf.py >/dev/null 2>&1; then
        local d
        for d in "$HOME/.espressif/esp-idf-v6.0.1" "$HOME/esp/esp-idf-v6.0.1"; do
            # shellcheck disable=SC1091
            [[ -f "$d/export.sh" ]] && { . "$d/export.sh" >/dev/null 2>&1; break; }
        done
    fi
    command -v idf.py >/dev/null 2>&1 || die "ESP-IDF v6.0.1 not found (see docs/05-open-questions-and-build.md)"
}

build() {
    apply_patches
    token_overlay
    "$ESP32/tools/muse/board.sh" build "$BOARD"
}

case "${1:-}" in
build)
    build
    ;;
flash)
    build
    "$ESP32/tools/muse/board.sh" flash "$BOARD" "${2:-}"
    ;;
monitor)
    idf_env
    port="$(python3 "$ESP32/tools/muse/ports.py" "$BOARD" ${2:-})"
    cd "$BUILD" && exec idf.py -B "$BUILD" -p "$port" monitor
    ;;
*)
    sed -n '2,9p' "$0"
    exit 2
    ;;
esac
