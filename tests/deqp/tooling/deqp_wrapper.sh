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

set -e

DEQP_BINARY=""
DEQP_CASES=""
DEQP_QPA_TO_TEST_XML=""
HOST_DRIVERS_DIR=""
HOST_VULKAN_DRIVER_MANIFEST=""
KUMQUAT_BINARY=""
RENDERER_FEATURES=""
VULKAN_DRIVER=""

for arg in "$@"; do
  case "${arg}" in
    --deqp_binary=*)
      DEQP_BINARY="${arg#*=}"
      ;;
    --deqp_cases=*)
      DEQP_CASES="${arg#*=}"
      ;;
    --deqp_qpa_to_test_xml=*)
      DEQP_QPA_TO_TEST_XML="${arg#*=}"
      ;;
    --host_drivers_dir=*)
      HOST_DRIVERS_DIR="${arg#*=}"
      ;;
    --host_vulkan_driver_manifest=*)
      HOST_VULKAN_DRIVER_MANIFEST="${arg#*=}"
      ;;
    --kumquat_binary=*)
      KUMQUAT_BINARY="${arg#*=}"
      ;;
    --renderer_features=*)
      RENDERER_FEATURES="${arg#*=}"
      ;;
    --vulkan_driver=*)
      VULKAN_DRIVER="${arg#*=}"
      ;;
    *)
      echo "Unknown flag: ${arg}" >&2
      exit 1
      ;;
  esac
done

if [[ -z "${DEQP_BINARY}" ]]; then
  echo "Missing required --deqp_binary flag." >&2
  exit 1
fi
if [[ -z "${DEQP_CASES}" ]]; then
  echo "Missing required --deqp_cases flag." >&2
  exit 1
fi
if [[ -z "${DEQP_QPA_TO_TEST_XML}" ]]; then
  echo "Missing required --deqp_qpa_to_test_xml flag." >&2
  exit 1
fi
if [[ -z "${KUMQUAT_BINARY}" ]]; then
  echo "Missing required --kumquat_binary flag." >&2
  exit 1
fi
if [[ -z "${VULKAN_DRIVER}" ]]; then
  echo "Missing required --vulkan_driver flag." >&2
  exit 1
fi

function get_absolute_path() {
  local p="$1"
  if [[ -z "$p" ]]; then
    echo ""
    return
  fi
  if [[ "$p" != /* ]]; then
    if [ -e "$p" ]; then
      p="$PWD/$p"
    elif [[ -n "${TEST_SRCDIR}" ]] && [ -e "${TEST_SRCDIR}/_main/$p" ]; then
      p="${TEST_SRCDIR}/_main/$p"
    fi
  fi
  echo "$p"
}

function shard_file() {
  local input_file="$1"
  local output_file="$2"
  local shard_index="$3"
  local shard_count="$4"

  # `split` expects a 1-based index (e.g. r/1/2)
  local split_index=$((shard_index + 1))
  split -n "r/${split_index}/${shard_count}" "${input_file}" > "${output_file}"
}

DEQP_BINARY="$(get_absolute_path "${DEQP_BINARY}")"
DEQP_DATA_PATH="$(dirname "${DEQP_BINARY}")/external/vulkancts/data"
DEQP_QPA_TO_TEST_XML="$(get_absolute_path "${DEQP_QPA_TO_TEST_XML}")"
HOST_DRIVERS_DIR="$(get_absolute_path "${HOST_DRIVERS_DIR}")"
HOST_VULKAN_DRIVER_MANIFEST="$(get_absolute_path "${HOST_VULKAN_DRIVER_MANIFEST}")"
KUMQUAT_BINARY="$(get_absolute_path "${KUMQUAT_BINARY}")"
VULKAN_DRIVER="$(get_absolute_path "${VULKAN_DRIVER}")"

echo "dEQP wrapper:"
echo "  - dEQP cases: ${DEQP_CASES}"
echo "  - dEQP binary: ${DEQP_BINARY}"
echo "  - dEQP data path: ${DEQP_DATA_PATH}"
echo "  - dEQP QPA to test.xml converter: ${DEQP_QPA_TO_TEST_XML}"
echo "  - Kumquat binary: ${KUMQUAT_BINARY}"
echo "  - Guest Vulkan driver: ${VULKAN_DRIVER}"
echo "  - Host drivers dir: ${HOST_DRIVERS_DIR}"
echo "  - Host Vulkan driver manifest: ${HOST_VULKAN_DRIVER_MANIFEST}"

if [[ -n "${HOST_DRIVERS_DIR}" ]]; then
  export LD_LIBRARY_PATH="${HOST_DRIVERS_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi
if [[ -n "${HOST_VULKAN_DRIVER_MANIFEST}" ]]; then
  export VK_DRIVER_FILES="${HOST_VULKAN_DRIVER_MANIFEST}"
  export VK_ICD_FILENAMES="${HOST_VULKAN_DRIVER_MANIFEST}"
fi

WORK_DIR="$(mktemp -d "${TEST_TMPDIR:-/tmp}/gfxstream_deqp_vk.XXXXXX")"
DEQP_QPA_FILE="${WORK_DIR}/TestResults.qpa"
KUMQUAT_LOGS="${WORK_DIR}/kumquat_logs.txt"
KUMQUAT_SOCKET="/tmp/kumquat-gpu-0"
KUMQUAT_PID=""

function cleanup() {
  set +e
  if [[ -n "${KUMQUAT_PID}" ]] && kill -0 "${KUMQUAT_PID}" 2>/dev/null; then
    kill "${KUMQUAT_PID}" 2>/dev/null || true
    wait "${KUMQUAT_PID}" 2>/dev/null || true
  fi
  rm -f "${KUMQUAT_SOCKET}"
  if [[ -n "${TEST_UNDECLARED_OUTPUTS_DIR}" ]] && [[ -d "${TEST_UNDECLARED_OUTPUTS_DIR}" ]]; then
    if [ -f "${DEQP_QPA_FILE}" ]; then
      echo "Copying ${DEQP_QPA_FILE} to ${TEST_UNDECLARED_OUTPUTS_DIR}"
      cp "${DEQP_QPA_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    fi
    if [ -f "${KUMQUAT_LOGS}" ]; then
      echo "Copying ${KUMQUAT_LOGS} to ${TEST_UNDECLARED_OUTPUTS_DIR}"
      cp "${KUMQUAT_LOGS}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    fi
  fi
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

DEQP_RAW_CASELIST_FILE="${WORK_DIR}/deqp_raw_caselist.txt"
DEQP_CASELIST_FILE="${WORK_DIR}/deqp_caselist.txt"
DEQP_SHARDED_CASELIST_FILE="${WORK_DIR}/sharded_deqp_caselist.txt"
DEQP_SHADERCACHE_FILE="${WORK_DIR}/shadercache.bin"

"${DEQP_BINARY}" \
  --deqp-runmode=txt-caselist \
  --deqp-archive-dir="${DEQP_DATA_PATH}" \
  --deqp-caselist-export-file="${DEQP_RAW_CASELIST_FILE}" \
  --deqp-shadercache-filename="${DEQP_SHADERCACHE_FILE}" \
  --deqp-log-filename="${WORK_DIR}/caselist_export.qpa" \
  --deqp-case="${DEQP_CASES}"
sed -n 's/^TEST: //p' "${DEQP_RAW_CASELIST_FILE}" > "${DEQP_CASELIST_FILE}"
rm -f "${DEQP_RAW_CASELIST_FILE}" "${WORK_DIR}/caselist_export.qpa"

# Relies entirely on Bazel native sharding env vars:
SHARD_IDX="${TEST_SHARD_INDEX:-0}"
SHARD_CNT="${TEST_TOTAL_SHARDS:-1}"

if [ ! -s "${DEQP_CASELIST_FILE}" ]; then
  echo "Error: dEQP caselist file is empty for cases: ${DEQP_CASES}" >&2
  exit 1
fi

shard_file \
  "${DEQP_CASELIST_FILE}" \
  "${DEQP_SHARDED_CASELIST_FILE}" \
  "${SHARD_IDX}" \
  "${SHARD_CNT}"

# Test sharding requires writing to a status file to verify sharding support
# (https://bazel.build/reference/test-encyclopedia#test-sharding).
if [[ -n "${TEST_SHARD_STATUS_FILE}" ]]; then
  touch "${TEST_SHARD_STATUS_FILE}"
fi

if [ ! -s "${DEQP_SHARDED_CASELIST_FILE}" ]; then
  echo "No tests assigned to shard ${SHARD_IDX} of ${SHARD_CNT}. Gracefully exiting with code 0."
  exit 0
fi

if [[ -n "${TEST_UNDECLARED_OUTPUTS_DIR}" ]] && [[ -d "${TEST_UNDECLARED_OUTPUTS_DIR}" ]]; then
  cp "${DEQP_SHARDED_CASELIST_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
fi

# Launch Kumquat server:
rm -f "${KUMQUAT_SOCKET}"
KUMQUAT_ARGS=(
  "--gpu-socket-path=${KUMQUAT_SOCKET}"
  "--capset-names=gfxstream-vulkan:"
)
if [[ -n "${RENDERER_FEATURES}" ]]; then
  KUMQUAT_ARGS+=("--renderer-features=${RENDERER_FEATURES}")
fi
"${KUMQUAT_BINARY}" "${KUMQUAT_ARGS[@]}" &> "${KUMQUAT_LOGS}" < /dev/null &
KUMQUAT_PID=$!

for _ in $(seq 1 100); do
  if [ -S "${KUMQUAT_SOCKET}" ]; then
    break
  fi
  if ! kill -0 "${KUMQUAT_PID}" 2>/dev/null; then
    echo "Error: Kumquat server exited prematurely:" >&2
    cat "${KUMQUAT_LOGS}" >&2
    exit 1
  fi
  sleep 0.1
done
if [ ! -S "${KUMQUAT_SOCKET}" ]; then
  echo "Error: Timed out waiting for Kumquat socket at ${KUMQUAT_SOCKET}." >&2
  cat "${KUMQUAT_LOGS}" >&2
  exit 1
fi

# Run dEQP:
echo
echo
echo "$(date '+%Y-%m-%d %H:%M:%S') dEQP starting!"

RUN_EXIT_CODE=0
VIRTGPU_KUMQUAT=1 \
VK_LOADER_DEBUG=driver \
"${DEQP_BINARY}" \
  --deqp-vk-library-path="${VULKAN_DRIVER}" \
  --deqp-archive-dir="${DEQP_DATA_PATH}" \
  --deqp-caselist-file="${DEQP_SHARDED_CASELIST_FILE}" \
  --deqp-shadercache-filename="${DEQP_SHADERCACHE_FILE}" \
  --deqp-log-filename="${DEQP_QPA_FILE}" || RUN_EXIT_CODE=$?

echo
echo
echo "$(date '+%Y-%m-%d %H:%M:%S') dEQP finished!"

if [ ! -f "${DEQP_QPA_FILE}" ] || ! grep -q "^#beginTestCaseResult " "${DEQP_QPA_FILE}"; then
  echo "Error: No dEQP test case results recorded in ${DEQP_QPA_FILE}." >&2
  if [ -f "${KUMQUAT_LOGS}" ]; then
    cat "${KUMQUAT_LOGS}" >&2
  fi
  exit 1
fi

# Generate test.xml from QPA file:
echo
echo
if [[ -n "${XML_OUTPUT_FILE}" ]]; then
  echo "Generating test.xml from ${DEQP_QPA_FILE}."
  "${DEQP_QPA_TO_TEST_XML}" \
    --input_qpa_file="${DEQP_QPA_FILE}" \
    --output_test_xml_file="${XML_OUTPUT_FILE}"
else
  echo "No XML output file specified, skipping conversion."
fi

echo
echo
echo "Finished!"
exit "${RUN_EXIT_CODE}"
