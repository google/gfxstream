{
global:
	# EGL 1.0
	eglChooseConfig;
	eglCopyBuffers;
	eglCreateContext;
	eglCreatePbufferSurface;
	eglCreatePixmapSurface;
	eglCreateWindowSurface;
	eglDestroyContext;
	eglDestroySurface;
	eglGetConfigAttrib;
	eglGetConfigs;
	eglGetCurrentDisplay;
	eglGetCurrentSurface;
	eglGetDisplay;
	eglGetError;
	eglGetProcAddress;
	eglInitialize;
	eglMakeCurrent;
	eglQueryContext;
	eglQueryString;
	eglQuerySurface;
	eglSwapBuffers;
	eglTerminate;
	eglWaitGL;
	eglWaitNative;

	# EGL 1.1
	eglBindTexImage;
	eglReleaseTexImage;
	eglSurfaceAttrib;
	eglSwapInterval;

	# EGL 1.2
	eglBindAPI;
	eglCreatePbufferFromClientBuffer;
	eglQueryAPI;
	eglReleaseThread;
	eglWaitClient;

	# EGL 1.4
	eglGetCurrentContext;

	# EGL 1.5
	eglClientWaitSync;
	eglCreateImage;
	eglCreatePlatformPixmapSurface;
	eglCreatePlatformWindowSurface;
	eglCreateSync;
	eglDestroyImage;
	eglDestroySync;
	eglGetPlatformDisplay;
	eglGetSyncAttrib;
	eglWaitSync;

	# EGL_ANDROID_blob_cache
	eglSetBlobCacheFuncsANDROID;

	# EGL_ANDROID_create_native_client_buffer
	eglCreateNativeClientBufferANDROID;

	# EGL_ANDROID_get_frame_timestamps
	eglGetCompositorTimingANDROID;
	eglGetCompositorTimingSupportedANDROID;
	eglGetFrameTimestampSupportedANDROID;
	eglGetFrameTimestampsANDROID;
	eglGetNextFrameIdANDROID;

	# EGL_ANDROID_get_native_client_buffer
	eglGetNativeClientBufferANDROID;

	# EGL_ANDROID_native_fence_sync
	eglDupNativeFenceFDANDROID;

	# EGL_ANDROID_presentation_time
	eglPresentationTimeANDROID;

	# EGL_ANGLE_device_creation
	eglCreateDeviceANGLE;
	eglReleaseDeviceANGLE;

	# EGL_ANGLE_feature_control
	eglQueryDisplayAttribANGLE;
	eglQueryStringiANGLE;

	# EGL_ANGLE_metal_shared_event_sync
	eglCopyMetalSharedEventANGLE;

	# EGL_ANGLE_power_preference
	eglForceGPUSwitchANGLE;
	eglHandleGPUSwitchANGLE;
	eglReacquireHighPowerGPUANGLE;
	eglReleaseHighPowerGPUANGLE;

	# EGL_ANGLE_prepare_swap_buffers
	eglPrepareSwapBuffersANGLE;

	# EGL_ANGLE_program_cache_control
	eglProgramCacheGetAttribANGLE;
	eglProgramCachePopulateANGLE;
	eglProgramCacheQueryANGLE;
	eglProgramCacheResizeANGLE;

	# EGL_ANGLE_query_surface_pointer
	eglQuerySurfacePointerANGLE;

	# EGL_ANGLE_stream_producer_d3d_texture
	eglCreateStreamProducerD3DTextureANGLE;
	eglStreamPostD3DTextureANGLE;

	# EGL_ANGLE_sync_control_rate
	eglGetMscRateANGLE;

	# EGL_ANGLE_vulkan_image
	eglExportVkImageANGLE;

	# EGL_CHROMIUM_sync_control
	eglGetSyncValuesCHROMIUM;

	# EGL_EXT_device_query
	eglQueryDeviceAttribEXT;
	eglQueryDeviceStringEXT;
	eglQueryDisplayAttribEXT;

	# EGL_EXT_image_dma_buf_import_modifiers
	eglQueryDmaBufFormatsEXT;
	eglQueryDmaBufModifiersEXT;

	# EGL_EXT_platform_base
	eglCreatePlatformPixmapSurfaceEXT;
	eglCreatePlatformWindowSurfaceEXT;
	eglGetPlatformDisplayEXT;

	# EGL_KHR_debug
	eglDebugMessageControlKHR;
	eglLabelObjectKHR;
	eglQueryDebugKHR;

	# EGL_KHR_fence_sync
	eglClientWaitSyncKHR;
	eglCreateSyncKHR;
	eglDestroySyncKHR;
	eglGetSyncAttribKHR;

	# EGL_KHR_image
	eglCreateImageKHR;
	eglDestroyImageKHR;

	# EGL_KHR_lock_surface3
	eglLockSurfaceKHR;
	eglQuerySurface64KHR;
	eglUnlockSurfaceKHR;

	# EGL_KHR_partial_update
	eglSetDamageRegionKHR;

	# EGL_KHR_reusable_sync
	eglSignalSyncKHR;

	# EGL_KHR_stream
	eglCreateStreamKHR;
	eglDestroyStreamKHR;
	eglQueryStreamKHR;
	eglQueryStreamu64KHR;
	eglStreamAttribKHR;

	# EGL_KHR_stream_consumer_gltexture
	eglStreamConsumerAcquireKHR;
	eglStreamConsumerGLTextureExternalKHR;
	eglStreamConsumerReleaseKHR;

	# EGL_KHR_swap_buffers_with_damage
	eglSwapBuffersWithDamageKHR;

	# EGL_KHR_wait_sync
	eglWaitSyncKHR;

	# EGL_NV_post_sub_buffer
	eglPostSubBufferNV;

	# EGL_NV_stream_consumer_gltexture_yuv
	eglStreamConsumerGLTextureExternalAttribsNV;

local:
	*;
};