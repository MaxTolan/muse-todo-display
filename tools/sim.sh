#!/usr/bin/env bash
# Build, test and run the 800x480 todo simulator.
#
#   tools/sim.sh build              configure + build into build/sim
#   tools/sim.sh test               build, then run the model and scenario tests
#   tools/sim.sh run [ARGS...]      open the simulator window (see sim/README.md)
#   tools/sim.sh previews           render every scenario's screenshots into docs/previews
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/sim"

# Some Command Line Tools releases ship a newest macOS SDK their own linker
# can't read ("tapi error: malformed file"). If the default SDK can't link a
# trivial program, fall back to the newest one that can.
pick_macos_sdk() {
    [[ "$(uname)" == Darwin && -z "${SDKROOT:-}" ]] || return 0
    local probe
    probe="$(mktemp -d)"
    echo 'int main(void){return 0;}' > "$probe/t.c"
    if ! cc "$probe/t.c" -o "$probe/t" 2>/dev/null; then
        local sdk
        for sdk in $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -rV); do
            if SDKROOT="$sdk" cc "$probe/t.c" -o "$probe/t" 2>/dev/null; then
                export SDKROOT="$sdk"
                echo "sim.sh: default macOS SDK can't link; using $(basename "$sdk")" >&2
                break
            fi
        done
    fi
    rm -rf "$probe"
}

build() {
    git -C "$ROOT" submodule update --init third_party/muse-gadget-sdk
    pick_macos_sdk
    cmake -S "$ROOT/sim" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$BUILD"
}

case "${1:-}" in
build)
    build
    ;;
test)
    build
    ctest --test-dir "$BUILD" --output-on-failure
    ;;
run)
    shift
    [[ -x "$BUILD/todo_sim" ]] || build
    exec "$BUILD/todo_sim" "$@"
    ;;
previews)
    build
    out="$ROOT/build/previews"
    rm -rf "$out" && mkdir -p "$out"
    for scenario in "$ROOT"/sim/scenarios/*.txt; do
        "$BUILD/todo_sim" --headless --scenario "$scenario" --out "$out" >/dev/null
    done
    python3 "$ROOT/tools/compress_previews.py" "$out" "$ROOT/docs/previews"
    ;;
*)
    sed -n '2,8p' "$0"
    exit 2
    ;;
esac
