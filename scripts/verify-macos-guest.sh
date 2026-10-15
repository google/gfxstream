#!/bin/bash
# Run the gfxstream guest Vulkan driver against the kumquat server on macOS and
# check that vulkaninfo, vkcube and the dEQP-VK smoke tests come back through
# it.
#
# The two ends live in different trees: the server is built here, from the mesa
# and rutabaga commits this repo pins, while the guest driver is built from a
# mesa checkout with meson. So the mesa source is an input rather than something
# this script can derive.
#
# usage: scripts/verify-macos-guest.sh [--mesa <dir>] [--deqp <dir>] [--host <icd>]
#                                      [--runs <n>]
#
#   --mesa   mesa checkout to build the guest driver from.
#            Defaults to $MESA_SRC, then to a sibling ../mesa checkout.
#   --deqp   VK-GL-CTS build directory holding deqp-vk. Defaults to $DEQP_BUILD,
#            then to a sibling ../VK-GL-CTS/build; skipped when neither exists.
#   --host   ICD manifest the server renders with. Defaults to $KUMQUAT_VK_ICD,
#            then to kosmickrisp from the mesa build when its LLVM dependencies
#            are installed (brew install llvm spirv-llvm-translator libclc),
#            then to MoltenVK.
#   --runs   how many clients to run against the one server. Default 3.
#
# Exits non-zero on the first failure, and leaves no server behind.
#
# Doing the same by hand, after this script has built both sides once:
#
#   # server: serves one client, then exits. VK_ICD_FILENAMES is the driver it
#   # renders with; MoltenVK also wants MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0.
#   DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib \
#   VK_ICD_FILENAMES=<mesa>/build/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.*.json \
#       bazel-bin/external/+git_repository+rutabaga/kumquat_virtio &
#
#   # client: VK_DRIVER_FILES loads the gfxstream ICD, and VIRTGPU_KUMQUAT makes
#   # it talk to the server. Apple has no virtio-gpu backend of its own, so a
#   # client without it finds no device at all.
#   VIRTGPU_KUMQUAT=1 \
#   VK_DRIVER_FILES=<mesa>/build/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.*.json \
#       vulkaninfo --summary
#
# The socket /tmp/kumquat-gpu-0 appears before the renderer is ready; a client
# that connects too early is told there are no blob resources, so wait a moment
# or retry, as this script does.

set -euo pipefail

GFX=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MESA=${MESA_SRC:-$(cd "$GFX/.." && pwd)/mesa}
DEQP=${DEQP_BUILD:-$(cd "$GFX/.." && pwd)/VK-GL-CTS/build}
HOST_ICD=${KUMQUAT_VK_ICD:-}
RUNS=3

while [ $# -gt 0 ]; do
    case "$1" in
        --mesa) MESA=$2; shift 2 ;;
        --deqp) DEQP=$2; shift 2 ;;
        --host) HOST_ICD=$2; shift 2 ;;
        --runs) RUNS=$2; shift 2 ;;
        -h|--help) sed -n '2,43p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

BUILD=$MESA/build
VENV=$MESA/.venv-gfxstream-verify
DEQP_VK=$DEQP/external/vulkancts/modules/vulkan/deqp-vk
MOLTENVK_ICD=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
SOCKET=/tmp/kumquat-gpu-0

# The server dlopens the Vulkan loader and needs an ICD of its own. Without one
# it dies with "Cannot add any library for Vulkan loader", and the guest then
# reports the less obvious "Failed to init virtgpu kumquat".
# A fallback path, because dyld tries DYLD_LIBRARY_PATH even before the absolute
# path an ICD names, which would swap a MoltenVK build for Homebrew's.
export DYLD_FALLBACK_LIBRARY_PATH=${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}

say() { printf '\n== %s\n' "$1"; }
fail() { echo "FAIL: $1" >&2; exit 1; }

# Set once the guest driver is built; every client goes through it.
ICD=
VKCUBE=
# Set once the server is started.
SERVER_PID=
SERVER_LOG=$(mktemp)

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        # Reap it here, so the shell does not report the signal on its own.
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SERVER_LOG"
}
trap cleanup EXIT

# Fails with the server's log when it is no longer running.
server_alive() {
    kill -0 "$SERVER_PID" 2>/dev/null || { cat "$SERVER_LOG" >&2; fail "server $1"; }
}

# Runs a Vulkan program as a gfxstream guest. Apple has no native virtio-gpu
# backend, only a stub, so kumquat is asked for the same way as on every other
# platform.
client() {
    VIRTGPU_KUMQUAT=1 VK_DRIVER_FILES=$ICD "$@"
}

check_prerequisites() {
    say "checking prerequisites"
    [ "$(uname -s)" = Darwin ] || fail "this checks the macOS backend; host is $(uname -s)"
    for tool in bazel python3 vulkaninfo; do
        command -v "$tool" >/dev/null || fail "missing $tool (brew install bazelisk vulkan-tools)"
    done
    # The vkcube on PATH is an open(1) wrapper; run the bundle's binary.
    VKCUBE=$(echo "$(dirname "$(readlink -f "$(command -v vulkaninfo)")")"/../cube/vkcube.app/Contents/MacOS/vkcube)
    [ -x "$VKCUBE" ] || fail "no vkcube binary at $VKCUBE"
    [ -d "$MESA" ] || fail "no mesa checkout at $MESA (pass --mesa)"
}

build_guest_driver() {
    say "building the guest Vulkan driver from $MESA"
    # meson generates with the python it is run under, and that python needs
    # mako, pyyaml and packaging. Putting the venv first on PATH is what makes
    # meson pick it, so this has to happen before `meson setup`, not just
    # before `ninja`.
    [ -d "$VENV" ] || python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q meson ninja mako pyyaml packaging
    export PATH="$VENV/bin:$PATH"

    # kosmickrisp is the host to test against: a Metal driver in this same
    # tree, so a host bug is fixable here. It needs LLVM, so it is built when
    # that is installed and MoltenVK stands in when it is not. It is built even
    # when another host is asked for, so the shared build directory does not
    # lose it.
    local drivers=gfxstream llvm=disabled
    if [ -x /opt/homebrew/opt/llvm/bin/llvm-config ] \
       && [ -d /opt/homebrew/opt/spirv-llvm-translator/lib/pkgconfig ]; then
        export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
        export PKG_CONFIG_PATH="/opt/homebrew/opt/llvm/lib/pkgconfig:/opt/homebrew/opt/spirv-llvm-translator/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
        drivers=gfxstream,kosmickrisp
        llvm=enabled
    fi

    # The Metal WSI needs platforms=macos. Name the source directory, or a
    # build directory that does not exist yet is configured against whatever
    # is current.
    [ -d "$BUILD" ] || meson setup "$BUILD" "$MESA" \
        -Dvulkan-drivers=$drivers \
        -Dgallium-drivers= \
        -Dvirtgpu_kumquat=true \
        -Dplatforms=macos \
        -Dglx=disabled -Dgbm=disabled -Degl=disabled -Dopengl=false -Dllvm=$llvm
    # Older build directories were configured differently.
    meson configure "$BUILD" -Dplatforms=macos -Dvulkan-drivers=$drivers -Dllvm=$llvm >/dev/null
    ninja -C "$BUILD"

    ICD=$(echo "$BUILD"/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.*.json)
    [ -f "$ICD" ] || fail "no ICD manifest under $BUILD"
}

choose_host_driver() {
    if [ -z "$HOST_ICD" ]; then
        HOST_ICD=$(echo "$BUILD"/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.*.json)
        [ -f "$HOST_ICD" ] || HOST_ICD=$MOLTENVK_ICD
    fi
    [ -f "$HOST_ICD" ] || fail "no host ICD at $HOST_ICD (brew install molten-vk)"
    export VK_ICD_FILENAMES=$HOST_ICD

    # MoltenVK pads its Metal argument buffers, and then wants a base type for
    # every descriptor in the set, including one a stage's shader never
    # declares. The compositor's sampler is exactly that to its vertex shader,
    # so every compositor pipeline fails to build. Plain descriptors have no
    # such trouble.
    case "$HOST_ICD" in
        *MoltenVK*) export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0 ;;
    esac
}

build_server() {
    say "building the kumquat server"
    cd "$GFX"
    # Bazel invalidates a fetched git_repository when the patch *list* changes,
    # not when the contents of a patch change. Without this, editing a patch
    # in place and rebuilding silently tests the previous tree.
    bazel fetch --force --repo @mesa --repo @rutabaga
    bazel build @rutabaga//:rutabaga_gfx_kumquat_server
    SERVER=$GFX/$(bazel cquery --output=files @rutabaga//:rutabaga_gfx_kumquat_server 2>/dev/null | tail -1)
    [ -x "$SERVER" ] || fail "no server binary at $SERVER"
}

start_server() {
    say "starting the server on $(basename "$HOST_ICD" | sed 's/_.*//')"
    "$SERVER" >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!

    # The two ends meet on this socket and then swap Mach rights, so its
    # absence means nothing could have connected.
    for _ in $(seq 50); do
        [ -S "$SOCKET" ] && break
        server_alive "exited during startup"
        sleep 0.2
    done
    [ -S "$SOCKET" ] || { cat "$SERVER_LOG" >&2; fail "no rendezvous socket at $SOCKET"; }

    # The socket is bound before the renderer has finished starting, so a
    # client that connects too early is told there are no blob resources. Wait
    # for one to get through before counting any.
    for attempt in $(seq 15); do
        client vulkaninfo --summary >/dev/null 2>&1 && return
        server_alive "exited while starting"
        sleep 1
    done
    cat "$SERVER_LOG" >&2
    fail "server never became ready"
}

run_clients() {
    say "running $RUNS clients against the one server"
    local run output
    for run in $(seq "$RUNS"); do
        output=$(client vulkaninfo --summary 2>&1) \
            || { echo "$output" >&2; fail "vulkaninfo failed on run $run"; }
        echo "$output" | grep -E 'deviceName|driverName' | sed 's/^/  /'
        echo "$output" | grep -q 'driverName *= gfxstream' \
            || { echo "$output" >&2; fail "run $run did not go through gfxstream"; }
        ! echo "$output" | grep -q 'MESA: error' \
            || { echo "$output" | grep 'MESA: error' >&2; fail "run $run logged an error"; }
        server_alive "died on run $run"
    done
}

run_vkcube() {
    say "running vkcube for 60 frames"
    # Unlike vulkaninfo, this maps memory and presents. A window comes and goes.
    local before output
    before=$(grep -c '' "$SERVER_LOG")
    output=$(client "$VKCUBE" --c 60 2>&1) || { echo "$output" >&2; fail "vkcube failed"; }
    tail -n +"$((before + 1))" "$SERVER_LOG" | grep -iE 'error|fatal' \
        && fail "the host logged an error during vkcube"
    server_alive "died during vkcube"
}

run_deqp() {
    if [ ! -x "$DEQP_VK" ]; then
        say "no deqp-vk at $DEQP_VK, skipping the smoke tests"
        return
    fi
    say "running the dEQP-VK smoke and Metal WSI tests"
    # These compare rendered pixels; the wsi ones present through the Metal
    # layer the way vkcube does. deqp-vk needs its data directory as cwd.
    local cases output
    cases='dEQP-VK.api.smoke.*'
    cases="$cases,dEQP-VK.wsi.metal.surface.*,dEQP-VK.wsi.metal.swapchain.create.*"
    cases="$cases,dEQP-VK.wsi.metal.swapchain.acquire.*,dEQP-VK.wsi.metal.swapchain.render.basic"
    output=$(cd "$(dirname "$DEQP_VK")" && client ./deqp-vk --deqp-case="$cases" \
        --deqp-log-filename=/dev/null --deqp-log-images=disable \
        --deqp-log-shader-sources=disable 2>&1) \
        || { echo "$output" >&2; fail "deqp-vk failed to run"; }
    echo "$output" | grep -E '^  (Passed|Failed):' | sed 's/^/  /'
    echo "$output" | grep -qE '^  Failed: +0/' || { echo "$output" >&2; fail "dEQP-VK smoke tests failed"; }
    echo "$output" | grep -qE '^  Passed: +[1-9]' || { echo "$output" >&2; fail "dEQP-VK ran no tests"; }
}

check_nothing_leaked() {
    # A name left in the bootstrap namespace would mean an introduction was not
    # retired, which is what dropping the checked-in right is supposed to do.
    local lingering
    lingering=$(launchctl print "user/$(id -u)" 2>/dev/null | grep -c 'org\.mesa3d\.magma' || true)
    [ "$lingering" -eq 0 ] || fail "$lingering bootstrap names left behind"
}

check_prerequisites
build_guest_driver
choose_host_driver
build_server
start_server
run_clients
run_vkcube
run_deqp
check_nothing_leaked
say "all checks passed: $RUNS clients, vkcube, deqp, one server, no leaks"
