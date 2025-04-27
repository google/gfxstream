// Copyright 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "CompositorGl.h"

#include "BorrowedImageGl.h"
#include "DebugGl.h"
#include "DisplaySurfaceGl.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "TextureDraw.h"
#include "gfxstream/host/logging.h"

namespace gfxstream {
namespace gl {
namespace {

const BorrowedImageInfoGl* getInfoOrAbort(const std::unique_ptr<BorrowedImageInfo>& info) {
    auto imageGl = static_cast<const BorrowedImageInfoGl*>(info.get());
    if (imageGl != nullptr) {
        return imageGl;
    }

    GFXSTREAM_FATAL("CompositorGl did not find BorrowedImageInfoGl");
    return nullptr;
}

std::shared_future<void> getCompletedFuture() {
    std::shared_future<void> completedFuture = std::async(std::launch::deferred, [] {}).share();
    completedFuture.wait();
    return completedFuture;
}

}  // namespace

CompositorGl::CompositorGl(TextureDraw* textureDraw) : m_textureDraw(textureDraw) {}

CompositorGl::~CompositorGl() {}

Compositor::CompositionFinishedWaitable CompositorGl::compose(
        const CompositionRequest& composeRequest) {
    const auto* targetImage = getInfoOrAbort(composeRequest.target);
    const uint32_t targetWidth = targetImage->width;
    const uint32_t targetHeight = targetImage->height;
    const GLuint targetTexture = targetImage->texture;
    GL_SCOPED_DEBUG_GROUP("CompositorGl::compose() into texture:%d", targetTexture);

    GLint restoredViewport[4] = {0, 0, 0, 0};
    s_gles2.glGetIntegerv(GL_VIEWPORT, restoredViewport);

    s_gles2.glViewport(0, 0, targetWidth, targetHeight);
    if (!m_composeFbo) {
        s_gles2.glGenFramebuffers(1, &m_composeFbo);
    }
    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, m_composeFbo);
    s_gles2.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D,
                                   targetTexture,
                                   /*level=*/0);

    m_textureDraw->prepareForDrawLayer();

    for (const CompositionRequestLayer& layer : composeRequest.layers) {
        if (layer.props.composeMode == HWC2_COMPOSITION_DEVICE) {
            const BorrowedImageInfoGl* layerImage = getInfoOrAbort(layer.source);
            const GLuint layerTexture = layerImage->texture;
            GL_SCOPED_DEBUG_GROUP("CompositorGl::compose() from layer texture:%d", layerTexture);
            m_textureDraw->drawLayer(layer.props, targetWidth, targetHeight, layerImage->width,
                                     layerImage->height, layerTexture);
        } else {
            m_textureDraw->drawLayer(layer.props, targetWidth, targetHeight, 1, 1, 0);
        }
    }

    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s_gles2.glViewport(restoredViewport[0], restoredViewport[1], restoredViewport[2],
                       restoredViewport[3]);

    m_textureDraw->cleanupForDrawLayer();

    targetImage->onCommandsIssued();

    // Note: This should be returning a future when all work, both CPU and GPU, is
    // complete but is currently only returning a future when all CPU work is completed.
    // In the future, CompositionFinishedWaitable should be replaced with something that
    // passes along a GL fence or VK fence.
    return getCompletedFuture();
}

}  // namespace gl
}  // namespace gfxstream