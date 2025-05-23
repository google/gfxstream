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

#include <array>
#include <future>
#include <unordered_map>

#include "gfxstream/host/AstcCpuDecompressor.h"
#include "astcenc.h"

namespace gfxstream {
namespace vk {
namespace {

constexpr uint32_t kNumThreads = 2;

const astcenc_swizzle kSwizzle = {ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

// Used by std::unique_ptr to release the context when the pointer is destroyed
struct AstcencContextDeleter {
    void operator()(astcenc_context* c) { astcenc_context_free(c); }
};

using AstcencContextUniquePtr = std::unique_ptr<astcenc_context, AstcencContextDeleter>;

// Creates a new astcenc_context and wraps it in a smart pointer.
// It is not needed to call astcenc_context_free() on the returned pointer.
// blockWith, blockSize: ASTC block size for the context
// Error: (output param) Where to put the error status. Must not be null.
// Returns nullptr in case of error.
AstcencContextUniquePtr makeDecoderContext(uint32_t blockWidth, uint32_t blockHeight,
                                           astcenc_error* error) {
    astcenc_config config = {};
    *error =
        // TODO(gregschlom): Do we need to pass ASTCENC_PRF_LDR_SRGB here?
        astcenc_config_init(ASTCENC_PRF_LDR, blockWidth, blockHeight, 1, ASTCENC_PRE_FASTEST,
                            ASTCENC_FLG_DECOMPRESS_ONLY, &config);
    if (*error != ASTCENC_SUCCESS) {
        return nullptr;
    }

    astcenc_context* context;
    *error = astcenc_context_alloc(&config, kNumThreads, &context);
    if (*error != ASTCENC_SUCCESS) {
        return nullptr;
    }
    return AstcencContextUniquePtr(context);
}


#if !defined(__clang__) && defined(_MSC_VER)
// AVX2 support detection for Visual Studio
#include <intrin.h>
bool cpuSupportsAvx2()
{
    int data[4];
    __cpuid(data, 0);
    if (data[0] >= 7) {
        __cpuidex(data, 7, 0);
        return data[1] & (1 << 5);  // AVX2 = Bank 7, EBX, bit 5
    }
    return false;
}
#elif defined(__aarch64__)
bool cpuSupportsAvx2()
{
    return false;
}
#else
// AVX2 support detection for GCC and Clang
#include <cpuid.h>
bool cpuSupportsAvx2()
{
    unsigned int data[4];
    if (__get_cpuid_count(7, 0, &data[0], &data[1], &data[2], &data[3])) {
        return data[1] & (1 << 5);  // AVX2 = Bank 7, EBX, bit 5
    }
    return false;
}
#endif

// Returns whether the ASTC decoder can be used on this machine. It might not be available if the
// CPU doesn't support AVX2 instructions for example. Since this call is a bit expensive and never
// changes, the result should be cached.
bool isAstcDecoderAvailable() {
    if (!cpuSupportsAvx2()) return false;
    astcenc_error error;
    // Try getting an arbitrary context. If it works, the decoder is available.
    auto context = makeDecoderContext(5, 5, &error);
    return context != nullptr;
}

// Caches and manages astcenc_context objects.
//
// Each context is fairly large (around 30 MB) and takes a while to construct, so it's important to
// reuse them as much as possible.
//
// While context objects can be reused across multiple threads, they must be used sequentially. To
// avoid having to lock and manage access between threads, we keep one cache per thread. This avoids
// any concurrency issues, at the cost of extra memory.
//
// Currently, there is no eviction strategy. Each cache could grow to a maximum of ~400 MB in size
// since they are 13 possible ASTC block sizes.
//
// Thread-safety: not thread safe.
class AstcDecoderContextCache {
   public:
    // Returns a context object for a given ASTC block size, along with the error code if the
    // context initialization failed.
    // In this case, the context will be null, and the status code will be non-zero.
    std::pair<astcenc_context*, astcenc_error> get(uint32_t blockWidth, uint32_t blockHeight) {
        Value& value = mContexts[{blockWidth, blockHeight}];
        if (value.context == nullptr) {
            value.context = makeDecoderContext(blockWidth, blockHeight, &value.error);
        }
        return {value.context.get(), value.error};
    }

   private:
    // Holds the data we use as the cache key
    struct Key {
        uint32_t blockWidth;
        uint32_t blockHeight;

        bool operator==(const Key& other) const {
            return blockWidth == other.blockWidth && blockHeight == other.blockHeight;
        }
    };

    struct Value {
        AstcencContextUniquePtr context = nullptr;
        astcenc_error error = ASTCENC_SUCCESS;
    };

    // Computes the hash of a Key
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            // blockWidth and blockHeight are < 256 (actually, < 16), so this is safe
            return k.blockWidth << 8 | k.blockHeight;
        }
    };

    std::unordered_map<Key, Value, KeyHash> mContexts;
};

// Thread-safety: all public methods are thread-safe
class WorkerThread {
   public:
    explicit WorkerThread() : mThread(&WorkerThread::main, this) {}

    // Terminates the thread. Call wait() to wait until the thread fully exits.
    void terminate() {
        std::lock_guard lock(mWorkerMutex);
        mTerminated = true;
        mWorkerCondition.notify_one();
    }

    // Blocks until the thread exits.
    void wait() { mThread.join(); }

    std::future<astcenc_error> decompress(astcenc_context* context, uint32_t threadIndex,
                                          const uint8_t* data, size_t dataLength,
                                          astcenc_image* image) {
        std::lock_guard lock(mWorkerMutex);
        mTask = std::packaged_task<astcenc_error()>{[=] {
            return astcenc_decompress_image(context, data, dataLength, image, &kSwizzle,
                                            threadIndex);
        }};
        mWorkerCondition.notify_one();
        return mTask.get_future();
    }

   private:
    // Thread's main loop
    void main() {
        while (true) {
            std::packaged_task<astcenc_error()> task;
            {
                std::unique_lock lock(mWorkerMutex);
                mWorkerCondition.wait(lock, [this] { return mTask.valid() || mTerminated; });
                if (mTerminated) return;
                task = std::move(mTask);
            }
            task();
        }
    }

    bool mTerminated = false;
    std::condition_variable mWorkerCondition = {};  // Signals availability of work
    std::mutex mWorkerMutex = {};                   // Mutex used with mWorkerCondition.
    std::packaged_task<astcenc_error()> mTask = {};
    std::thread mThread = {};
};

// Performs ASTC decompression of an image on the CPU
class AstcCpuDecompressorImpl : public AstcCpuDecompressor {
   public:
    AstcCpuDecompressorImpl()
        : AstcCpuDecompressor(), mContextCache(std::make_unique<AstcDecoderContextCache>()) {}

    ~AstcCpuDecompressorImpl() override {
        // Stop the worker threads, otherwise the process would hang upon exit.
        std::lock_guard global_lock(mMutex);
        for (auto& worker : mWorkerThreads) {
            worker.terminate();
            worker.wait();
        }
    }

    bool available() const override {
        static bool available = isAstcDecoderAvailable();
        return available;
    }

    int32_t decompress(const uint32_t imgWidth, const uint32_t imgHeight, const uint32_t blockWidth,
                       const uint32_t blockHeight, const uint8_t* astcData, size_t astcDataLength,
                       uint8_t* output) override {
        std::array<std::future<astcenc_error>, kNumThreads> futures;

        std::lock_guard global_lock(mMutex);

        auto [context, context_status] = mContextCache->get(blockWidth, blockHeight);
        if (context_status != ASTCENC_SUCCESS) return context_status;

        astcenc_image image = {
            .dim_x = imgWidth,
            .dim_y = imgHeight,
            .dim_z = 1,
            .data_type = ASTCENC_TYPE_U8,
            .data = reinterpret_cast<void**>(&output),
        };

        for (uint32_t i = 0; i < kNumThreads; ++i) {
            futures[i] = mWorkerThreads[i].decompress(context, i, astcData, astcDataLength, &image);
        }

        astcenc_error result = ASTCENC_SUCCESS;

        // Wait for all threads to be done
        for (auto& future : futures) {
            astcenc_error status = future.get();
            if (status != ASTCENC_SUCCESS) {
                result = status;
            }
        }

        astcenc_decompress_reset(context);

        return result;
    }

    const char* getStatusString(int32_t statusCode) const override {
        const char* msg = astcenc_get_error_string((astcenc_error)statusCode);
        return msg ? msg : "ASTCENC_UNKNOWN_STATUS";
    }

   private:
    std::unique_ptr<AstcDecoderContextCache> mContextCache;
    std::mutex mMutex;  // Locked while calling `decompress()`
    std::array<WorkerThread, kNumThreads> mWorkerThreads;
};

}  // namespace

AstcCpuDecompressor& AstcCpuDecompressor::get() {
    static AstcCpuDecompressorImpl instance;
    return instance;
}

}  // namespace vk
}  // namespace gfxstream
