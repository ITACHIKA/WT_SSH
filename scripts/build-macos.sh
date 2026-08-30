#!/usr/bin/env bash
set -euo pipefail

configuration="Release"
credential_backend="auto"
generator="Ninja"
build_directory=""
clean=0
run_tests=1

usage() {
  cat <<'EOF'
Usage: bash scripts/build-macos.sh [options]

Options:
  --config TYPE       Debug, Release, RelWithDebInfo, or MinSizeRel
  --backend NAME      auto, native, keychain, or none
  --build-dir PATH    Build directory (default: build/macos-<config>)
  --generator NAME    CMake generator (default: Ninja)
  --clean             Remove the selected build directory before configuring
  --no-tests          Do not build or run tests
  -h, --help          Show this help
EOF
}

while (($#)); do
  case "$1" in
    --config) configuration="${2:?missing value for --config}"; shift 2 ;;
    --backend) credential_backend="${2:?missing value for --backend}"; shift 2 ;;
    --build-dir) build_directory="${2:?missing value for --build-dir}"; shift 2 ;;
    --generator) generator="${2:?missing value for --generator}"; shift 2 ;;
    --clean) clean=1; shift ;;
    --no-tests) run_tests=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$configuration" in
  Debug|Release|RelWithDebInfo|MinSizeRel) ;;
  *) echo "Invalid configuration: $configuration" >&2; exit 2 ;;
esac
case "$credential_backend" in
  auto|native|keychain|none) ;;
  *) echo "Invalid macOS credential backend: $credential_backend" >&2; exit 2 ;;
esac

script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_directory/.." && pwd -P)"
configuration_lower="$(printf '%s' "$configuration" | tr '[:upper:]' '[:lower:]')"
if [[ -z "$build_directory" ]]; then
  build_directory="$repo_root/build/macos-$configuration_lower"
elif [[ "$build_directory" != /* ]]; then
  build_directory="$repo_root/$build_directory"
fi

if ((clean)) && [[ -e "$build_directory" ]]; then
  resolved_build="$(cd -- "$build_directory" && pwd -P)"
  case "$resolved_build/" in
    "$repo_root/build/"*) rm -rf -- "$resolved_build" ;;
    *) echo "Refusing to clean outside '$repo_root/build': $resolved_build" >&2; exit 2 ;;
  esac
fi

if ((run_tests)); then build_testing="ON"; else build_testing="OFF"; fi

echo "Configuring macOS build ($configuration, backend=$credential_backend)..."
cmake -S "$repo_root" -B "$build_directory" -G "$generator" \
  -DCMAKE_BUILD_TYPE="$configuration" \
  -DWTSSH_CREDENTIAL_BACKEND="$credential_backend" \
  -DBUILD_TESTING="$build_testing"

cmake --build "$build_directory" --config "$configuration"

if ((run_tests)); then
  ctest --test-dir "$build_directory" --build-config "$configuration" --output-on-failure
fi

echo "Build completed: $build_directory/wtssh"
