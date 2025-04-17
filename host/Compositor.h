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

#pragma once

#include <future>
#include <optional>
#include <vector>

#include "gfxstream/host/borrowed_image.h"
#include "Hwc2.h"

namespace gfxstream {

//  Thread hostile and should only be called from the same single thread.
class Compositor {
   public:
    virtual ~Compositor() {}

    struct CompositionRequestLayer {
        std::unique_ptr<BorrowedImageInfo> source;
        ComposeLayer props;
    };

    struct CompositionRequest {
        std::unique_ptr<BorrowedImageInfo> target;
        std::vector<CompositionRequestLayer> layers;
    };

    using CompositionFinishedWaitable = std::shared_future<void>;

    virtual CompositionFinishedWaitable compose(const CompositionRequest& compositionRequest) = 0;

    virtual void onImageDestroyed(uint32_t imageId) {}
};

}  // namespace gfxstream
