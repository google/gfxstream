---
name: gfxstream_deqp_vk
description: Build, run, debug, and iterate on Linux deqp-vk Vulkan CTS tests over Gfxstream (guest vulkan.ranchu.so + host kumquat_virtio + Lavapipe) without an Android guest image. Use this skill whenever running dEQP-VK tests against Gfxstream, diagnosing Vulkan CTS failures, collecting guest/host/QPA logs, or making iterative fixes to Gfxstream guest (external/mesa3d), Gfxstream host (hardware/google/gfxstream), or dEQP (external/deqp). This skill assumes a certain checkout structure to locate gfxstream, mesa3d, and deqp folders relative to the workspace root.
---

# Gfxstream Linux `deqp-vk` Fast-Iteration Skill

This skill enables fast edit-compile-test iterations for Vulkan CTS (`deqp-vk`) over Gfxstream on Linux via `bazel test` without booting an Android guest image.

## Architecture & Key Paths

```
[deqp-vk (@deqp//:deqp-vk, Linux headless target)]
       |  (--deqp-vk-library-path=vulkan.ranchu.so)
       v
[Guest Gfxstream ICD: vulkan.ranchu.so (@mesa//:mesa_gfxstream_guest_vulkan)]  (VIRTGPU_KUMQUAT=1)
       |  (/tmp/kumquat-gpu-0 UNIX socket + shared memory blobs)
       v
[Host Server: kumquat_virtio (@rutabaga//:rutabaga_gfx_kumquat_server)]  (libgfxstream_backend)
       |  (VK_DRIVER_FILES=vk_lavapipe_icd.json or host driver)
       v
[Host Vulkan Driver: Lavapipe / SwiftShader / Host GPU]
```

### Expected Checkout Structure & Source Code Locations
```
<workspace_root>/
├── hardware/google/gfxstream/   # Gfxstream host, Bazel build files, & dEQP test wrapper
├── external/mesa3d/             # Optional local Gfxstream guest Vulkan driver checkout
└── external/deqp/               # Optional local dEQP / Vulkan CTS source checkout
```

- **Gfxstream Repository (Host & Test Wrapper)**: `hardware/google/gfxstream`
  - **Host Vulkan Decoder**: `host/vulkan/` (`VkDecoder.cpp`, `VkDecoderGlobalState.cpp`, `VkSubDecoder.cpp`, etc.)
  - **Host Vulkan Codegen**: `codegen/scripts/` and `host/vulkan/cereal/`
  - **Bazel dEQP Test Macro & Wrapper**:
    - `tests/deqp/tooling/deqp.bzl` (`gfxstream_deqp_vk_test`)
    - `tests/deqp/tooling/deqp_wrapper.sh`
    - `tests/deqp/tooling/deqp_qpa_to_test_xml.py` & `tests/deqp/tooling/deqp_qpa_to_test_xml_main.py`
    - `tests/deqp/BUILD.bazel` (`//tests/deqp:deqp_vk_smoke_tests`)
  - **dEQP Bazel Build Definitions**: `third_party/deqp/`, `third_party/amber/`, `third_party/libpng/`
  - **Mesa Bazel Build Definitions**: `third_party/mesa/` (`mesa.MODULE.bazel`, `BUILD.mesa.bazel`)
- **Gfxstream Guest Vulkan ICD (`vulkan.ranchu.so`)**: `external/mesa3d/src/gfxstream`
  - **Guest Vulkan Driver & Encoder**: `src/gfxstream/guest/vulkan/` and `src/gfxstream/guest/vulkan_enc/` (`ResourceTracker.cpp`, `VkEncoder.cpp`, `gfxstream_vk_device.cpp`, `gfxstream_vk_cmd.cpp`, etc.)
  - **Guest Codegen**: `src/gfxstream/codegen/scripts/`
- **dEQP / Vulkan CTS Source**: `external/deqp`
  - **Vulkan CTS Modules**: `external/vulkancts/modules/vulkan/`
  - **Built-in Mustpass Caselists**: `external/vulkancts/mustpass/main/vk-default/`

---

## Scope & Limitations (CRITICAL)

This harness runs on **Linux host + Linux guest user-space** (`DEQP_SUPPORT_HEADLESS=1` over `kumquat_virtio`), **without** an Android OS framework (`surfaceflinger`, `gralloc`, `libandroid`).

### Supported Test Suites
- Core Vulkan 1.0 / 1.1 / 1.2 / 1.3 API, pipeline, shader, SPIR-V, compute, memory, synchronization, descriptors, dynamic rendering, renderpass, image, texture, subgroups, robustness, YCbCr, and POSIX external memory/semaphore (`opaque_fd`) tests:
  - `dEQP-VK.api.*` (excluding Android-specific subtrees below)
  - `dEQP-VK.pipeline.*`, `dEQP-VK.binding_model.*`, `dEQP-VK.spirv_assembly.*`, `dEQP-VK.glsl.*`
  - `dEQP-VK.memory.*`, `dEQP-VK.synchronization.*`, `dEQP-VK.Synchronization2.*`
  - `dEQP-VK.compute.*`, `dEQP-VK.draw.*`, `dEQP-VK.renderpass*`, `dEQP-VK.dynamic_rendering.*`
  - `dEQP-VK.image.*`, `dEQP-VK.texture.*`, `dEQP-VK.ycbcr.*`, `dEQP-VK.subgroups.*`, `dEQP-VK.robustness.*`

### Unsupported Test Suites (Require Android Guest Image)
Do **NOT** use this harness to validate Android-only extensions; they will report `NotSupported` or fail to find Android symbols:
1. **Android Hardware Buffers (AHB)**:
   - `VK_ANDROID_external_memory_android_hardware_buffer`
   - `dEQP-VK.api.external.memory.android_hardware_buffer.*`
2. **Android Window System Integration (WSI)**:
   - `VK_KHR_android_surface`, `ANativeWindow`, `VK_ANDROID_native_buffer`
   - `dEQP-VK.wsi.android.*`
3. **Android Native Sync FD / Gralloc**:
   - Android-specific fence/semaphore FDs requiring Android `libsync` or `gralloc` HALs.

---

## Workflow Commands

Always run commands with `Cwd` set to `<workspace_root>/hardware/google/gfxstream`.
Bazel automatically fetches and builds `@deqp//:deqp-vk`, `@deqp//:data`, `@mesa//:mesa_gfxstream_guest_vulkan`, and `@rutabaga//:rutabaga_gfx_kumquat_server`.

### 1. Running Tests via `bazel test`

```bash
# Run default smoke tests (dEQP-VK.api.smoke.*) against host Vulkan driver:
bazel test //tests/deqp:deqp_vk_smoke_tests \
  --test_output=streamed

# Run smoke tests against hermetic Lavapipe software Vulkan driver:
bazel test //tests/deqp:deqp_vk_smoke_tests \
  --graphics_drivers=gles_angle_vulkan_lavapipe \
  --test_output=streamed

# Run a single test or custom comma-separated wildcard pattern via --test_arg:
bazel test //tests/deqp:deqp_vk_smoke_tests \
  --graphics_drivers=gles_angle_vulkan_lavapipe \
  --test_output=streamed \
  --test_arg=--deqp_cases="dEQP-VK.api.smoke.triangle"

# Pass custom Kumquat --renderer-features (e.g. VulkanValidation:print):
bazel test //tests/deqp:deqp_vk_smoke_tests \
  --graphics_drivers=gles_angle_vulkan_lavapipe \
  --test_output=streamed \
  --test_arg=--renderer_features="VulkanValidation:print" \
  --test_arg=--deqp_cases="dEQP-VK.api.smoke.triangle"
```

### 2. Defining Sharded Test Targets with `gfxstream_deqp_vk_test`

`gfxstream_deqp_vk_test` in `tests/deqp/tooling/deqp.bzl` supports native Bazel test sharding (`shard_count` / `TEST_TOTAL_SHARDS` / `TEST_SHARD_INDEX`):

```python
gfxstream_deqp_vk_test(
    name = "deqp_vk_smoke_tests",
    deqp_cases = ["dEQP-VK.api.smoke.*"],
)
```

### 3. Iterating Against Local `external/mesa3d` or `external/deqp` Checkouts

By default, `third_party/mesa/mesa.MODULE.bazel` and `third_party/deqp/deqp.MODULE.bazel` pin `@mesa` and `@deqp` to hermetic upstream commits.
To build and test against local source modifications in `<workspace_root>/external/mesa3d` or `<workspace_root>/external/deqp`:

#### Local `@mesa` (`<workspace_root>/external/mesa3d`):
1. Ensure the testing patch (`third_party/mesa/PATCH.0001_disable_vk_envvar_modification_for_testing.patch`) is applied and `BUILD.bazel` is symlinked to `third_party/mesa/BUILD.mesa.bazel`:
   ```bash
   ln -sf $(pwd)/third_party/mesa/BUILD.mesa.bazel ../../external/mesa3d/BUILD.bazel
   git -C ../../external/mesa3d apply --check $(pwd)/third_party/mesa/PATCH.0001_disable_vk_envvar_modification_for_testing.patch 2>/dev/null && \
     git -C ../../external/mesa3d apply $(pwd)/third_party/mesa/PATCH.0001_disable_vk_envvar_modification_for_testing.patch || true
   ```
2. Pass `--override_repository=+git_repository+mesa=$(realpath ../../external/mesa3d)` to `bazel test`:
   ```bash
   bazel test //tests/deqp:deqp_vk_smoke_tests \
     --graphics_drivers=gles_angle_vulkan_lavapipe \
     --override_repository=+git_repository+mesa=$(realpath ../../external/mesa3d) \
     --test_output=streamed \
     --test_arg=--deqp_cases="dEQP-VK.api.smoke.triangle"
   ```
   *(Remember to remove the temporary `BUILD.bazel` symlink and revert the patch in `external/mesa3d` before committing changes there.)*

#### Local `@deqp` (`<workspace_root>/external/deqp`):
1. Symlink `third_party/deqp/BUILD.deqp.bazel` to `../../external/deqp/BUILD.bazel`:
   ```bash
   ln -sf $(pwd)/third_party/deqp/BUILD.deqp.bazel ../../external/deqp/BUILD.bazel
   ```
2. Pass `--override_repository=+git_repository+deqp=$(realpath ../../external/deqp)` to `bazel test`:
   ```bash
   bazel test //tests/deqp:deqp_vk_smoke_tests \
     --graphics_drivers=gles_angle_vulkan_lavapipe \
     --override_repository=+git_repository+deqp=$(realpath ../../external/deqp) \
     --test_output=streamed \
     --test_arg=--deqp_cases="dEQP-VK.api.smoke.triangle"
   ```

---

## Collecting & Analyzing Logs

Every `bazel test //tests/deqp:deqp_vk_smoke_tests` invocation writes test results and diagnostic artifacts under `bazel-testlogs/tests/deqp/deqp_vk_smoke_tests/`:

1. **`test.xml`**:
   - JUnit XML converted from `TestResults.qpa` by `deqp_qpa_to_test_xml_main`, listing per-case pass/fail/skip/crash status and failure details.
2. **`test.log`**:
   - Captures stdout/stderr from `deqp_wrapper.sh`, `deqp-vk`, and guest `vulkan.ranchu.so` (`mesa_loge`, `mesa_logi`, guest encoder errors, VirtGpuKumquat messages).
3. **`test.outputs/`** (undeclared test outputs, or `test.outputs/outputs.zip` when zipped):
   - **`TestResults.qpa`**: Raw dEQP QPA log file for all executed test cases.
   - **`kumquat_logs.txt`**: Captures stdout/stderr from `kumquat_virtio`, Gfxstream host Vulkan decoder (`VkDecoder`, `FrameBuffer`, `VkCommonOperations`), and Lavapipe/SwiftShader/host driver messages.
   - **`sharded_deqp_caselist.txt`**: Exact list of dEQP test cases assigned to the shard.

---

## Recommended AI Debugging Loop

1. **Reproduce**: Run the failing test case in isolation:
   ```bash
   bazel test //tests/deqp:deqp_vk_smoke_tests \
     --graphics_drivers=gles_angle_vulkan_lavapipe \
     --test_output=streamed \
     --test_arg=--deqp_cases="dEQP-VK.<failing_test>"
   ```
2. **Inspect Logs**:
   - Check `bazel-testlogs/tests/deqp/deqp_vk_smoke_tests/test.xml`, `test.log`, and `test.outputs/` (`TestResults.qpa`, `kumquat_logs.txt`) for dEQP failure details and guest encoder vs. host decoder errors.
3. **Modify Code**:
   - For host-side Vulkan decoding/emulation issues: edit files under `hardware/google/gfxstream/host/vulkan/`.
   - For guest-side Vulkan encoding/resource tracking issues: edit files under `external/mesa3d/src/gfxstream/guest/` and run with `--override_repository=+git_repository+mesa=...`.
   - For dEQP test adjustments or extra debug logging in CTS: edit files under `external/deqp/external/vulkancts/` and run with `--override_repository=+git_repository+deqp=...`.
4. **Rebuild & Verify**:
   Re-run the `bazel test` command above, then run the broader module wildcard (e.g. `--test_arg=--deqp_cases="dEQP-VK.<module>.*"`) to ensure no regressions.
