/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <functional>
#include <future>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Compositor.h"
#include "Hwc2.h"
#include "PostCommands.h"
#include "aemu/base/Compiler.h"
#include "aemu/base/synchronization/Lock.h"
#include "aemu/base/synchronization/MessageChannel.h"
#include "host-common/window_agent.h"

namespace gfxstream {
class ColorBuffer;
class FrameBuffer;
struct RenderThreadInfo;

class PostWorker {
   public:
    PostWorker(bool mainThreadPostingOnly, FrameBuffer* fb, Compositor* compositor);
    virtual ~PostWorker();

    // post: posts the next color buffer.
    // Assumes framebuffer lock is held.
    void post(ColorBuffer* cb, std::unique_ptr<Post::CompletionCallback> postCallback);

    // viewport: (re)initializes viewport dimensions.
    // Assumes framebuffer lock is held.
    // This is called whenever the subwindow needs a refresh
    // (FrameBuffer::setupSubWindow).
    void viewport(int width, int height);

    // compose: compse the layers into final framebuffer. The callback will be
    // called when the CPU side job completes. The passed in future in the
    // callback will be completed when the GPU opereation completes.
    void compose(std::unique_ptr<FlatComposeRequest> composeRequest,
                 std::unique_ptr<Post::CompletionCallback> composeCallback);

    // clear: blanks out emulator display when refreshing the subwindow
    // if there is no last posted color buffer to show yet.
    void clear();

    virtual void screenshot(ColorBuffer* cb, int screenwidth, int screenheight, GLenum format,
                            GLenum type, int skinRotation, void* pixels, Rect rect) = 0;

    // The block task will set the scheduledSignal promise when the task is scheduled, and wait
    // until continueSignal is ready before completes.
    void block(std::promise<void> scheduledSignal, std::future<void> continueSignal);

    // Exit post worker, unbind gl context if necessary.
    void exit();

   protected:
    void runTask(std::packaged_task<void()>);
    // Impl versions of the above, so we can run it from separate threads
    virtual std::shared_future<void> postImpl(ColorBuffer* cb) = 0;
    virtual void viewportImpl(int width, int height) = 0;
    virtual void clearImpl() = 0;
    virtual void exitImpl() = 0;
    virtual std::shared_future<void> composeImpl(const FlatComposeRequest& composeRequest);

   protected:
    FrameBuffer* mFb;
    Compositor* m_compositor = nullptr;

   protected:
    // If m_mainThreadPostingOnly is true, schedule the task to UI thread by
    // using m_runOnUiThread. Otherwise, execute the task on the current thread.
    bool m_mainThreadPostingOnly = false;

   private:
    using UiThreadRunner = std::function<void(UiUpdateFunc, void*, bool)>;

    UiThreadRunner m_runOnUiThread = 0;

    std::unordered_map<uint32_t, std::shared_future<void>> m_composeTargetToComposeFuture;

    bool isComposeTargetReady(uint32_t targetHandle);

    DISALLOW_COPY_AND_ASSIGN(PostWorker);
};

}  // namespace gfxstream
