#!/usr/bin/env bash

# Exit on failure:
set -e
# Print commands before running:
set -x

# Parse command line flags:
XTS_ARGS=""
XTS_TYPE=""
CUTTLEFISH_CREATE_ARGS=""
CUTTLEFISH_FETCH_ARGS=""
CUTTLEFISH_PREBUILT_DIRECTORY_PATH=""
GFXSTREAM_LIBRARY_PATH=""
XML_CONVERTER_PATH=""
for arg in "$@"; do
  case "${arg}" in
    --cuttlefish-create-args=*)
      CUTTLEFISH_CREATE_ARGS="${arg#*=}"
      ;;
    --cuttlefish-fetch-args=*)
      CUTTLEFISH_FETCH_ARGS="${arg#*=}"
      ;;
    --cuttlefish-prebuilt-directory-path=*)
      CUTTLEFISH_PREBUILT_DIRECTORY_PATH="${arg#*=}"
      ;;
    --gfxstream-library-path=*)
      GFXSTREAM_LIBRARY_PATH="${arg#*=}"
      ;;
    --xml-test-result-converter-path=*)
      XML_CONVERTER_PATH="${arg#*=}"
      ;;
    --xts-args=*)
      XTS_ARGS="${arg#*=}"
      ;;
    --xts-type=*)
      XTS_TYPE="${arg#*=}"
      ;;
    *)
      echo "Unknown flag: ${arg}" >&2
      exit 1
      ;;
  esac
done
if [ -z "${CUTTLEFISH_CREATE_ARGS}" ]; then
  echo "Missing required ----cuttlefish-create-args flag."
  exit 1
fi
if [ -z "${CUTTLEFISH_FETCH_ARGS}" ] && [ -z "${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}"]; then
  echo "Missing required --cuttlefish-fetch-args or --cuttlefish-prebuilt-directory-path flag."
  exit 1
fi
if [ -z "${GFXSTREAM_LIBRARY_PATH}" ]; then
  echo "Missing required --gfxstream-library-path flag."
  exit 1
fi
if [ -z "${XML_CONVERTER_PATH}" ]; then
  echo "Missing required --xml-test-result-converter-path flag."
  exit 1
fi
if [ -z "${XTS_ARGS}" ]; then
  echo "Missing required --cts-args flag."
  exit 1
fi
if [ -z "${XTS_TYPE}" ]; then
  echo "Missing required --xts-type flag."
  exit 1
fi

# Validating command line flags:
CUTTLEFISH_PREBUILT_DIRECTORY_PATH="$(realpath ${CUTTLEFISH_PREBUILT_DIRECTORY_PATH})"
GFXSTREAM_LIBRARY_PATH="$(realpath ${GFXSTREAM_LIBRARY_PATH})"
XML_CONVERTER_PATH="$(realpath ${XML_CONVERTER_PATH})"
echo "Parsed command line args:"
echo "  * CUTTLEFISH_CREATE_ARGS: ${CUTTLEFISH_CONFIG_FILENAME}"
echo "  * CUTTLEFISH_FETCH_ARGS: ${CUTTLEFISH_FETCH_ARGS}"
echo "  * CUTTLEFISH_PREBUILT_DIRECTORY_PATH: ${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}"
echo "  * GFXSTREAM_LIBRARY_PATH: ${GFXSTREAM_LIBRARY_PATH}"
echo "  * XML_CONVERTER_PATH: ${XML_CONVERTER_PATH}"
echo "  * XTS_ARGS: ${XTS_ARGS}"
echo "  * XTS_TYPE: ${XTS_TYPE}"

if [[ -n "${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}" ]]; then
  if [ ! -d "${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}" ]; then
    echo "Cuttlefish prebuilt directory not found at ${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}!"
    exit 1
  fi
fi
if [ ! -f "${GFXSTREAM_LIBRARY_PATH}" ]; then
  echo "Gfxstream backend library not found at ${GFXSTREAM_LIBRARY_PATH}!"
  exit 1
fi

RUN_DIRECTORY="$(pwd)"

# Files to keep track of and save to the final test results directory:
CVD_CREATE_LOG_FILE="${RUN_DIRECTORY}/cvd_create_logs.txt"
CVD_FETCH_LOG_FILE="${RUN_DIRECTORY}/cvd_fetch_logs.txt"
XTS_LOG_FILE="${RUN_DIRECTORY}/cts_logs.txt"


echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Attempting to locate Android ${XTS_TYPE}..."
XTS_DIRECTORY=""
XTS_LATEST_RESULTS_DIRECTORY=""
XTS_RUNNER=""
if [ "${XTS_TYPE}" == "cts" ]; then
  CTS_LATEST_URL="https://dl.google.com/dl/android/cts/android-cts-16_r1-linux_x86-x86.zip"
  CTS_DIRECTORY_POSSIBILITIES=(
    "/home/kbuilder/android-cts"
    "/tmp/android-cts"
  )
  for possible_directory in "${CTS_DIRECTORY_POSSIBILITIES[@]}"; do
    echo "Checking ${possible_directory} ..."
    if [ -d "${possible_directory}" ]; then
      if [ -f "${possible_directory}/android-cts/tools/cts-tradefed" ]; then
        echo "Found Android CTS in ${possible_directory}!"
        XTS_DIRECTORY="${possible_directory}"
        break
      else
        echo "${possible_directory} exists but does not contain cts-tradefed..."
        exit 1
      fi
    else
      echo "${possible_directory} does not exist..."
    fi
  done
  if [ -z "${XTS_DIRECTORY}" ]; then
    echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
    echo "Android CTS directory not found. Downloading Android CTS..."
    XTS_DIRECTORY="/tmp/android-cts"
    mkdir "${XTS_DIRECTORY}"
    wget "${CTS_LATEST_URL}" -p "${XTS_DIRECTORY}"
    unzip "${XTS_DIRECTORY}/*.zip"
    rm "${XTS_DIRECTORY}/*.zip"
    echo "Android CTS now available in ${XTS_DIRECTORY}."
  fi
  XTS_LATEST_RESULTS_DIRECTORY="${XTS_DIRECTORY}/android-cts/results/latest"
  XTS_RUNNER="./android-cts/tools/cts-tradefed"
elif [ "${XTS_TYPE}" == "vts" ]; then
  VTS_DIRECTORY_POSSIBILITIES=(
    "/home/kbuilder/android-vts"
    "/tmp/android-vts"
  )
  for possible_directory in "${VTS_DIRECTORY_POSSIBILITIES[@]}"; do
    echo "Checking ${possible_directory} ..."
    if [ -d "${possible_directory}" ]; then
      if [ -f "${possible_directory}/android-vts/tools/vts-tradefed" ]; then
        echo "Found Android VTS in ${possible_directory}!"
        XTS_DIRECTORY="${possible_directory}"
        break
      else
        echo "${possible_directory} exists but does not contain vts-tradefed..."
        exit 1
      fi
    else
      echo "${possible_directory} does not exist..."
    fi
  done
  if [ -z "${XTS_DIRECTORY}" ]; then
    echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
    echo "Android VTS directory not found. Please build Android XTS locally."
    exit 1
  fi
  XTS_LATEST_RESULTS_DIRECTORY="${XTS_DIRECTORY}/android-vts/results/latest"
  XTS_RUNNER="./android-vts/tools/vts-tradefed"
else
  echo "Unsupported XTS type: ${XTS_TYPE}. Failure."
  exit 1
fi

XTS_LATEST_RESULT_XML="${XTS_LATEST_RESULTS_DIRECTORY}/test_result.xml"
XTS_LATEST_RESULT_CONVERTED_XML="${XTS_LATEST_RESULTS_DIRECTORY}/test.xml"

function collect_logs_and_cleanup() {
    echo "Collecting logs and cleaning up..."

    # Don't immediately exit on failure anymore
    set +e

    if [[ -n "${TEST_UNDECLARED_OUTPUTS_DIR}" ]] && [[ -d "${TEST_UNDECLARED_OUTPUTS_DIR}" ]]; then
        echo "Copying CTS log to test output directory..."
        cp "${XTS_LOG_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"

        echo "Copying CVD log to test output directory..."
        cp "${CVD_CREATE_LOG_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
        cp "${CVD_FETCH_LOG_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"

        echo "Trying to find Cuttlefish host logs..."
        if [[ -n "${CVD_CREATE_LOG_FILE}" ]]; then
          num_cuttlefish_devices_found="$(cvd fleet | grep instance_dir | wc -l)"
          echo "Found ${num_cuttlefish_devices_found} devices..."

          if [[ "${num_cuttlefish_devices_found}" = "1" ]]; then
            echo "Checking for Cuttlefish device instance directory to find logs..."
            cuttlefish_instance_dir="$(cvd fleet 2>&1 | sed -r -n 's|.*"instance_dir" : "(.+)",|\1|p' )"
            echo "Found ${cuttlefish_instance_dir}"

            if [ -d "${cuttlefish_instance_dir}" ]; then
              cuttlefish_launcher_log="${cuttlefish_instance_dir}/logs/launcher.log"
              if [ -f "${cuttlefish_launcher_log}" ]; then
                echo "Copying Cuttlefish host log to test output directory..."
                cp "${cuttlefish_launcher_log}" "${TEST_UNDECLARED_OUTPUTS_DIR}/cuttlefish_launcher.log"
              else
                echo "Failed to find valid launcher log file. Not copying host log."
              fi
            else
              echo "Failed to find valid instance directory. Not copying host log."
            fi
          else
            echo "Failed to find exactly one device. Not copying host log."
          fi
        else
          echo "Create file not found ... assuming test failed before creating a device."
        fi
    fi

    if [[ -n "${XML_OUTPUT_FILE}" ]]; then
        echo "Copying converted XML results for Bazel consumption..."
        cp "${XTS_LATEST_RESULT_CONVERTED_XML}" "${XML_OUTPUT_FILE}"
        echo "Copied!"
    fi

    # Be nice, don't leave devices behind.
    cvd reset -y
}

# Regardless of whether and when a failure occurs logs must collected:
trap collect_logs_and_cleanup EXIT

# Make sure to run in a clean environment by cleaning up old devices:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Cleaning up any existing Cuttlefish devices..."
cvd reset -y

CUTTLEFISH_GUEST_AND_HOST_DIRECTORY=""
if [ -n "${CUTTLEFISH_FETCH_ARGS}" ]; then
  # Fetch Cuttlefish files:
  echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
  echo "Fetching a Cuttlefish host tools and guest images:"
  cvd fetch ${CUTTLEFISH_FETCH_ARGS} \
    2>&1 | tee "${CVD_FETCH_LOG_FILE}"

  CUTTLEFISH_GUEST_AND_HOST_DIRECTORY="${RUN_DIRECTORY}"
else
  echo "Using prebuilts from ${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}."
  CUTTLEFISH_GUEST_AND_HOST_DIRECTORY="${CUTTLEFISH_PREBUILT_DIRECTORY_PATH}"
fi

# Override the existing libgfxstream_backend.so library with the locally built on:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Overriding the existing libgfxstream_backend.so library with the one from ${GFXSTREAM_LIBRARY_PATH}..."
existing_gfxstream_library_paths="$(find ${CUTTLEFISH_GUEST_AND_HOST_DIRECTORY} -name libgfxstream_backend.so)"
echo "Found: ${existing_gfxstream_library_paths}"
for existing_gfxstream_library_path in $existing_gfxstream_library_paths; do
  echo "Overriding ${existing_gfxstream_library_path}"
  cp -f "${GFXSTREAM_LIBRARY_PATH}" "${existing_gfxstream_library_path}"
done

# Create a new Cuttlefish device:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Creating a Cuttlefish device with: ${CUTTLEFISH_CREATE_ARGS}"
cvd create \
  --host_path=${CUTTLEFISH_GUEST_AND_HOST_DIRECTORY} \
  --product_path=${CUTTLEFISH_GUEST_AND_HOST_DIRECTORY} \
  ${CUTTLEFISH_CREATE_ARGS} \
  2>&1 | tee "${CVD_CREATE_LOG_FILE}"

echo "Cuttlefish device created!"

# Run XTS
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Running ${XTS_TYPE}..."
cd "${XTS_DIRECTORY}"
HOME="$(pwd)" \
${XTS_RUNNER} run commandAndExit ${XTS_TYPE} \
    --log-level-display=INFO \
    ${XTS_ARGS} \
    2>&1 | tee "${XTS_LOG_FILE}"
echo "Finished running ${XTS_TYPE}!"

# Convert results to Bazel friendly format:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Converting ${XTS_TYPE} test result output to Bazel XML format..."
python3 ${XML_CONVERTER_PATH} \
    --input_xml_file="${XTS_LATEST_RESULT_XML}" \
    --output_xml_file="${XTS_LATEST_RESULT_CONVERTED_XML}"
echo "Converted!"

# Determine exit code:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Checking if any ${XTS_TYPE} tests failed..."
failures=$(cat ${XTS_LATEST_RESULT_CONVERTED_XML} | grep "<testsuites" | grep -E -o "failures=\"[0-9]+\"")
if [ "${failures}" = "failures=\"0\"" ]; then
  echo "${XTS_TYPE} passed!"
  exit 0
else
  echo "${XTS_TYPE} had failures!"
  exit 1
fi
