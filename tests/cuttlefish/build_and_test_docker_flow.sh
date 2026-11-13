#!/usr/bin/env bash
# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

IMAGE_NAME="${IMAGE_NAME:-gfxstream-cuttlefish-ci:local}"
CONTAINER_NAME="gfxstream-cuttlefish-test-$$"
DOCKER_BAZEL_CACHE_DIR="${DOCKER_BAZEL_CACHE_DIR:-${HOME}/.cache/gfxstream-cuttlefish-docker-bazel-cache}"
TEST_TARGET="${TEST_TARGET:-//tests/cuttlefish:gfxstream_guest_angle_host_swiftshader_deqp_smoke}"

mkdir -p "${DOCKER_BAZEL_CACHE_DIR}"

function cleanup() {
  set +e
  if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "Stopping container ${CONTAINER_NAME}..."
    docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

echo "=== 1. Building Docker image ${IMAGE_NAME} ==="
docker build \
  -f "${SCRIPT_DIR}/gfxstream_cuttlefish.dockerfile" \
  -t "${IMAGE_NAME}" \
  "${REPO_ROOT}"

echo "=== 2. Starting Docker container ${CONTAINER_NAME} with mounted workspace ==="
DOCKER_DEVICE_ARGS=(--privileged)
for dev in /dev/kvm /dev/vhost-vsock /dev/vhost-net /dev/net/tun; do
  if [ -e "${dev}" ]; then
    DOCKER_DEVICE_ARGS+=(--device="${dev}")
  fi
done

docker run -d --rm \
  --name "${CONTAINER_NAME}" \
  "${DOCKER_DEVICE_ARGS[@]}" \
  -v "${REPO_ROOT}:/workspace" \
  -v "${DOCKER_BAZEL_CACHE_DIR}:/bazel-disk-cache" \
  -w /workspace \
  "${IMAGE_NAME}" \
  sleep infinity

echo "=== 3. Running ${TEST_TARGET} inside container ==="
docker exec \
  "${CONTAINER_NAME}" \
  bash -c "
    set -e
    if [ -x /etc/init.d/cuttlefish-host-resources ]; then
      /etc/init.d/cuttlefish-host-resources start || true
    fi
    bazel test ${TEST_TARGET} \
      --disk_cache=/bazel-disk-cache \
      --experimental_convenience_symlinks=ignore \
      --test_output=streamed \
      --verbose_failures \
      --test_arg=--cuttlefish-prebuilt-directory-path=/home/kbuilder/cuttlefish-prebuilt
  "

echo "=== Finished successfully! ==="
