#!/bin/bash

# Fail on errors
set -e
set -x

RUN_DIRECTORY="$(pwd)"
echo "Running from ${RUN_DIRECTORY}"

REPO_DIRECTORY="$(realpath $(dirname $0)/..)"
echo "Repo is in ${REPO_DIRECTORY}"

function gather_test_results() {
  # Don't immediately exit on error anymore
  set +e

  # Copy test outputs to the original directory used by kokoro.
  OUTPUT_DIRECTORY="${RUN_DIRECTORY}"

  for d in "${REPO_DIRECTORY}"/bazel-testlogs/tests/emulator/*; do
    dir="${OUTPUT_DIRECTORY}/$(basename "$d")"
    mkdir -p "${dir}"
    cp "${d}/test.log" "${dir}/sponge_log.log"
    cp "${d}/test.xml" "${dir}/sponge_log.xml"
    if [[ -f "${d}/test.outputs/outputs.zip" ]]; then
      unzip "${d}/test.outputs/outputs.zip" -d "${dir}/kokoro_test_outputs"
    fi
    # Make sure everyone has access to the output files
    chmod -R a+rw "${dir}"
  done
}

# Gather test results regardless of status, but still return the exit code:
trap gather_test_results EXIT

# Move into the Gfxstream repository so that bazel commands work:
cd "${REPO_DIRECTORY}"

# --zip_undeclared_test_outputs triggers the creation of the outputs.zip file
# everything written to $TEST_UNDECLARED_OUTPUTS_DIR is put into this zip
bazel test \
    --test_arg=--cuttlefish-prebuilt-directory-path=/home/kbuilder/cuttlefish-prebuilts/android16-release \
    --test_output=streamed \
    --test_summary=testcase \
    --zip_undeclared_test_outputs \
    tests/cuttlefish/...
