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

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime and Bazel toolchain dependencies for Cuttlefish and Android CTS/VTS:
RUN apt-get update && apt-get install -y --no-install-recommends \
    adb \
    apt-transport-https \
    ca-certificates \
    clang \
    clang-tidy \
    curl \
    default-jre-headless \
    git \
    gnupg \
    iproute2 \
    kmod \
    libtinfo5 \
    libvulkan1 \
    python3 \
    sudo \
    udev \
    unzip \
    wget \
    zip \
    && rm -rf /var/lib/apt/lists/*

# Install Bazel:
RUN curl -fsSL https://releases.bazel.build/bazel-release.pub.gpg | gpg --dearmor > /usr/share/keyrings/bazel-archive-keyring.gpg && \
    echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" \
      > /etc/apt/sources.list.d/bazel.list && \
    apt-get update && \
    apt-get install -y bazel && \
    rm -rf /var/lib/apt/lists/*

# Create kbuilder user and groups expected by Cuttlefish:
RUN groupadd -f kvm && \
    groupadd -f render && \
    groupadd -f cvdnetwork && \
    useradd -m -s /bin/bash kbuilder && \
    echo "kbuilder ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Install Cuttlefish host packages (cvd) from Artifact Registry:
RUN curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg \
      -o /etc/apt/trusted.gpg.d/artifact-registry.asc && \
    chmod a+r /etc/apt/trusted.gpg.d/artifact-registry.asc && \
    echo "deb https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish main" \
      > /etc/apt/sources.list.d/artifact-registry.list && \
    apt-get update && \
    apt-get install -y cuttlefish-base cuttlefish-user && \
    usermod -aG kvm,cvdnetwork,render root && \
    usermod -aG kvm,cvdnetwork,render kbuilder && \
    rm -rf /var/lib/apt/lists/*

# Pre-fetch Cuttlefish guest and host artifacts:
RUN mkdir -p /home/kbuilder/cuttlefish-prebuilt && \
    cd /home/kbuilder/cuttlefish-prebuilt && \
    cvd fetch \
      --default_build=aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug \
      --target_directory=/home/kbuilder/cuttlefish-prebuilt && \
    chown -R kbuilder:kbuilder /home/kbuilder/cuttlefish-prebuilt

# Download Android CTS and extract only tools, jdk, and graphics test modules:
RUN mkdir -p /tmp/cts /home/kbuilder/android-cts && \
    curl -fSL https://dl.google.com/dl/android/cts/android-cts-16_r1-linux_x86-x86.zip -o /tmp/cts/cts.zip && \
    unzip -q /tmp/cts/cts.zip \
      "android-cts/tools/*" \
      "android-cts/jdk/*" \
      "android-cts/testcases/CtsDeqpTestCases*" \
      "android-cts/testcases/CtsGraphicsTestCases*" \
      "android-cts/testcases/CtsNativeHardwareTestCases*" \
      -d /home/kbuilder/android-cts && \
    rm -rf /tmp/cts && \
    chown -R kbuilder:kbuilder /home/kbuilder/android-cts

WORKDIR /workspace
