#pragma once

#include <memory>
#include <string>
#include <unordered_set>

#include "gfxstream/host/features.h"
#include "vulkan/vulkan_core.h"

namespace gfxstream {
namespace host {
namespace vk {

// X11 headers (via -DUSE_X11) define `#define None 0L`, which conflicts with VVLBehavior::None.
#ifdef None
#undef None
#endif

enum class VVLBehavior {
    None,         // Do not register callback / ignore validation
    PrintOnly,
    Fail,         // Returns an error to the Vulkan API call
    Crash         // Forces a hard crash (e.g., abort())
};

// The payload we pass via pUserData to the debug callback
struct VVLContext {
    std::string appName;
    std::string engineName;
    VVLBehavior behavior;
    std::string appInfo;
};

class VVLConfiguration {
   public:
    static VVLConfiguration parse(const gfxstream::host::FeatureSet& features);
    std::unique_ptr<VVLContext> createDebugContext(
        const std::string& appName,
        const std::string& engineName,
        VkDebugUtilsMessengerCreateInfoEXT* outCreateInfo = nullptr) const;

    VVLBehavior getBehavior() const { return mBehavior; }
    const std::unordered_set<std::string>& getIncludeFilters() const { return mIncludeFilters; }
    const std::unordered_set<std::string>& getExcludeFilters() const { return mExcludeFilters; }

   private:
    VVLConfiguration(VVLBehavior behavior,
                     std::unordered_set<std::string> includeFilters,
                     std::unordered_set<std::string> excludeFilters)
        : mBehavior(behavior),
          mIncludeFilters(std::move(includeFilters)),
          mExcludeFilters(std::move(excludeFilters)) {}

    bool matchesApp(const std::string& appName, const std::string& engineName) const;

    const VVLBehavior mBehavior;
    const std::unordered_set<std::string> mIncludeFilters;
    const std::unordered_set<std::string> mExcludeFilters;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream