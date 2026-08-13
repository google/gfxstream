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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "compositor_gl.h"

#include "OpenGLESDispatch/DispatchTables.h"
#include "color_buffer_gl.h"
#include "debug_gl.h"
#include "display_surface_gl.h"
#include "gfxstream/common/logging.h"
#include "texture_draw.h"

namespace gfxstream {
namespace host {
namespace gl {
namespace {

std::shared_future<void> getCompletedFuture() {
    std::shared_future<void> completedFuture = std::async(std::launch::deferred, [] {}).share();
    completedFuture.wait();
    return completedFuture;
}

}  // namespace

CompositorGl::CompositorGl(TextureDraw* textureDraw) : m_textureDraw(textureDraw) {
    if(!m_textureDraw) {
        GFXSTREAM_FATAL("CompositorGl requires a valid TextureDraw object!");
    }
}

CompositorGl::~CompositorGl() {}

Compositor::CompositionFinishedWaitable CompositorGl::compose(
        const CompositionRequest& composeRequest) {
    auto targetCb = composeRequest.target;
    if (!targetCb) {
        GFXSTREAM_ERROR("CompositorGl::compose: target is null");
        return getCompletedFuture();
    }
    targetCb->invalidateForBackend(Backend::GL);
    auto targetCbGl = targetCb->getColorBufferGl();
    if (!targetCbGl) {
        GFXSTREAM_ERROR("CompositorGl::compose: target is not GL ColorBuffer");
        return getCompletedFuture();
    }

    const uint32_t targetWidth = targetCbGl->getWidth();
    const uint32_t targetHeight = targetCbGl->getHeight();
    const GLuint targetTexture = targetCbGl->getTexture();
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
            auto layerCb = layer.source;
            if (!layerCb) {
                continue;
            }
            layerCb->invalidateForBackend(Backend::GL);
            auto layerCbGl = layerCb->getColorBufferGl();
            if (!layerCbGl) {
                continue;
            }

            const GLuint layerTexture = layerCbGl->getTexture();
            GL_SCOPED_DEBUG_GROUP("CompositorGl::compose() from layer texture:%d", layerTexture);
            m_textureDraw->drawLayer(layer.props, targetWidth, targetHeight, layerCbGl->getWidth(),
                                     layerCbGl->getHeight(), layerTexture);
        } else {
            m_textureDraw->drawLayer(layer.props, targetWidth, targetHeight, 1, 1, 0);
        }
    }

    s_gles2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    s_gles2.glViewport(restoredViewport[0], restoredViewport[1], restoredViewport[2],
                       restoredViewport[3]);

    m_textureDraw->cleanupForDrawLayer();

    targetCbGl->setSync();

    // Note: This should be returning a future when all work, both CPU and GPU, is
    // complete but is currently only returning a future when all CPU work is completed.
    // In the future, CompositionFinishedWaitable should be replaced with something that
    // passes along a GL fence or VK fence.
    return getCompletedFuture();
}

void CompositorGl::setScreenMask(int width, int height, const uint8_t* rgbaData) {
    m_textureDraw->setScreenMask(width, height, rgbaData);
}
void CompositorGl::setScreenBackground(int width, int height, const uint8_t* rgbaData) {
    m_textureDraw->setScreenBackground(width, height, rgbaData);
}

}  // namespace gl
}  // namespace host
}  // namespace gfxstream