/*
* Copyright (C) 2011 The Android Open Source Project
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
#include "eglDisplay.h"
#include "HostConnection.h"
#include "KeyedVectorUtils.h"

#include <string>
#include <vector>

#include <dlfcn.h>
#include <GLES3/gl31.h>

#include <system/graphics.h>

#include "aemu/base/Path.h"
#include "gfxstream/common/logging.h"

static const int systemEGLVersionMajor = 1;
static const int systemEGLVersionMinor = 4;
static const char systemEGLVendor[] = "Google Android emulator";

// list of extensions supported by this EGL implementation
//  NOTE that each extension name should be suffixed with space
static const char systemStaticEGLExtensions[] =
            "EGL_ANDROID_image_native_buffer "
            "EGL_KHR_fence_sync "
            "EGL_KHR_image_base "
            "EGL_KHR_gl_texture_2d_image ";

// extensions to add dynamically depending on host-side support
static const char kDynamicEGLExtNativeSync[] = "EGL_ANDROID_native_fence_sync ";
static const char kDynamicEGLExtWaitSync[] = "EGL_KHR_wait_sync ";

static void *s_gles_lib = NULL;
static void *s_gles2_lib = NULL;

// The following function will be called when we (libEGL)
// gets unloaded
// At this point we want to unload the gles libraries we
// might have loaded during initialization
static void __attribute__ ((destructor)) do_on_unload(void)
{
    if (s_gles_lib) {
        dlclose(s_gles_lib);
    }

    if (s_gles2_lib) {
        dlclose(s_gles2_lib);
    }
}

eglDisplay::eglDisplay() :
    m_initialized(false),
    m_major(0),
    m_minor(0),
    m_hostRendererVersion(0),
    m_numConfigs(0),
    m_numConfigAttribs(0),
    m_attribs(),
    m_configs(NULL),
    m_gles_iface(NULL),
    m_gles2_iface(NULL),
    m_versionString(NULL),
    m_vendorString(NULL),
    m_extensionString(NULL),
    m_hostDriverCaps_knownMajorVersion(0),
    m_hostDriverCaps_knownMinorVersion(0)
{
    pthread_mutex_init(&m_lock, NULL);
    pthread_mutex_init(&m_ctxLock, NULL);
    pthread_mutex_init(&m_surfaceLock, NULL);
}

eglDisplay::~eglDisplay()
{
    terminate();
    pthread_mutex_destroy(&m_lock);
    pthread_mutex_destroy(&m_ctxLock);
    pthread_mutex_destroy(&m_surfaceLock);
}



bool eglDisplay::initialize(EGLClient_eglInterface *eglIface)
{
    pthread_mutex_lock(&m_lock);
    if (!m_initialized) {

        //
        // load GLES client API
        //
        m_gles_iface = loadGLESClientAPI("libGLESv1_CM_emulation",
                                         eglIface,
                                         &s_gles_lib);
        if (!m_gles_iface) {
            pthread_mutex_unlock(&m_lock);
            GFXSTREAM_ERROR("Failed to load gles1 iface");
            return false;
        }

        m_gles2_iface = loadGLESClientAPI("libGLESv2_emulation",
                                          eglIface,
                                          &s_gles2_lib);

        //
        // establish connection with the host
        //
        HostConnection *hcon = HostConnection::get();
        if (!hcon) {
            pthread_mutex_unlock(&m_lock);
            GFXSTREAM_ERROR("Failed to establish connection with the host.");
            return false;
        }

        //
        // get renderControl encoder instance
        //
        renderControl_encoder_context_t *rcEnc = hcon->rcEncoder();
        if (!rcEnc) {
            pthread_mutex_unlock(&m_lock);
            GFXSTREAM_ERROR("Failed to get renderControl encoder instance");
            return false;
        }

        //
        // Query host reneder and EGL version
        //
        m_hostRendererVersion = rcEnc->rcGetRendererVersion(rcEnc);
        EGLint status = rcEnc->rcGetEGLVersion(rcEnc, &m_major, &m_minor);
        if (status != EGL_TRUE) {
            // host EGL initialization failed !!
            pthread_mutex_unlock(&m_lock);
            return false;
        }

        //
        // Take minimum version beween what we support and what the host support
        //
        if (m_major > systemEGLVersionMajor) {
            m_major = systemEGLVersionMajor;
            m_minor = systemEGLVersionMinor;
        }
        else if (m_major == systemEGLVersionMajor &&
                 m_minor > systemEGLVersionMinor) {
            m_minor = systemEGLVersionMinor;
        }

        //
        // Query the host for the set of configs
        //
        m_numConfigs = rcEnc->rcGetNumConfigs(rcEnc, (uint32_t*)&m_numConfigAttribs);
        if (m_numConfigs <= 0 || m_numConfigAttribs <= 0) {
            // just sanity check - should never happen
            pthread_mutex_unlock(&m_lock);
            return false;
        }

        uint32_t nInts = m_numConfigAttribs * (m_numConfigs + 1);
        EGLint tmp_buf[nInts];

        m_configs = new EGLint[nInts-m_numConfigAttribs];

        if (!m_configs) {
            pthread_mutex_unlock(&m_lock);
            return false;
        }

        EGLint n = rcEnc->rcGetConfigs(rcEnc, nInts*sizeof(EGLint), (GLuint*)tmp_buf);
        if (n != m_numConfigs) {
            pthread_mutex_unlock(&m_lock);
            return false;
        }

        // Fill the attributes vector.
        // The first m_numConfigAttribs values of tmp_buf are the actual attributes enums.
        for (int i=0; i<m_numConfigAttribs; i++) {
            m_attribs[tmp_buf[i]] = i;
        }

        memcpy(m_configs, tmp_buf + m_numConfigAttribs,
               m_numConfigs*m_numConfigAttribs*sizeof(EGLint));

        m_initialized = true;
    }
    pthread_mutex_unlock(&m_lock);

    processConfigs();

    return true;
}

void eglDisplay::processConfigs()
{
    for (intptr_t i=0; i<m_numConfigs; i++) {
        EGLConfig config = getConfigAtIndex(i);
        uint32_t format;
        if (getConfigNativePixelFormat(config, &format)) {
            setConfigAttrib(config, EGL_NATIVE_VISUAL_ID, format);
        }
    }
}

void eglDisplay::terminate()
{
    pthread_mutex_lock(&m_lock);
    if (m_initialized) {
        // Cannot use the for loop in the following code because
        // eglDestroyContext may erase elements.
        EGLContextSet::iterator ctxIte = m_contexts.begin();
        while (ctxIte != m_contexts.end()) {
            EGLContextSet::iterator ctxToDelete = ctxIte;
            ctxIte ++;
            eglDestroyContext(static_cast<EGLDisplay>(this), *ctxToDelete);
        }
        EGLSurfaceSet::iterator surfaceIte = m_surfaces.begin();
        while (surfaceIte != m_surfaces.end()) {
            EGLSurfaceSet::iterator surfaceToDelete = surfaceIte;
            surfaceIte ++;
            eglDestroySurface(static_cast<EGLDisplay>(this), *surfaceToDelete);
        }
        m_initialized = false;
        delete [] m_configs;
        m_configs = NULL;

        if (m_versionString) {
            free(m_versionString);
            m_versionString = NULL;
        }
        if (m_vendorString) {
            free(m_vendorString);
            m_vendorString = NULL;
        }
        if (m_extensionString) {
            free(m_extensionString);
            m_extensionString = NULL;
        }
    }
    pthread_mutex_unlock(&m_lock);
}

#ifdef __APPLE__
#define LIBSUFFIX ".dylib"
#else
#ifdef _WIN32
#define LIBSUFFIX ".dll"
#else
#define LIBSUFFIX ".so"
#endif // !_WIN32 (linux)
#endif // !__APPLE__

#define PARTITION "/system"
#if __LP64__
#define LIBDIR "/lib64/egl/"
#else
#define LIBDIR "/lib/egl/"
#endif // !__LP64__

EGLClient_glesInterface *eglDisplay::loadGLESClientAPI(const char *basename,
                                                       EGLClient_eglInterface *eglIface,
                                                       void **libHandle)
{
    std::vector<std::string> paths;
#if defined(__ANDROID__)
    // Try to load from the current linker namespace first.
    paths.push_back(basename + std::string(LIBSUFFIX));
    // And then look into the known location.
    paths.push_back(std::string(PARTITION) +
                    std::string(LIBDIR) +
                    basename +
                    std::string(LIBSUFFIX));
#else
    const std::string directory = gfxstream::guest::getProgramDirectory();
    paths.push_back(directory + "/" + basename + LIBSUFFIX);
    paths.push_back(directory + "/" + basename + "_with_host" + LIBSUFFIX);
    paths.push_back(directory + "/lib64/" + basename + LIBSUFFIX);
    paths.push_back(directory + "/lib64/" + basename + "_with_host" + LIBSUFFIX);
#endif

    void* lib = nullptr;
    for (const std::string& path : paths) {
        GFXSTREAM_INFO("Opening %s", path.c_str());
        lib = dlopen(path.c_str(), RTLD_NOW);
        if (lib) {
            break;
        }
    }
    if (!lib) {
        GFXSTREAM_ERROR("Failed to dlopen %s", basename);
        return NULL;
    }

    init_emul_gles_t init_gles_func = (init_emul_gles_t)dlsym(lib,"init_emul_gles");
    if (!init_gles_func) {
        GFXSTREAM_ERROR("Failed to find init_emul_gles");
        dlclose((void*)lib);
        return NULL;
    }

    *libHandle = lib;
    return (*init_gles_func)(eglIface);
}

static char *queryHostEGLString(EGLint name)
{
    HostConnection *hcon = HostConnection::get();
    if (hcon) {
        renderControl_encoder_context_t *rcEnc = hcon->rcEncoder();
        if (rcEnc) {
            int n = rcEnc->rcQueryEGLString(rcEnc, name, NULL, 0);
            if (n < 0) {
                // allocate space for the string.
                char *str = (char *)malloc(-n);
                n = rcEnc->rcQueryEGLString(rcEnc, name, str, -n);
                if (n > 0) {
                    return str;
                }

                free(str);
            }
        }
    }

    return NULL;
}

static char *buildExtensionString()
{
    //Query host extension string
    char *hostExt = queryHostEGLString(EGL_EXTENSIONS);
    if (!hostExt || (hostExt[1] == '\0')) {
        // no extensions on host - only static extension list supported
        return strdup(systemStaticEGLExtensions);
    }

    int n = strlen(hostExt);
    if (n > 0) {
        char *initialEGLExts;
        char *finalEGLExts;

        HostConnection *hcon = HostConnection::get();
        // If we got here, we must have succeeded in queryHostEGLString
        // and we thus should have a valid connection
        assert(hcon);

        asprintf(&initialEGLExts,"%s%s", systemStaticEGLExtensions, hostExt);

        std::string dynamicEGLExtensions;

        if ((hcon->rcEncoder()->hasVirtioGpuNativeSync() || hcon->rcEncoder()->hasNativeSync()) &&
            !strstr(initialEGLExts, kDynamicEGLExtNativeSync)) {
            dynamicEGLExtensions += kDynamicEGLExtNativeSync;

            if (hcon->rcEncoder()->hasVirtioGpuNativeSync() || hcon->rcEncoder()->hasNativeSyncV3()) {
                dynamicEGLExtensions += kDynamicEGLExtWaitSync;
            }
        }

        asprintf(&finalEGLExts, "%s%s", initialEGLExts, dynamicEGLExtensions.c_str());

        free(initialEGLExts);
        free((char*)hostExt);
        return finalEGLExts;
    }
    else {
        free((char*)hostExt);
        return strdup(systemStaticEGLExtensions);
    }
}

const char *eglDisplay::queryString(EGLint name)
{
    if (name == EGL_CLIENT_APIS) {
        return "OpenGL_ES";
    }
    else if (name == EGL_VERSION) {
        pthread_mutex_lock(&m_lock);
        if (m_versionString) {
            pthread_mutex_unlock(&m_lock);
            return m_versionString;
        }

        // build version string
        asprintf(&m_versionString, "%d.%d", m_major, m_minor);
        pthread_mutex_unlock(&m_lock);

        return m_versionString;
    }
    else if (name == EGL_VENDOR) {
        pthread_mutex_lock(&m_lock);
        if (m_vendorString) {
            pthread_mutex_unlock(&m_lock);
            return m_vendorString;
        }

        // build vendor string
        const char *hostVendor = queryHostEGLString(EGL_VENDOR);

        if (hostVendor) {
            asprintf(&m_vendorString, "%s Host: %s",
                                     systemEGLVendor, hostVendor);
            free((char*)hostVendor);
        }
        else {
            m_vendorString = (char *)systemEGLVendor;
        }
        pthread_mutex_unlock(&m_lock);

        return m_vendorString;
    }
    else if (name == EGL_EXTENSIONS) {
        pthread_mutex_lock(&m_lock);
        if (m_extensionString) {
            pthread_mutex_unlock(&m_lock);
            return m_extensionString;
        }

        // build extension string
        m_extensionString = buildExtensionString();
        pthread_mutex_unlock(&m_lock);

        return m_extensionString;
    }
    else {
        GFXSTREAM_ERROR("Unknown name %d.", name);
        return NULL;
    }
}

/* To get the value of attribute <a> of config <c> use the following formula:
 * value = *(m_configs + (int)c*m_numConfigAttribs + a);
 */
EGLBoolean eglDisplay::getAttribValue(EGLConfig config, EGLint attribIdx, EGLint * value)
{
    if (attribIdx == ATTRIBUTE_NONE)
    {
        GFXSTREAM_ERROR("Bad attribute idx.");
        return EGL_FALSE;
    }
    *value = *(m_configs + (intptr_t)(getIndexOfConfig(config))*m_numConfigAttribs + attribIdx);
    return EGL_TRUE;
}

#define EGL_COLOR_COMPONENT_TYPE_EXT 0x3339
#define EGL_COLOR_COMPONENT_TYPE_FIXED_EXT 0x333A

EGLConfig eglDisplay::getConfigAtIndex(uint32_t index) const {
    uintptr_t asPtr = (uintptr_t)index;
    return (EGLConfig)(asPtr + 1);
}

uint32_t eglDisplay::getIndexOfConfig(EGLConfig config) const {
    uintptr_t asInteger = (uintptr_t)config;
    return (uint32_t)(asInteger - 1);
}

bool eglDisplay::isValidConfig(EGLConfig cfg) const {
    uint32_t index = getIndexOfConfig(cfg);
    intptr_t asInt = (intptr_t)index;
    return !(asInt < 0 || asInt > m_numConfigs);
}

EGLBoolean eglDisplay::getConfigAttrib(EGLConfig config, EGLint attrib, EGLint * value)
{
    if (attrib == EGL_FRAMEBUFFER_TARGET_ANDROID) {
        *value = EGL_TRUE;
        return EGL_TRUE;
    }
    if (attrib == EGL_COVERAGE_SAMPLES_NV ||
        attrib == EGL_COVERAGE_BUFFERS_NV) {
        *value = 0;
        return EGL_TRUE;
    }
    if (attrib == EGL_DEPTH_ENCODING_NV) {
        *value = EGL_DEPTH_ENCODING_NONE_NV;
        return EGL_TRUE;
    }
    if  (attrib == EGL_COLOR_COMPONENT_TYPE_EXT) {
        *value = EGL_COLOR_COMPONENT_TYPE_FIXED_EXT;
        return EGL_TRUE;
    }
    //Though it seems that valueFor() is thread-safe, we don't take chanses
    pthread_mutex_lock(&m_lock);
    EGLBoolean ret =
        getAttribValue(
            config,
            findObjectOrDefault(
                m_attribs, attrib, EGL_DONT_CARE),
            value);
    pthread_mutex_unlock(&m_lock);
    return ret;
}

void eglDisplay::dumpConfig(EGLConfig config)
{
    EGLint value = 0;
    for (int i=0; i<m_numConfigAttribs; i++) {
        getAttribValue(config, i, &value);
    }
}

/* To set the value of attribute <a> of config <c> use the following formula:
 * *(m_configs + (int)c*m_numConfigAttribs + a) = value;
 */
EGLBoolean eglDisplay::setAttribValue(EGLConfig config, EGLint attribIdx, EGLint value)
{
    if (attribIdx == ATTRIBUTE_NONE)
    {
        GFXSTREAM_ERROR("Bad attribute idx");
        return EGL_FALSE;
    }
    *(m_configs + (intptr_t)(getIndexOfConfig(config))*m_numConfigAttribs + attribIdx) = value;
    return EGL_TRUE;
}

EGLBoolean eglDisplay::setConfigAttrib(EGLConfig config, EGLint attrib, EGLint value)
{
    //Though it seems that valueFor() is thread-safe, we don't take chanses
    pthread_mutex_lock(&m_lock);
    EGLBoolean ret =
        setAttribValue(
            config,
            findObjectOrDefault(
                m_attribs,
                attrib,
                EGL_DONT_CARE),
            value);
    pthread_mutex_unlock(&m_lock);
    return ret;
}


EGLBoolean eglDisplay::getConfigNativePixelFormat(EGLConfig config, uint32_t * format)
{
    EGLint redSize, blueSize, greenSize, alphaSize;

    if (!(
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_RED_SIZE, EGL_DONT_CARE),
                &redSize) &&
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_BLUE_SIZE, EGL_DONT_CARE),
                &blueSize) &&
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_GREEN_SIZE, EGL_DONT_CARE),
                &greenSize) &&
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_ALPHA_SIZE, EGL_DONT_CARE),
                &alphaSize))) {
        GFXSTREAM_ERROR("Couldn't find value for one of the pixel format attributes");
        return EGL_FALSE;
    }

    //calculate the GL internal format
    if ((redSize==8)&&(greenSize==8)&&(blueSize==8)&&(alphaSize==8)) *format = HAL_PIXEL_FORMAT_RGBA_8888; //XXX: BGR?
    else if ((redSize==8)&&(greenSize==8)&&(blueSize==8)&&(alphaSize==0)) *format = HAL_PIXEL_FORMAT_RGBX_8888; //XXX or HAL_PIXEL_FORMAT_RGB_888
    else if ((redSize==5)&&(greenSize==6)&&(blueSize==5)&&(alphaSize==0)) *format = HAL_PIXEL_FORMAT_RGB_565;
    else {
        return EGL_FALSE;
    }
    return EGL_TRUE;
}
EGLBoolean eglDisplay::getConfigGLPixelFormat(EGLConfig config, GLenum * format)
{
    EGLint redSize, blueSize, greenSize, alphaSize;

    if (!(
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_RED_SIZE, EGL_DONT_CARE),
                &redSize) &&
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_BLUE_SIZE, EGL_DONT_CARE),
                &blueSize) &&
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_GREEN_SIZE, EGL_DONT_CARE),
                &greenSize) &&
            getAttribValue(
                config,
                findObjectOrDefault(m_attribs, EGL_ALPHA_SIZE, EGL_DONT_CARE),
                &alphaSize))) {
        GFXSTREAM_ERROR("Couldn't find value for one of the pixel format attributes");
        return EGL_FALSE;
    }

    //calculate the GL internal format
    if ((redSize == greenSize) && (redSize == blueSize) &&
        ((redSize == 8) || (redSize == 16) || (redSize == 32)))
    {
        if (alphaSize == 0) *format = GL_RGB;
        else *format = GL_RGBA;
    }
    else if ((redSize==5)&&(greenSize==6)&&(blueSize==5)&&(alphaSize==0)) *format = GL_RGB565_OES;
    else if ((redSize==5)&&(greenSize==5)&&(blueSize==5)&&(alphaSize==1)) *format = GL_RGB5_A1_OES;
    else if ((redSize==4)&&(greenSize==4)&&(blueSize==4)&&(alphaSize==4)) *format = GL_RGBA4_OES;
    else return EGL_FALSE;

    return EGL_TRUE;
}

void eglDisplay::onCreateContext(EGLContext ctx) {
    pthread_mutex_lock(&m_ctxLock);
    m_contexts.insert(ctx);
    pthread_mutex_unlock(&m_ctxLock);
}

void eglDisplay::onCreateSurface(EGLSurface surface) {
    pthread_mutex_lock(&m_surfaceLock);
    m_surfaces.insert(surface);
    pthread_mutex_unlock(&m_surfaceLock);
}

void eglDisplay::onDestroyContext(EGLContext ctx) {
    pthread_mutex_lock(&m_ctxLock);
    m_contexts.erase(ctx);
    pthread_mutex_unlock(&m_ctxLock);
}

void eglDisplay::onDestroySurface(EGLSurface surface) {
    pthread_mutex_lock(&m_surfaceLock);
    m_surfaces.erase(surface);
    pthread_mutex_unlock(&m_surfaceLock);
}

bool eglDisplay::isContext(EGLContext ctx) {
    pthread_mutex_lock(&m_ctxLock);
    bool res = m_contexts.find(ctx) != m_contexts.end();
    pthread_mutex_unlock(&m_ctxLock);
    return res;
}

bool eglDisplay::isSurface(EGLSurface surface) {
    pthread_mutex_lock(&m_surfaceLock);
    bool res = m_surfaces.find(surface) != m_surfaces.end();
    pthread_mutex_unlock(&m_surfaceLock);
    return res;
}

HostDriverCaps eglDisplay::getHostDriverCaps(int majorVersion, int minorVersion) {
    pthread_mutex_lock(&m_lock);
    if (majorVersion <= m_hostDriverCaps_knownMajorVersion &&
        minorVersion <= m_hostDriverCaps_knownMinorVersion) {
        pthread_mutex_unlock(&m_lock);
        return m_hostDriverCaps;
    }

    memset(&m_hostDriverCaps, 0x0, sizeof(m_hostDriverCaps));

    m_hostDriverCaps.max_color_attachments = 8;

    // Can we query gles2?
    if (majorVersion >= 1) {
        m_gles2_iface->getIntegerv(GL_MAX_VERTEX_ATTRIBS, &m_hostDriverCaps.max_vertex_attribs);
        m_gles2_iface->getIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &m_hostDriverCaps.max_combined_texture_image_units);

        m_gles2_iface->getIntegerv(GL_MAX_TEXTURE_SIZE, &m_hostDriverCaps.max_texture_size);
        m_gles2_iface->getIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &m_hostDriverCaps.max_texture_size_cube_map);
        m_gles2_iface->getIntegerv(GL_MAX_RENDERBUFFER_SIZE, &m_hostDriverCaps.max_renderbuffer_size);
        m_hostDriverCaps_knownMajorVersion = 2;
    }

    // Can we query gles3.0?
    if (majorVersion >= 3) {
        m_gles2_iface->getIntegerv(GL_MAX_COLOR_ATTACHMENTS, &m_hostDriverCaps.max_color_attachments);
        m_gles2_iface->getIntegerv(GL_MAX_DRAW_BUFFERS, &m_hostDriverCaps.max_draw_buffers);
        m_gles2_iface->getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_hostDriverCaps.ubo_offset_alignment);
        m_gles2_iface->getIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &m_hostDriverCaps.max_uniform_buffer_bindings);
        m_gles2_iface->getIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &m_hostDriverCaps.max_transform_feedback_separate_attribs);
        m_gles2_iface->getIntegerv(GL_MAX_3D_TEXTURE_SIZE, &m_hostDriverCaps.max_texture_size_3d);
        m_gles2_iface->getIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &m_hostDriverCaps.max_array_texture_layers);

        m_hostDriverCaps_knownMajorVersion = 3;

        // Can we query gles3.1?
        if (minorVersion >= 1) {
            m_gles2_iface->getIntegerv(GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, &m_hostDriverCaps.max_atomic_counter_buffer_bindings);
            m_gles2_iface->getIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &m_hostDriverCaps.max_shader_storage_buffer_bindings);
            m_gles2_iface->getIntegerv(GL_MAX_VERTEX_ATTRIB_BINDINGS, &m_hostDriverCaps.max_vertex_attrib_bindings);
            m_gles2_iface->getIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &m_hostDriverCaps.max_vertex_attrib_stride);
            m_gles2_iface->getIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &m_hostDriverCaps.ssbo_offset_alignment);
            m_hostDriverCaps_knownMinorVersion = 1;
        }
    }

    pthread_mutex_unlock(&m_lock);

    return m_hostDriverCaps;
}

