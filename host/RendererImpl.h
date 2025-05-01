// Copyright (C) 2016 The Android Open Source Project
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
#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

#include "RenderThread.h"
#include "RenderWindow.h"
#include "aemu/base/threads/FunctorThread.h"
#include "gfxstream/host/Features.h"
#include "render-utils/Renderer.h"

namespace android_studio {
    class EmulatorGLESUsages;
}

namespace gfxstream {

class RendererImpl final : public Renderer {
public:
    RendererImpl();
    ~RendererImpl();

    bool initialize(int width, int height, const gfxstream::host::FeatureSet& features, bool useSubWindow,
                    bool egl2egl);
    void stop(bool wait) override;
    void finish() override;

public:
    RenderChannelPtr createRenderChannel(
        gfxstream::Stream* loadStream, uint32_t virtioGpuContextId) final;

    void* addressSpaceGraphicsConsumerCreate(
        struct asg_context,
        gfxstream::Stream* stream,
        android::emulation::asg::ConsumerCallbacks,
        uint32_t contextId, uint32_t capsetId,
        std::optional<std::string> name) override final;
    void addressSpaceGraphicsConsumerDestroy(void*) override final;
    void addressSpaceGraphicsConsumerPreSave(void* consumer) override final;
    void addressSpaceGraphicsConsumerSave(void* consumer, gfxstream::Stream* stream) override final;
    void addressSpaceGraphicsConsumerPostSave(void* consumer) override final;
    void addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(void* consumer) override final;

    HardwareStrings getHardwareStrings() final;
    void setPostCallback(OnPostCallback onPost,
                         void* context,
                         bool useBgraReadback,
                         uint32_t displayId) final;
    bool asyncReadbackSupported() final;
    ReadPixelsCallback getReadPixelsCallback() final;
    FlushReadPixelPipeline getFlushReadPixelPipeline() final;
    bool showOpenGLSubwindow(FBNativeWindowType window,
                             int wx,
                             int wy,
                             int ww,
                             int wh,
                             int fbw,
                             int fbh,
                             float dpr,
                             float zRot,
                             bool deleteExisting,
                             bool hideWindow) final;
    bool destroyOpenGLSubwindow() final;
    void setOpenGLDisplayRotation(float zRot) final;
    void setOpenGLDisplayTranslation(float px, float py) final;
    void repaintOpenGLDisplay() final;

    bool hasGuestPostedAFrame() final;
    void resetGuestPostedAFrame() final;

    void setScreenMask(int width, int height, const unsigned char* rgbaData) final;
    void setMultiDisplay(uint32_t id,
                         int32_t x,
                         int32_t y,
                         uint32_t w,
                         uint32_t h,
                         uint32_t dpi,
                         bool add) final;
    void setMultiDisplayColorBuffer(uint32_t id, uint32_t cb) override;
    void onGuestGraphicsProcessCreate(uint64_t puid) final;
    // TODO(kaiyili): rename this interface to onGuestGraphicsProcessDestroy.
    void cleanupProcGLObjects(uint64_t puid) final;
    void waitForProcessCleanup() final;
    struct AndroidVirtioGpuOps* getVirtioGpuOps() final;

    void pauseAllPreSave() final;
    void resumeAll() final;

    void save(gfxstream::Stream* stream,
              const ITextureSaverPtr& textureSaver) final;
    bool load(gfxstream::Stream* stream,
              const ITextureLoaderPtr& textureLoader) final;
    void fillGLESUsages(android_studio::EmulatorGLESUsages*) final;
    int getScreenshot(unsigned int nChannels, unsigned int* width, unsigned int* height,
                      uint8_t* pixels, size_t* cPixels, int displayId, int desiredWidth,
                      int desiredHeight, int desiredRotation, Rect rect) final;

    void preLoad() override;
    void postLoad() override;

    void addListener(FrameBufferChangeEventListener* listener) override;
    void removeListener(FrameBufferChangeEventListener* listener) override;

    void setVsyncHz(int vsyncHz) final;
    void setDisplayConfigs(int configId, int w, int h, int dpiX, int dpiY) override;
    void setDisplayActiveConfig(int configId) override;

    const void* getEglDispatch() override;
    const void* getGles2Dispatch() override;

    void setShouldSkipDraw(bool skip) override;
    bool getShouldSkipDraw() const override;

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(RendererImpl);

private:
    // Stop all render threads and wait until they exit,
    // and also delete them.
    void cleanupRenderThreads();

    std::unique_ptr<RenderWindow> mRenderWindow;

    std::mutex mChannelsMutex;
    std::vector<std::shared_ptr<RenderChannelImpl>> mChannels;
    std::vector<std::shared_ptr<RenderChannelImpl>> mStoppedChannels;
    bool mStopped = false;

    class ProcessCleanupThread;
    std::unique_ptr<ProcessCleanupThread> mCleanupThread;

    std::unique_ptr<RenderThread> mLoaderRenderThread;

    std::vector<RenderThread*> mAdditionalPostLoadRenderThreads;

    std::mutex mAddressSpaceRenderThreadMutex;
    std::unordered_set<RenderThread*> mAddressSpaceRenderThreads;
};

}  // namespace gfxstream
