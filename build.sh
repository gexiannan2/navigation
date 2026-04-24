#!/bin/sh

set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD_DIR="${1:-build}"
JOBS="${JOBS:-4}"

echo "Project root: ${ROOT_DIR}"
echo "Build dir: ${BUILD_DIR}"
echo "Jobs: ${JOBS}"

cmake --fresh -S "${ROOT_DIR}" -B "${ROOT_DIR}/${BUILD_DIR}"
cmake --build "${ROOT_DIR}/${BUILD_DIR}" -j"${JOBS}"

echo
echo "Build finished."
echo "Artifacts:"
echo "  ${ROOT_DIR}/${BUILD_DIR}/grid_map_demo"
echo "  ${ROOT_DIR}/${BUILD_DIR}/navmesh_dump"
echo "  ${ROOT_DIR}/${BUILD_DIR}/navigation_load_by_name_demo"
echo "  ${ROOT_DIR}/Bin/lib/libnavigation.so"
