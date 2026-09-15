#include "vk_vvl_configuration.h"
#include "vulkan_dispatch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include "gfxstream/common/logging.h"
#include "gfxstream/host/features.h"
#include "gfxstream/strings.h"

namespace gfxstream {
namespace host {
namespace vk {

namespace {

std::string toLowerString(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return res;
}

std::unordered_set<std::string> parseFilterList(const std::string& filterList) {
    std::unordered_set<std::string> patterns;
    for (const auto& token : gfxstream::Split(filterList, ", \t\r\n")) {
        if (token.empty()) {
            continue;
        }
        patterns.insert(toLowerString(token));
    }
    return patterns;
}

VVLBehavior parseVVLBehaviorString(const std::string& modeStr) {
    if (modeStr.empty()) {
        return VVLBehavior::None;
    }
    if (modeStr == "print") {
        return VVLBehavior::PrintOnly;
    } else if (modeStr == "fail") {
        return VVLBehavior::Fail;
    } else if (modeStr == "crash") {
        return VVLBehavior::Crash;
    } else if (modeStr == "off") {
        return VVLBehavior::None;
    } else {
        GFXSTREAM_WARNING("Unknown Vulkan validation mode '%s', defaulting to off.",
                          modeStr.c_str());
        return VVLBehavior::None;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    auto* context = static_cast<VVLContext*>(pUserData);
    if (!context) {
        GFXSTREAM_ERROR("VVL debug callback invoked without a valid VVLContext!");
        return VK_FALSE;
    }

    gfxstream::host::LogLevel logSeverity = gfxstream::host::LogLevel::kInfo;
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        logSeverity = gfxstream::host::LogLevel::kError;
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        logSeverity = gfxstream::host::LogLevel::kWarning;
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        logSeverity = gfxstream::host::LogLevel::kInfo;
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        logSeverity = gfxstream::host::LogLevel::kVerbose;
    }

    GFXSTREAM_LOG_INNER(logSeverity, "VVL%s: %s", context->appInfo.c_str(), pCallbackData->pMessage);

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        if (context->behavior == VVLBehavior::Crash) {
            GFXSTREAM_FATAL("VVL requested CRASH on error: %s", pCallbackData->pMessage);
        } else if (context->behavior == VVLBehavior::Fail) {
            GFXSTREAM_ERROR("VVL requested FAIL on error: %s", pCallbackData->pMessage);
            return VK_TRUE;
        }
    }

    return VK_FALSE;
}

}  // namespace

VVLConfiguration VVLConfiguration::parse(const gfxstream::host::FeatureSet& features) {
    auto vvlValOpt = features.VulkanValidation.getValue();
    std::string valMode = vvlValOpt ? *vvlValOpt : "";

    if (valMode.empty()) {
        if (const char* env = getenv("ANDROID_EMU_VVL_BEHAVIOR")) {
            valMode = env;
            GFXSTREAM_INFO("VVL behavior set via ANDROID_EMU_VVL_BEHAVIOR envvar: '%s'", valMode.c_str());
        }
    }

    std::unordered_set<std::string> includeFilters;
    auto includeOpt = features.VulkanValidationIncludeFilter.getValue();
    if (includeOpt && !includeOpt->empty()) {
        includeFilters = parseFilterList(*includeOpt);
    } else if (const char* env = getenv("ANDROID_EMU_VVL_INCLUDE_FILTER")) {
        includeFilters = parseFilterList(env);
    }

    std::unordered_set<std::string> excludeFilters;
    auto excludeOpt = features.VulkanValidationExcludeFilter.getValue();
    if (excludeOpt && !excludeOpt->empty()) {
        excludeFilters = parseFilterList(*excludeOpt);
    } else if (const char* env = getenv("ANDROID_EMU_VVL_EXCLUDE_FILTER")) {
        excludeFilters = parseFilterList(env);
    }

    if (valMode.empty() && (!includeFilters.empty() || !excludeFilters.empty())) {
        valMode = "print";
        GFXSTREAM_INFO("VVL filters specified without behavior mode, defaulting behavior to '%s'", valMode.c_str());
    }

    VVLBehavior behavior = parseVVLBehaviorString(valMode);
    if (behavior != VVLBehavior::None) {
        ensureVulkanValidationLayersEnabled();
    }

    return VVLConfiguration(behavior, std::move(includeFilters), std::move(excludeFilters));
}

bool VVLConfiguration::matchesApp(const std::string& appName, const std::string& engineName) const {
    if (mBehavior == VVLBehavior::None) {
        return false;
    }

    std::string app = toLowerString(appName);
    std::string engine = toLowerString(engineName);

    return (mIncludeFilters.empty() || mIncludeFilters.find(app) != mIncludeFilters.end() ||
            mIncludeFilters.find(engine) != mIncludeFilters.end()) &&
           (mExcludeFilters.find(app) == mExcludeFilters.end() &&
            mExcludeFilters.find(engine) == mExcludeFilters.end());
}

std::unique_ptr<VVLContext> VVLConfiguration::createDebugContext(
    const std::string& appName, const std::string& engineName,
    VkDebugUtilsMessengerCreateInfoEXT* outCreateInfo) const {
    if (!matchesApp(appName, engineName)) {
        return nullptr;
    }

    auto debugContext = std::make_unique<VVLContext>();
    debugContext->appName = appName;
    debugContext->engineName = engineName;
    debugContext->behavior = mBehavior;

    if (!appName.empty()) {
        debugContext->appInfo = " [App: " + appName;
        if (!engineName.empty() && engineName != "No Engine") {
            debugContext->appInfo += ", Engine: " + engineName;
        }
        debugContext->appInfo += "]";
    }

    if (outCreateInfo) {
        *outCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vk_debug_callback,
            .pUserData = debugContext.get(),
        };
    }

    return debugContext;
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream