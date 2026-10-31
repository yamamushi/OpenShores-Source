#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
daemon_repo_root=$(bash "$repo_root/docker/support/resolve-project-mount.sh" "$repo_root")
image=${HAZERON_LINUX_IMAGE:-hazeron-linux-qt5}
config=${HAZERON_BUILD_CONFIG:-release}
target=${1:-all}

case "$config" in
    debug)
        cmake_build_type=Debug
        ;;
    release)
        cmake_build_type=Release
        ;;
    relwithdebinfo)
        cmake_build_type=RelWithDebInfo
        ;;
    *)
        echo "error: unsupported build configuration: $config" >&2
        exit 2
        ;;
esac

if [[ ! "$target" =~ ^[A-Za-z0-9_.-]+$ ]]; then
    echo "error: invalid CMake target: $target" >&2
    exit 2
fi

cache_volume=${HAZERON_LINUX_CACHE_VOLUME:-hazeron-linux-${config}-cache}
local_output_dir="$repo_root/build/linux-${config}"
daemon_output_dir="$daemon_repo_root/build/linux-${config}"
local_editor_root="$repo_root/build/editor"
daemon_editor_root="$daemon_repo_root/build/editor"
editor_workspace_root=$repo_root
if [[ -x /usr/bin/wslpath ]] && grep -qi microsoft /proc/sys/kernel/osrelease; then
    editor_workspace_root=$(wslpath -m "$repo_root")
fi
mkdir -p "$local_output_dir" "$local_editor_root"

ccache_mount=()
if [[ -n "${HAZERON_CCACHE_DIR:-}" ]]; then
    mkdir -p "$HAZERON_CCACHE_DIR"
    daemon_ccache_dir=$(bash "$repo_root/docker/support/resolve-project-mount.sh" "$HAZERON_CCACHE_DIR")
    ccache_mount=(--mount "type=bind,source=$daemon_ccache_dir,target=/build/ccache")
fi

cd "$repo_root"

# CI can restore a toolchain image keyed by the Docker build context.
if [[ "${HAZERON_LINUX_SKIP_TOOLCHAIN_BUILD:-0}" != 1 ]]; then
    docker build \
        --tag "$image" \
        --target toolchain \
        --file docker/linux/Dockerfile \
        docker/linux
fi

image_id=$(docker image inspect "$image" --format '{{.Id}}')
docker volume create "$cache_volume" >/dev/null
docker run --rm \
    --mount "type=volume,source=$cache_volume,target=/build" \
    "$image" chown "$(id -u):$(id -g)" /build

docker run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env QT_RCC_SOURCE_DATE_OVERRIDE=1 \
    --env CCACHE_DIR=/build/ccache \
    --env CCACHE_MAXSIZE=2G \
    --env CCACHE_COMPILERCHECK=content \
    --env "AU_CMAKE_BUILD_TYPE=$cmake_build_type" \
    --env "AU_IMAGE_ID=$image_id" \
    --env "AU_TARGET=$target" \
    --env "AU_BUILD_JOBS=${HAZERON_BUILD_JOBS:-}" \
    --env "AU_WORKSPACE_ROOT=$editor_workspace_root" \
    --mount "type=bind,source=$daemon_repo_root,target=/src,readonly" \
    --mount "type=volume,source=$cache_volume,target=/build" \
    "${ccache_mount[@]}" \
    --mount "type=bind,source=$daemon_output_dir,target=/output" \
    --mount "type=bind,source=$daemon_editor_root,target=/editor" \
    "$image" \
    /bin/bash -ec '
        cmake \
            -S /src \
            -B /build \
            -G Ninja \
            -DCMAKE_BUILD_TYPE="$AU_CMAKE_BUILD_TYPE" \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
            -DAU_OUTPUT_ROOT=/output
        cmake --build /build \
            --target "$AU_TARGET" \
            --parallel "${AU_BUILD_JOBS:-$(nproc)}"
        python3 /src/docker/support/export-intellisense.py \
            --compile-commands /build/compile_commands.json \
            --compiler c++ \
            --source-root /src \
            --build-root /build \
            --output-root /editor/linux \
            --workspace-root "$AU_WORKSPACE_ROOT" \
            --image-id "$AU_IMAGE_ID"
    '

printf 'Linux artifacts: %s/build/linux-%s\n' "$repo_root" "$config"
printf 'Linux cache volume: %s\n' "$cache_volume"
