#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENMV_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PORT_DIR="${ESP_OPENMV_PORT_DIR:-}"

OPENMV_BASE_COMMIT="2c24e0b7812cab1f22ed4fc0c68c67fcea6cecb0"
MICROPYTHON_BASE_COMMIT="366b6bd242fa068e4ee03a5c516e8cfa7d10c374"

usage() {
    cat <<EOF
Usage: tools/update_esp_openmv_port_patches.sh --port-dir DIR [options]

Generate esp-openmv-port patch files from the current OpenMV development tree.

Options:
  -p, --port-dir DIR          esp-openmv-port repository directory.
  -C, --openmv-dir DIR        OpenMV development tree. Default: this repository.
      --openmv-base COMMIT    OpenMV base commit. Default: ${OPENMV_BASE_COMMIT}
      --micropython-base COMMIT
                              MicroPython base commit. Default: ${MICROPYTHON_BASE_COMMIT}
  -h, --help                  Show this help.

Generated files:
  DIR/patches/openmv-core.patch
  DIR/patches/micropython-core.patch

Board files and ESP32 port files that are maintained under esp-openmv-port/overlay
are intentionally excluded from these patches.
EOF
}

die() {
    echo "error: $*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -p|--port-dir)
            shift
            PORT_DIR="${1:-}"
            ;;
        -C|--openmv-dir)
            shift
            OPENMV_DIR="${1:-}"
            ;;
        --openmv-base)
            shift
            OPENMV_BASE_COMMIT="${1:-}"
            ;;
        --micropython-base)
            shift
            MICROPYTHON_BASE_COMMIT="${1:-}"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
    shift
done

if [ -z "${PORT_DIR}" ]; then
    if [ -d "${OPENMV_DIR}/../esp-openmv-port" ]; then
        PORT_DIR="${OPENMV_DIR}/../esp-openmv-port"
    else
        die "missing --port-dir DIR"
    fi
fi

OPENMV_DIR="$(cd "${OPENMV_DIR}" && pwd)"
PORT_DIR="$(cd "${PORT_DIR}" && pwd)"
MICROPYTHON_DIR="${OPENMV_DIR}/lib/micropython"
PATCH_DIR="${PORT_DIR}/patches"

[ -f "${OPENMV_DIR}/Makefile" ] || die "not an OpenMV tree: ${OPENMV_DIR}"
git -C "${MICROPYTHON_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
    || die "missing MicroPython git tree: ${MICROPYTHON_DIR}"
git -C "${OPENMV_DIR}" rev-parse --verify "${OPENMV_BASE_COMMIT}^{commit}" >/dev/null \
    || die "OpenMV base commit not found: ${OPENMV_BASE_COMMIT}"
git -C "${MICROPYTHON_DIR}" rev-parse --verify "${MICROPYTHON_BASE_COMMIT}^{commit}" >/dev/null \
    || die "MicroPython base commit not found: ${MICROPYTHON_BASE_COMMIT}"

mkdir -p "${PATCH_DIR}"

openmv_paths=(
    Makefile
    micropython.cmake
    common/fb_alloc.h
    common/mp_utils.c
    common/omv_debug.h
    common/tinyusb_debug.c
    common/usbdbg.c
    lib/cmsis/include/cmsis_compiler.h
    lib/cmsis/include/cmsis_extension.h
    lib/cmsis/include/cmsis_gcc.h
    lib/imlib/apriltag.c
    lib/imlib/clahe.c
    lib/imlib/imlib.h
    lib/imlib/jpegd.c
    lib/imlib/mathop.c
    modules/py_display.c
    modules/py_display.h
    modules/py_image.c
    scripts/libraries/_boot.py
)

micropython_paths=(
    py/makeqstrdefs.py
    py/mkrules.cmake
    ports/esp32/machine_sdcard.c
    ports/esp32/qstrdefsport.h
    shared/tinyusb/tusb_config.h
)

openmv_patch_tmp="${PATCH_DIR}/openmv-core.patch.tmp"
micropython_patch_tmp="${PATCH_DIR}/micropython-core.patch.tmp"

git -C "${OPENMV_DIR}" diff --binary "${OPENMV_BASE_COMMIT}" -- "${openmv_paths[@]}" > "${openmv_patch_tmp}"
git -C "${MICROPYTHON_DIR}" diff --binary "${MICROPYTHON_BASE_COMMIT}" -- "${micropython_paths[@]}" > "${micropython_patch_tmp}"

if grep -E '(^diff --git a/.*lockfiles/|dependencies\.lock|ports/esp32/\.gitignore)' \
    "${openmv_patch_tmp}" "${micropython_patch_tmp}" >/dev/null; then
    rm -f "${openmv_patch_tmp}" "${micropython_patch_tmp}"
    die "generated patch unexpectedly contains lockfile or .gitignore changes"
fi

mv "${openmv_patch_tmp}" "${PATCH_DIR}/openmv-core.patch"
mv "${micropython_patch_tmp}" "${PATCH_DIR}/micropython-core.patch"

echo "updated ${PATCH_DIR}/openmv-core.patch"
grep -E '^diff --git ' "${PATCH_DIR}/openmv-core.patch" || true
wc -l "${PATCH_DIR}/openmv-core.patch"

echo "updated ${PATCH_DIR}/micropython-core.patch"
grep -E '^diff --git ' "${PATCH_DIR}/micropython-core.patch" || true
wc -l "${PATCH_DIR}/micropython-core.patch"
