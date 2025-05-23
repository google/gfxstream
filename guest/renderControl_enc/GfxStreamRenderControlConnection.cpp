/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GfxStreamRenderControlConnection.h"

GfxStreamRenderControlConnection::GfxStreamRenderControlConnection(
    gfxstream::guest::IOStream* stream) {
    mRcEnc = std::make_unique<ExtendedRCEncoderContext>(stream, &mCheckSumHelper);
}

GfxStreamRenderControlConnection::~GfxStreamRenderControlConnection() {
    // round-trip to ensure that queued commands have been processed
    // before process pipe closure is detected.
    if (mRcEnc) {
        (void)mRcEnc->rcGetRendererVersion(mRcEnc.get());
    }
}

void* GfxStreamRenderControlConnection::getEncoder() { return mRcEnc.get(); }

gfxstream::guest::ChecksumCalculator* GfxStreamRenderControlConnection::getCheckSumHelper() {
    return &mCheckSumHelper;
}
