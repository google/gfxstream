// Copyright (C) 2024 The Android Open Source Project
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

#include "VirtioGpuResource.h"

#include "FrameBuffer.h"
#include "VirtioGpuFormatUtils.h"

namespace gfxstream {
namespace host {

using gfxstream::base::DescriptorType;
#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
using gfxstream::host::snapshot::VirtioGpuResourceCreateArgs;
using gfxstream::host::snapshot::VirtioGpuResourceCreateBlobArgs;
using gfxstream::host::snapshot::VirtioGpuResourceSnapshot;
#endif

namespace {

enum pipe_texture_target {
    PIPE_BUFFER,
    PIPE_TEXTURE_1D,
    PIPE_TEXTURE_2D,
    PIPE_TEXTURE_3D,
    PIPE_TEXTURE_CUBE,
    PIPE_TEXTURE_RECT,
    PIPE_TEXTURE_1D_ARRAY,
    PIPE_TEXTURE_2D_ARRAY,
    PIPE_TEXTURE_CUBE_ARRAY,
    PIPE_MAX_TEXTURE_TYPES,
};

/**
 *  Resource binding flags -- state tracker must specify in advance all
 *  the ways a resource might be used.
 */
#define PIPE_BIND_DEPTH_STENCIL (1 << 0)        /* create_surface */
#define PIPE_BIND_RENDER_TARGET (1 << 1)        /* create_surface */
#define PIPE_BIND_BLENDABLE (1 << 2)            /* create_surface */
#define PIPE_BIND_SAMPLER_VIEW (1 << 3)         /* create_sampler_view */
#define PIPE_BIND_VERTEX_BUFFER (1 << 4)        /* set_vertex_buffers */
#define PIPE_BIND_INDEX_BUFFER (1 << 5)         /* draw_elements */
#define PIPE_BIND_CONSTANT_BUFFER (1 << 6)      /* set_constant_buffer */
#define PIPE_BIND_DISPLAY_TARGET (1 << 7)       /* flush_front_buffer */
#define PIPE_BIND_STREAM_OUTPUT (1 << 10)       /* set_stream_output_buffers */
#define PIPE_BIND_CURSOR (1 << 11)              /* mouse cursor */
#define PIPE_BIND_CUSTOM (1 << 12)              /* state-tracker/winsys usages */
#define PIPE_BIND_GLOBAL (1 << 13)              /* set_global_binding */
#define PIPE_BIND_SHADER_BUFFER (1 << 14)       /* set_shader_buffers */
#define PIPE_BIND_SHADER_IMAGE (1 << 15)        /* set_shader_images */
#define PIPE_BIND_COMPUTE_RESOURCE (1 << 16)    /* set_compute_resources */
#define PIPE_BIND_COMMAND_ARGS_BUFFER (1 << 17) /* pipe_draw_info.indirect */
#define PIPE_BIND_QUERY_BUFFER (1 << 18)        /* get_query_result_resource */

static inline uint32_t AlignUp(uint32_t n, uint32_t a) { return ((n + a - 1) / a) * a; }

struct ResourceFormatInfo {
    uint32_t drm_fourcc;
    int bpp;
};

static std::unordered_map<int, struct ResourceFormatInfo> virglFormatInfoMap = {
    {VIRGL_FORMAT_B8G8R8A8_UNORM, {DRM_FORMAT_ARGB8888, 4}},
    {VIRGL_FORMAT_B8G8R8X8_UNORM, {DRM_FORMAT_XRGB8888, 4}},
    {VIRGL_FORMAT_B5G6R5_UNORM, {DRM_FORMAT_RGB565, 2}},
    {VIRGL_FORMAT_R8G8B8A8_UNORM, {DRM_FORMAT_ABGR8888, 4}},
    {VIRGL_FORMAT_R8G8B8X8_UNORM, {DRM_FORMAT_XBGR8888, 4}},
    {VIRGL_FORMAT_R8_UNORM, {DRM_FORMAT_R8, 1}},
};

static std::optional<int> DrmFourccToVirglFormat(uint32_t drm_fourcc) {
    for (auto it : virglFormatInfoMap) {
        if (it.second.drm_fourcc == drm_fourcc) {
            return it.first;
        }
    }
    return -1;
}

static std::optional<struct ResourceFormatInfo> VirglFormatInfo(uint32_t virglFormat) {
    auto it = virglFormatInfoMap.find(virglFormat);
    if (virglFormatInfoMap.end() != it) {
        return it->second;
    }
    return std::nullopt;
}

VirtioGpuResourceType GetResourceType(const struct stream_renderer_resource_create_args& args) {
    if (args.target == PIPE_BUFFER && args.bind == VIRGL_BIND_CUSTOM) {
        return VirtioGpuResourceType::PIPE;
    }

    if (args.format != VIRGL_FORMAT_R8_UNORM) {
        return VirtioGpuResourceType::COLOR_BUFFER;
    }
    if (args.bind & VIRGL_BIND_SAMPLER_VIEW) {
        return VirtioGpuResourceType::COLOR_BUFFER;
    }
    if (args.bind & VIRGL_BIND_RENDER_TARGET) {
        return VirtioGpuResourceType::COLOR_BUFFER;
    }
    if (args.bind & VIRGL_BIND_SCANOUT) {
        return VirtioGpuResourceType::COLOR_BUFFER;
    }
    if (args.bind & VIRGL_BIND_CURSOR) {
        return VirtioGpuResourceType::COLOR_BUFFER;
    }
    if (!(args.bind & VIRGL_BIND_LINEAR)) {
        return VirtioGpuResourceType::COLOR_BUFFER;
    }

    return VirtioGpuResourceType::BUFFER;
}

}  // namespace

/*static*/
std::optional<VirtioGpuResource> VirtioGpuResource::Create(
    const struct stream_renderer_resource_create_args* args, struct iovec* iov, uint32_t num_iovs) {
    GFXSTREAM_DEBUG("resource id: %u", args->handle);

    const auto resourceType = GetResourceType(*args);
    if (resourceType == VirtioGpuResourceType::BLOB) {
        GFXSTREAM_ERROR("Failed to create resource: encountered blob.");
        return std::nullopt;
    }

    if (resourceType == VirtioGpuResourceType::PIPE) {
        // Frontend only resource.
    } else if (resourceType == VirtioGpuResourceType::BUFFER) {
        FrameBuffer::getFB()->createBufferWithResourceHandle(args->width * args->height,
                                                             args->handle);
    } else if (resourceType == VirtioGpuResourceType::COLOR_BUFFER) {
        const uint32_t glformat = virgl_format_to_gl(args->format);
        const auto fwkformat = (gfxstream::FrameworkFormat)virgl_format_to_fwk_format(args->format);
        FrameBuffer::getFB()->createColorBufferWithResourceHandle(
            args->width, args->height, glformat, fwkformat, args->handle);
        FrameBuffer::getFB()->setGuestManagedColorBufferLifetime(true /* guest manages lifetime */);
        FrameBuffer::getFB()->openColorBuffer(args->handle);
    } else {
        GFXSTREAM_ERROR("Failed to create resource: unhandled type.");
        return std::nullopt;
    }

    VirtioGpuResource resource;
    resource.mId = args->handle;
    resource.mResourceType = resourceType;
    resource.mCreateArgs = *args;

    resource.AttachIov(iov, num_iovs);

    return resource;
}

/*static*/
std::optional<VirtioGpuResource> VirtioGpuResource::Create(
    uint32_t res_handle, const struct stream_renderer_handle* import_handle,
    const struct stream_renderer_import_data* import_data) {
    GFXSTREAM_DEBUG("resource id: %u", res_handle);

    if (!import_handle || !import_data) {
        GFXSTREAM_ERROR("Failed to import resource: import_handle/import_data not provided.");
        return std::nullopt;
    } else if (!(import_data->flags & STREAM_RENDERER_IMPORT_FLAG_3D_INFO)) {
        GFXSTREAM_ERROR(
            "Failed to import resource: stream_renderer_3d_info not provided in import data.");
        return std::nullopt;
    }

    struct stream_renderer_resource_create_args internal_create_args = {0};
    internal_create_args.handle = res_handle;
    // TODO(aruby@blackberry.com): Determine VIRGL_BIND_LINEAR from info_3d?
    internal_create_args.bind = VIRGL_BIND_SAMPLER_VIEW | VIRGL_BIND_SCANOUT | VIRGL_BIND_SHARED;
    internal_create_args.target = PIPE_TEXTURE_2D;
    // From info_3d
    auto virglFormat = DrmFourccToVirglFormat(import_data->info_3d.drm_fourcc);
    if (!virglFormat) {
        GFXSTREAM_ERROR("No virgl format available for drm_fourcc: %d",
                        import_data->info_3d.drm_fourcc);
        return std::nullopt;
    }
    internal_create_args.format = *virglFormat;
    internal_create_args.width = import_data->info_3d.width;
    internal_create_args.height = import_data->info_3d.height;
    // Default values
    internal_create_args.depth = 1;
    internal_create_args.array_size = 1;
    internal_create_args.last_level = 0;
    internal_create_args.nr_samples = 0;
    internal_create_args.flags = 0;

    const auto resourceType = GetResourceType(internal_create_args);
    if (resourceType != VirtioGpuResourceType::COLOR_BUFFER) {
        GFXSTREAM_ERROR(
            "Failed to create resource with import_handle: arguments resulted in unhandled type. "
            "Only ColorBuffer resources are supported for import.");
        return std::nullopt;
    }

    ExternalObjectManager::get()->addResourceExternalHandleInfo(
        res_handle, ExternalHandleInfo{
                        .handle = import_handle->os_handle,
                        .streamHandleType = import_handle->handle_type,
                    });
    const uint32_t glformat = virgl_format_to_gl(internal_create_args.format);
    const auto fwkformat =
        (gfxstream::FrameworkFormat)virgl_format_to_fwk_format(internal_create_args.format);
    FrameBuffer::getFB()->createColorBufferWithResourceHandle(
        internal_create_args.width, internal_create_args.height, glformat, fwkformat,
        internal_create_args.handle);
    FrameBuffer::getFB()->setGuestManagedColorBufferLifetime(true /* guest manages lifetime */);
    FrameBuffer::getFB()->openColorBuffer(internal_create_args.handle);

    VirtioGpuResource resource;
    resource.mId = res_handle;
    resource.mResourceType = resourceType;
    resource.mCreateArgs = internal_create_args;

    return resource;
}

/*static*/ std::optional<VirtioGpuResource> VirtioGpuResource::Create(
    const gfxstream::host::FeatureSet& features, uint32_t pageSize, uint32_t contextId,
    uint32_t resourceId, const struct stream_renderer_resource_create_args* createArgs,
    const struct stream_renderer_create_blob* createBlobArgs,
    const struct stream_renderer_handle* handle) {
    VirtioGpuResource resource;

    std::optional<BlobDescriptorInfo> descriptorInfoOpt;

    if (createArgs != nullptr) {
        auto resourceType = GetResourceType(*createArgs);
        if (resourceType != VirtioGpuResourceType::BUFFER &&
            resourceType != VirtioGpuResourceType::COLOR_BUFFER) {
            GFXSTREAM_ERROR("failed to create blob resource: unhandled type.");
            return std::nullopt;
        }

        auto resourceOpt = Create(createArgs, nullptr, 0);
        if (!resourceOpt) {
            return std::nullopt;
        }

        if (resourceType == VirtioGpuResourceType::BUFFER) {
            descriptorInfoOpt = FrameBuffer::getFB()->exportBuffer(resourceId);
        } else if (resourceType == VirtioGpuResourceType::COLOR_BUFFER) {
            descriptorInfoOpt = FrameBuffer::getFB()->exportColorBuffer(resourceId);
        } else {
            GFXSTREAM_ERROR("failed to create blob resource: unhandled type.");
            return std::nullopt;
        }

        resource = std::move(*resourceOpt);
    } else {
        resource.mResourceType = VirtioGpuResourceType::BLOB;
    }

    resource.mId = resourceId;
    resource.mCreateBlobArgs = *createBlobArgs;

    if (createBlobArgs->blob_id == 0) {
        RingBlobMemory memory;
        if (features.ExternalBlob.enabled) {
            memory = RingBlob::CreateWithShmem(resourceId, createBlobArgs->size);
        } else {
            memory = RingBlob::CreateWithHostMemory(resourceId, createBlobArgs->size, pageSize);
        }
        if (!memory) {
            GFXSTREAM_ERROR("Failed to create blob: failed to create ring blob.");
            return std::nullopt;
        }
        resource.mBlobMemory.emplace(std::move(memory));
    } else if (features.ExternalBlob.enabled) {
        if (createBlobArgs->blob_mem == STREAM_BLOB_MEM_GUEST &&
            (createBlobArgs->blob_flags & STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE)) {
#if defined(__linux__) || defined(__QNX__)
            ManagedDescriptor managedHandle(handle->os_handle);
            ExternalObjectManager::get()->addBlobDescriptorInfo(
                contextId, createBlobArgs->blob_id, std::move(managedHandle), handle->handle_type,
                0, std::nullopt);
#else
            GFXSTREAM_ERROR("Failed to create blob: unimplemented external blob.");
            return std::nullopt;
#endif
        } else {
            if (!descriptorInfoOpt) {
                descriptorInfoOpt = ExternalObjectManager::get()->removeBlobDescriptorInfo(
                    contextId, createBlobArgs->blob_id);
            }
            if (!descriptorInfoOpt) {
                GFXSTREAM_ERROR("Failed to create blob: no external blob descriptor.");
                return std::nullopt;
            }
            resource.mBlobMemory.emplace(
                std::make_shared<BlobDescriptorInfo>(std::move(*descriptorInfoOpt)));
        }
    } else {
        auto memoryMappingOpt =
            ExternalObjectManager::get()->removeMapping(contextId, createBlobArgs->blob_id);
        if (!memoryMappingOpt) {
            GFXSTREAM_ERROR("Failed to create blob: no external blob mapping.");
            return std::nullopt;
        }
        resource.mBlobMemory.emplace(std::move(*memoryMappingOpt));
    }

    return resource;
}

int VirtioGpuResource::Destroy() {
    if (mResourceType == VirtioGpuResourceType::BUFFER) {
        FrameBuffer::getFB()->closeBuffer(mId);
    } else if (mResourceType == VirtioGpuResourceType::COLOR_BUFFER) {
        FrameBuffer::getFB()->closeColorBuffer(mId);
    }
    return 0;
}

int VirtioGpuResource::ImportHandle(const struct stream_renderer_handle* handle,
                                    const struct stream_renderer_import_data* import_data) {
    if (mResourceType != VirtioGpuResourceType::COLOR_BUFFER) {
        GFXSTREAM_ERROR(
            "Failed to ImportResource: importing external handles to existing resources is only "
            "supported for ColorBuffer resources.");
        return -EINVAL;
    }

    auto colorBufferPtr = FrameBuffer::getFB()->findColorBuffer(mId);
    if (!colorBufferPtr) {
        GFXSTREAM_ERROR("Failed to ImportResource: could not find colorBuffer for res_handle: %d",
                        mId);
        return -EINVAL;
    }

    const bool preserveContent =
        (import_data->flags & STREAM_RENDERER_IMPORT_FLAG_PRESERVE_CONTENT);
    bool importSuccess = false;
    switch (handle->handle_type) {
#if GFXSTREAM_ENABLE_HOST_GLES
        case STREAM_HANDLE_TYPE_PLATFORM_EGL_NATIVE_PIXMAP:
            importSuccess = colorBufferPtr->glOpImportEglNativePixmap(
                reinterpret_cast<void*>(handle->os_handle), preserveContent);
            break;
#endif
        default:
            GFXSTREAM_ERROR(
                "Unsupported handle_type: 0x%x, specified for importing to resource: %d",
                handle->handle_type, mId);
            return -EINVAL;
    }

    return (importSuccess ? 0 : -EINVAL);
}

void VirtioGpuResource::AttachIov(struct iovec* iov, uint32_t num_iovs) {
    mIovs.clear();
    mLinear.clear();

    size_t linearSize = 0;
    if (num_iovs) {
        mIovs.reserve(num_iovs);
        for (uint32_t i = 0; i < num_iovs; ++i) {
            mIovs.push_back(iov[i]);
            linearSize += iov[i].iov_len;
        }
    }

    if (linearSize > 0) {
        mLinear.resize(linearSize, 0);
    }
}

void VirtioGpuResource::AttachToContext(VirtioGpuContextId contextId) {
    mAttachedToContexts.insert(contextId);
    mLatestAttachedContext = contextId;
}

void VirtioGpuResource::DetachFromContext(VirtioGpuContextId contextId) {
    mAttachedToContexts.erase(contextId);
    mLatestAttachedContext.reset();
    mHostPipe = nullptr;
}

std::unordered_set<VirtioGpuContextId> VirtioGpuResource::GetAttachedContexts() const {
    return mAttachedToContexts;
}

void VirtioGpuResource::DetachIov() {
    mIovs.clear();
    mLinear.clear();
}

int VirtioGpuResource::Map(void** outAddress, uint64_t* outSize) {
    if (!mBlobMemory) {
        GFXSTREAM_ERROR("Failed to map resource %d: no blob memory to map.", mId);
        return -EINVAL;
    }

    void* hva = nullptr;
    uint64_t hvaSize = 0;

    if (std::holds_alternative<RingBlobMemory>(*mBlobMemory)) {
        auto& memory = std::get<RingBlobMemory>(*mBlobMemory);
        hva = memory->map();
        hvaSize = memory->size();
    } else if (std::holds_alternative<ExternalMemoryMapping>(*mBlobMemory)) {
        if (!mCreateBlobArgs) {
            GFXSTREAM_ERROR("failed to map resource %d: missing args.", mId);
            return -EINVAL;
        }
        auto& memory = std::get<ExternalMemoryMapping>(*mBlobMemory);
        hva = memory.addr;
        hvaSize = mCreateBlobArgs->size;
    } else {
        GFXSTREAM_ERROR("failed to map resource %d: no mappable memory.", mId);
        return -EINVAL;
    }

    if (outAddress) {
        *outAddress = hva;
    }
    if (outSize) {
        *outSize = hvaSize;
    }
    return 0;
}

int VirtioGpuResource::GetInfo(struct stream_renderer_resource_info* outInfo) const {
    if (!mCreateArgs) {
        GFXSTREAM_ERROR("Failed to get info: resource %d missing args.", mId);
        return ENOENT;
    }

    auto formatInfo = VirglFormatInfo(mCreateArgs->format);
    if (!formatInfo) {
        return EINVAL;
    }

    outInfo->drm_fourcc = formatInfo->drm_fourcc;
    outInfo->stride = AlignUp(mCreateArgs->width * formatInfo->bpp, 16U);
    outInfo->virgl_format = mCreateArgs->format;
    outInfo->handle = mCreateArgs->handle;
    outInfo->height = mCreateArgs->height;
    outInfo->width = mCreateArgs->width;
    outInfo->depth = mCreateArgs->depth;
    outInfo->flags = mCreateArgs->flags;
    outInfo->tex_id = 0;
    return 0;
}

int VirtioGpuResource::GetVulkanInfo(struct stream_renderer_vulkan_info* outInfo) const {
    if (!mBlobMemory) {
        return -EINVAL;
    }
    if (!std::holds_alternative<ExternalMemoryInfo>(*mBlobMemory)) {
        return -EINVAL;
    }
    auto& memory = std::get<ExternalMemoryInfo>(*mBlobMemory);
    if (!memory->vulkanInfoOpt) {
        return -EINVAL;
    }
    auto& memoryVulkanInfo = *memory->vulkanInfoOpt;

    outInfo->memory_index = memoryVulkanInfo.memoryIndex;
    memcpy(outInfo->device_id.device_uuid, memoryVulkanInfo.deviceUUID,
           sizeof(outInfo->device_id.device_uuid));
    memcpy(outInfo->device_id.driver_uuid, memoryVulkanInfo.driverUUID,
           sizeof(outInfo->device_id.driver_uuid));
    return 0;
}

int VirtioGpuResource::GetCaching(uint32_t* outCaching) const {
    if (!mBlobMemory) {
        GFXSTREAM_ERROR("failed to get caching for resource %d: no blob memory", mId);
        return -EINVAL;
    }

    if (std::holds_alternative<ExternalMemoryMapping>(*mBlobMemory)) {
        auto& memory = std::get<ExternalMemoryMapping>(*mBlobMemory);
        *outCaching = memory.caching;
        return 0;
    } else if (std::holds_alternative<ExternalMemoryInfo>(*mBlobMemory)) {
        auto& memory = std::get<ExternalMemoryInfo>(*mBlobMemory);
        *outCaching = memory->caching;
        return 0;
    } else if (std::holds_alternative<RingBlobMemory>(*mBlobMemory)) {
        *outCaching = STREAM_RENDERER_MAP_CACHE_CACHED;
        return 0;
    }

    GFXSTREAM_ERROR("failed to get caching for resource %d: unhandled type?", mId);
    return -EINVAL;
}

// Corresponds to Virtio GPU "TransferFromHost" commands and VMM requests to
// copy into display buffers.
int VirtioGpuResource::TransferRead(uint64_t offset, stream_renderer_box* box,
                                    std::optional<std::vector<struct iovec>> iovs) {
    // First, copy from the underlying backend resource to this resource's linear buffer:
    int ret = 0;
    if (mResourceType == VirtioGpuResourceType::BLOB) {
        GFXSTREAM_ERROR("Failed to transfer: unexpected blob.");
        return -EINVAL;
    } else if (mResourceType == VirtioGpuResourceType::PIPE) {
        ret = ReadFromPipeToLinear(offset, box);
    } else if (mResourceType == VirtioGpuResourceType::BUFFER) {
        ret = ReadFromBufferToLinear(offset, box);
    } else if (mResourceType == VirtioGpuResourceType::COLOR_BUFFER) {
        ret = ReadFromColorBufferToLinear(offset, box);
    } else {
        GFXSTREAM_ERROR("Failed to transfer: unhandled resource type.");
        return -EINVAL;
    }
    if (ret != 0) {
        GFXSTREAM_ERROR("Failed to transfer: failed to sync with backend resource.");
        return ret;
    }

    // Second, copy from this resource's linear buffer to the desired iov:
    if (iovs) {
        ret = TransferToIov(offset, box, *iovs);
    } else {
        ret = TransferToIov(offset, box, mIovs);
    }
    if (ret != 0) {
        GFXSTREAM_ERROR("Failed to transfer: failed to copy to iov.");
    }
    return ret;
}

// Corresponds to Virtio GPU "TransferToHost" commands.
int VirtioGpuResource::TransferWrite(uint64_t offset, stream_renderer_box* box,
                                     std::optional<std::vector<struct iovec>> iovs) {
    // First, copy from the desired iov to this resource's linear buffer:
    int ret = 0;
    if (iovs) {
        ret = TransferFromIov(offset, box, *iovs);
    } else {
        ret = TransferFromIov(offset, box, mIovs);
    }
    if (ret != 0) {
        GFXSTREAM_ERROR("Failed to transfer: failed to copy from iov.");
        return ret;
    }

    // Second, copy from this resource's linear buffer to the underlying backend resource:
    if (mResourceType == VirtioGpuResourceType::BLOB) {
        GFXSTREAM_ERROR("Failed to transfer: unexpected blob.");
        return -EINVAL;
    } else if (mResourceType == VirtioGpuResourceType::PIPE) {
        return WriteToPipeFromLinear(offset, box);
    } else if (mResourceType == VirtioGpuResourceType::BUFFER) {
        return WriteToBufferFromLinear(offset, box);
    } else if (mResourceType == VirtioGpuResourceType::COLOR_BUFFER) {
        return WriteToColorBufferFromLinear(offset, box);
    } else {
        GFXSTREAM_ERROR("Failed to transfer: unhandled resource type.");
        return -EINVAL;
    }
}

int VirtioGpuResource::ReadFromPipeToLinear(uint64_t offset, stream_renderer_box* box) {
    if (mResourceType != VirtioGpuResourceType::PIPE) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d is not PIPE.", mId);
        return -EINVAL;
    }

    if (!mHostPipe) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d missing PIPE.", mId);
        return -EINVAL;
    }

    return mHostPipe->TransferFromHost(mLinear.data() + box->x, box->w);
}

int VirtioGpuResource::WriteToPipeFromLinear(uint64_t offset, stream_renderer_box* box) {
    if (mResourceType != VirtioGpuResourceType::PIPE) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d is not PIPE.", mId);
        return -EINVAL;
    }

    if (!mCreateArgs) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d missing args.", mId);
        return -EINVAL;
    }

    auto hostPipe = mHostPipe;
    if (!mHostPipe) {
        GFXSTREAM_ERROR("No hostPipe");
        return -EINVAL;
    }

    return mHostPipe->TransferToHost(mLinear.data() + box->x, box->w);
}

int VirtioGpuResource::ReadFromBufferToLinear(uint64_t offset, stream_renderer_box* box) {
    if (mResourceType != VirtioGpuResourceType::BUFFER) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d is not BUFFER.", mId);
        return -EINVAL;
    }

    if (!mCreateArgs) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d missing args.", mId);
        return -EINVAL;
    }

    FrameBuffer::getFB()->readBuffer(mCreateArgs->handle, 0,
                                     mCreateArgs->width * mCreateArgs->height, mLinear.data());
    return 0;
}

int VirtioGpuResource::WriteToBufferFromLinear(uint64_t offset, stream_renderer_box* box) {
    if (mResourceType != VirtioGpuResourceType::BUFFER) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d is not BUFFER.", mId);
        return -EINVAL;
    }

    if (!mCreateArgs) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d missing args.", mId);
        return -EINVAL;
    }

    FrameBuffer::getFB()->updateBuffer(mCreateArgs->handle, 0,
                                       mCreateArgs->width * mCreateArgs->height, mLinear.data());
    return 0;
}

int VirtioGpuResource::ReadFromColorBufferToLinear(uint64_t offset, stream_renderer_box* box) {
    if (mResourceType != VirtioGpuResourceType::COLOR_BUFFER) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d is not COLOR_BUFFER.", mId);
        return -EINVAL;
    }

    if (!mCreateArgs) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d missing args.", mId);
        return -EINVAL;
    }

    auto glformat = virgl_format_to_gl(mCreateArgs->format);
    auto gltype = gl_format_to_natural_type(glformat);

    // We always xfer the whole thing again from GL
    // since it's fiddly to calc / copy-out subregions
    if (virgl_format_is_yuv(mCreateArgs->format)) {
        FrameBuffer::getFB()->readColorBufferYUV(mCreateArgs->handle, 0, 0, mCreateArgs->width,
                                                 mCreateArgs->height, mLinear.data(),
                                                 mLinear.size());
    } else {
        FrameBuffer::getFB()->readColorBuffer(mCreateArgs->handle, 0, 0, mCreateArgs->width,
                                              mCreateArgs->height, glformat, gltype,
                                              mLinear.data());
    }

    return 0;
}

int VirtioGpuResource::WriteToColorBufferFromLinear(uint64_t offset, stream_renderer_box* box) {
    if (mResourceType != VirtioGpuResourceType::COLOR_BUFFER) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d is not COLOR_BUFFER.", mId);
        return -EINVAL;
    }

    if (!mCreateArgs) {
        GFXSTREAM_ERROR("Failed to transfer: resource %d missing args.", mId);
        return -EINVAL;
    }

    auto glformat = virgl_format_to_gl(mCreateArgs->format);
    auto gltype = gl_format_to_natural_type(glformat);

    // We always xfer the whole thing again to GL
    // since it's fiddly to calc / copy-out subregions
    FrameBuffer::getFB()->updateColorBuffer(mCreateArgs->handle, 0, 0, mCreateArgs->width,
                                            mCreateArgs->height, glformat, gltype, mLinear.data());
    return 0;
}

int VirtioGpuResource::TransferToIov(uint64_t offset, const stream_renderer_box* box,
                                     std::optional<std::vector<struct iovec>> iovs) {
    if (iovs) {
        return TransferWithIov(offset, box, *iovs, TransferDirection::LINEAR_TO_IOV);
    } else {
        return TransferWithIov(offset, box, mIovs, TransferDirection::LINEAR_TO_IOV);
    }
}

int VirtioGpuResource::TransferFromIov(uint64_t offset, const stream_renderer_box* box,
                                       std::optional<std::vector<struct iovec>> iovs) {
    if (iovs) {
        return TransferWithIov(offset, box, *iovs, TransferDirection::IOV_TO_LINEAR);
    } else {
        return TransferWithIov(offset, box, mIovs, TransferDirection::IOV_TO_LINEAR);
    }
}

int VirtioGpuResource::TransferWithIov(uint64_t offset, const stream_renderer_box* box,
                                       const std::vector<struct iovec>& iovs,
                                       TransferDirection direction) {
    if (!mCreateArgs) {
        GFXSTREAM_ERROR("failed to transfer: missing resource args.");
        return -EINVAL;
    }
    if (box->x > mCreateArgs->width || box->y > mCreateArgs->height) {
        GFXSTREAM_ERROR("failed to transfer: box out of range of resource");
        return -EINVAL;
    }
    if (box->w == 0U || box->h == 0U) {
        GFXSTREAM_ERROR("failed to transfer: empty transfer");
        return -EINVAL;
    }
    if (box->x + box->w > mCreateArgs->width) {
        GFXSTREAM_ERROR("failed to transfer: box overflows resource width");
        return -EINVAL;
    }

    size_t linearBase =
        virgl_format_to_linear_base(mCreateArgs->format, mCreateArgs->width, mCreateArgs->height,
                                    box->x, box->y, box->w, box->h);
    size_t start = linearBase;
    // height - 1 in order to treat the (w * bpp) row specially
    // (i.e., the last row does not occupy the full stride)
    size_t length =
        virgl_format_to_total_xfer_len(mCreateArgs->format, mCreateArgs->width, mCreateArgs->height,
                                       box->x, box->y, box->w, box->h);
    size_t end = start + length;

    if (start == end) {
        GFXSTREAM_ERROR("failed to transfer: nothing to transfer");
        return -EINVAL;
    }

    if (end > mLinear.size()) {
        GFXSTREAM_ERROR("failed to transfer: start + length overflows!");
        return -EINVAL;
    }

    uint32_t iovIndex = 0;
    size_t iovOffset = 0;
    size_t written = 0;
    char* linear = static_cast<char*>(mLinear.data());

    while (written < length) {
        if (iovIndex >= iovs.size()) {
            GFXSTREAM_ERROR("failed to transfer: write request overflowed iovs");
            return -EINVAL;
        }

        const char* iovBase_const = static_cast<const char*>(iovs[iovIndex].iov_base);
        char* iovBase = static_cast<char*>(iovs[iovIndex].iov_base);
        size_t iovLen = iovs[iovIndex].iov_len;
        size_t iovOffsetEnd = iovOffset + iovLen;

        auto lower_intersect = std::max(iovOffset, start);
        auto upper_intersect = std::min(iovOffsetEnd, end);
        if (lower_intersect < upper_intersect) {
            size_t toWrite = upper_intersect - lower_intersect;
            switch (direction) {
                case TransferDirection::IOV_TO_LINEAR:
                    memcpy(linear + lower_intersect, iovBase_const + lower_intersect - iovOffset,
                           toWrite);
                    break;
                case TransferDirection::LINEAR_TO_IOV:
                    memcpy(iovBase + lower_intersect - iovOffset, linear + lower_intersect,
                           toWrite);
                    break;
                default:
                    GFXSTREAM_ERROR("failed to transfer: invalid synchronization dir");
                    return -EINVAL;
            }
            written += toWrite;
        }
        ++iovIndex;
        iovOffset += iovLen;
    }

    return 0;
}

int VirtioGpuResource::ExportBlob(struct stream_renderer_handle* outHandle) {
    if (!mBlobMemory) {
        return -EINVAL;
    }

    if (std::holds_alternative<RingBlobMemory>(*mBlobMemory)) {
        auto& memory = std::get<RingBlobMemory>(*mBlobMemory);
        if (!memory->isExportable()) {
            return -EINVAL;
        }

        // Handle ownership transferred to VMM, Gfxstream keeps the mapping.
#ifdef _WIN32
        outHandle->os_handle =
            static_cast<int64_t>(reinterpret_cast<intptr_t>(memory->releaseHandle()));
#else
        outHandle->os_handle = static_cast<int64_t>(memory->releaseHandle());
#endif
        outHandle->handle_type = STREAM_HANDLE_TYPE_MEM_SHM;
        return 0;
    } else if (std::holds_alternative<ExternalMemoryInfo>(*mBlobMemory)) {
        auto& memory = std::get<ExternalMemoryInfo>(*mBlobMemory);

        auto rawDescriptorOpt = memory->descriptorInfo.descriptor.release();
        if (!rawDescriptorOpt) {
            GFXSTREAM_ERROR("failed to export blob for resource %u: failed to get raw handle.",
                            mId);
            return -EINVAL;
        }
        auto rawDescriptor = *rawDescriptorOpt;

#ifdef _WIN32
        outHandle->os_handle = static_cast<int64_t>(reinterpret_cast<intptr_t>(rawDescriptor));
#else
        outHandle->os_handle = static_cast<int64_t>(rawDescriptor);
#endif
        outHandle->handle_type = memory->descriptorInfo.streamHandleType;
        return 0;
    }

    return -EINVAL;
}

std::shared_ptr<RingBlob> VirtioGpuResource::ShareRingBlob() {
    if (!mBlobMemory) {
        return nullptr;
    }
    if (!std::holds_alternative<RingBlobMemory>(*mBlobMemory)) {
        return nullptr;
    }
    return std::get<RingBlobMemory>(*mBlobMemory);
}

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

std::optional<VirtioGpuResourceSnapshot> VirtioGpuResource::Snapshot() const {
    VirtioGpuResourceSnapshot resourceSnapshot;
    resourceSnapshot.set_id(mId);
    resourceSnapshot.set_type(static_cast<::gfxstream::host::snapshot::VirtioGpuResourceType>(mResourceType));

    if (mCreateArgs) {
        VirtioGpuResourceCreateArgs* snapshotCreateArgs = resourceSnapshot.mutable_create_args();
        snapshotCreateArgs->set_id(mCreateArgs->handle);
        snapshotCreateArgs->set_target(mCreateArgs->target);
        snapshotCreateArgs->set_format(mCreateArgs->format);
        snapshotCreateArgs->set_bind(mCreateArgs->bind);
        snapshotCreateArgs->set_width(mCreateArgs->width);
        snapshotCreateArgs->set_height(mCreateArgs->height);
        snapshotCreateArgs->set_depth(mCreateArgs->depth);
        snapshotCreateArgs->set_array_size(mCreateArgs->array_size);
        snapshotCreateArgs->set_last_level(mCreateArgs->last_level);
        snapshotCreateArgs->set_nr_samples(mCreateArgs->nr_samples);
        snapshotCreateArgs->set_flags(mCreateArgs->flags);
    }

    if (mCreateBlobArgs) {
        auto* snapshotCreateArgs = resourceSnapshot.mutable_create_blob_args();
        snapshotCreateArgs->set_mem(mCreateBlobArgs->blob_mem);
        snapshotCreateArgs->set_flags(mCreateBlobArgs->blob_flags);
        snapshotCreateArgs->set_id(mCreateBlobArgs->blob_id);
        snapshotCreateArgs->set_size(mCreateBlobArgs->size);
    }

    if (mBlobMemory) {
        if (std::holds_alternative<RingBlobMemory>(*mBlobMemory)) {
            auto& memory = std::get<RingBlobMemory>(*mBlobMemory);

            auto snapshotRingBlobOpt = memory->Snapshot();
            if (!snapshotRingBlobOpt) {
                GFXSTREAM_ERROR("Failed to snapshot ring blob for resource %d.", mId);
                return std::nullopt;
            }
            resourceSnapshot.mutable_ring_blob()->Swap(&*snapshotRingBlobOpt);
        } else if (std::holds_alternative<ExternalMemoryInfo>(*mBlobMemory)) {
            if (!mLatestAttachedContext) {
                GFXSTREAM_ERROR("Failed to snapshot resource %d: missing blob context?", mId);
                return std::nullopt;
            }
            if (!mCreateBlobArgs) {
                GFXSTREAM_ERROR("Failed to snapshot resource %d: missing blob args?", mId);
                return std::nullopt;
            }
            auto snapshotDescriptorInfo = resourceSnapshot.mutable_external_memory_descriptor();
            snapshotDescriptorInfo->set_context_id(*mLatestAttachedContext);
            snapshotDescriptorInfo->set_blob_id(mCreateBlobArgs->blob_id);
        } else if (std::holds_alternative<ExternalMemoryMapping>(*mBlobMemory)) {
            if (!mLatestAttachedContext) {
                GFXSTREAM_ERROR("Failed to snapshot resource %d: missing blob context?", mId);
                return std::nullopt;
            }
            if (!mCreateBlobArgs) {
                GFXSTREAM_ERROR("Failed to snapshot resource %d: missing blob args?", mId);
                return std::nullopt;
            }
            auto snapshotDescriptorInfo = resourceSnapshot.mutable_external_memory_mapping();
            snapshotDescriptorInfo->set_context_id(*mLatestAttachedContext);
            snapshotDescriptorInfo->set_blob_id(mCreateBlobArgs->blob_id);
        }
    }

    if (mLatestAttachedContext) {
        resourceSnapshot.set_latest_attached_context(*mLatestAttachedContext);
    }

    resourceSnapshot.mutable_attached_contexts()->Add(mAttachedToContexts.begin(),
                                                      mAttachedToContexts.end());

    return resourceSnapshot;
}

/*static*/ std::optional<VirtioGpuResource> VirtioGpuResource::Restore(
    const VirtioGpuResourceSnapshot& resourceSnapshot) {
    VirtioGpuResource resource = {};
    resource.mId = resourceSnapshot.id();
    resource.mResourceType = static_cast<VirtioGpuResourceType>(resourceSnapshot.type());

    if (resourceSnapshot.has_create_args()) {
        const auto& createArgsSnapshot = resourceSnapshot.create_args();
        resource.mCreateArgs = {
            .handle = createArgsSnapshot.id(),
            .target = createArgsSnapshot.target(),
            .format = createArgsSnapshot.format(),
            .bind = createArgsSnapshot.bind(),
            .width = createArgsSnapshot.width(),
            .height = createArgsSnapshot.height(),
            .depth = createArgsSnapshot.depth(),
            .array_size = createArgsSnapshot.array_size(),
            .last_level = createArgsSnapshot.last_level(),
            .nr_samples = createArgsSnapshot.nr_samples(),
            .flags = createArgsSnapshot.flags(),
        };
    }

    if (resourceSnapshot.has_create_blob_args()) {
        const auto& createArgsSnapshot = resourceSnapshot.create_blob_args();
        resource.mCreateBlobArgs = {
            .blob_mem = createArgsSnapshot.mem(),
            .blob_flags = createArgsSnapshot.flags(),
            .blob_id = createArgsSnapshot.id(),
            .size = createArgsSnapshot.size(),
        };
    }

    if (resourceSnapshot.has_ring_blob()) {
        auto resourceRingBlobOpt = RingBlob::Restore(resourceSnapshot.ring_blob());
        if (!resourceRingBlobOpt) {
            GFXSTREAM_ERROR("Failed to restore ring blob for resource %d", resource.mId);
            return std::nullopt;
        }
        resource.mBlobMemory.emplace(std::move(*resourceRingBlobOpt));
    } else if (resourceSnapshot.has_external_memory_descriptor()) {
        const auto& snapshotDescriptorInfo = resourceSnapshot.external_memory_descriptor();

        auto descriptorInfoOpt = ExternalObjectManager::get()->removeBlobDescriptorInfo(
            snapshotDescriptorInfo.context_id(), snapshotDescriptorInfo.blob_id());
        if (!descriptorInfoOpt) {
            GFXSTREAM_ERROR("Failed to restore resource: failed to find blob descriptor info.");
            return std::nullopt;
        }

        resource.mBlobMemory.emplace(
            std::make_shared<BlobDescriptorInfo>(std::move(*descriptorInfoOpt)));
    } else if (resourceSnapshot.has_external_memory_mapping()) {
        const auto& snapshotDescriptorInfo = resourceSnapshot.external_memory_mapping();

        auto memoryMappingOpt = ExternalObjectManager::get()->removeMapping(
            snapshotDescriptorInfo.context_id(), snapshotDescriptorInfo.blob_id());
        if (!memoryMappingOpt) {
            GFXSTREAM_ERROR("Failed to restore resource: failed to find mapping info.");
            return std::nullopt;
        }
        resource.mBlobMemory.emplace(std::move(*memoryMappingOpt));
    }

    if (resourceSnapshot.has_latest_attached_context()) {
        resource.mLatestAttachedContext = resourceSnapshot.latest_attached_context();
    }

    resource.mAttachedToContexts.insert(resourceSnapshot.attached_contexts().begin(),
                                        resourceSnapshot.attached_contexts().end());

    return resource;
}

#endif  // #ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

}  // namespace host
}  // namespace gfxstream
