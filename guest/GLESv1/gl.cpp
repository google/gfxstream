/*
* Copyright 2011 The Android Open Source Project
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

#include "GLES/gl.h"

#include "EGLClientIface.h"
#include "EGLImage.h"
#include "GLES/glext.h"
#include "GLEncoder.h"
#include "HostConnection.h"
#include "ThreadInfo.h"
#include "gfxstream/common/logging.h"

//XXX: fix this macro to get the context from fast tls path
#define GET_CONTEXT GLEncoder * ctx = getEGLThreadInfo()->hostConn->glEncoder();

#include "gl_entry.cpp"

//The functions table
#include "gl_ftable.h"

static EGLClient_eglInterface * s_egl = NULL;
static EGLClient_glesInterface * s_gl = NULL;

#define DEFINE_AND_VALIDATE_HOST_CONNECTION(ret)                     \
    HostConnection* hostCon = HostConnection::get();                 \
    if (!hostCon) {                                                  \
        GFXSTREAM_ERROR("egl: Failed to get host connection.");      \
        return ret;                                                  \
    }                                                                \
    renderControl_encoder_context_t* rcEnc = hostCon->rcEncoder();   \
    if (!rcEnc) {                                                    \
        GFXSTREAM_ERROR("egl: Failed to get renderControl encoder context."); \
        return ret;                                                  \
    }                                                                \
    auto* grallocHelper = hostCon->grallocHelper();                  \
    if (!grallocHelper) {                                            \
        GFXSTREAM_ERROR("egl: Failed to get grallocHelper.");        \
        return ret;                                                  \
    }                                                                \
    auto* anwHelper = hostCon->anwHelper();                          \
    if (!anwHelper) {                                                \
        GFXSTREAM_ERROR("egl: Failed to get anwHelper.");            \
        return ret;                                                  \
    }

//GL extensions
void glEGLImageTargetTexture2DOES(void * self, GLenum target, GLeglImageOES img)
{
    (void)self;

    EGLImage_t *image = (EGLImage_t*)img;

    if (image->target == EGL_NATIVE_BUFFER_ANDROID) {
        DEFINE_AND_VALIDATE_HOST_CONNECTION();

        EGLClientBuffer buffer = image->buffer;
        if (!anwHelper->isValid(buffer)) {
            GFXSTREAM_ERROR("Invalid native buffer.");
            return;
        }

        GET_CONTEXT;
        ctx->override2DTextureTarget(target);

        const int hostHandle = anwHelper->getHostHandle(buffer, grallocHelper);
        rcEnc->rcBindTexture(rcEnc, hostHandle);

        ctx->restore2DTextureTarget();
    } else if (image->target == EGL_GL_TEXTURE_2D_KHR) {
        GET_CONTEXT;
        ctx->override2DTextureTarget(target);
        GLeglImageOES hostImage = reinterpret_cast<GLeglImageOES>((intptr_t)image->host_egl_image);
        ctx->m_glEGLImageTargetTexture2DOES_enc(self, target, hostImage);
        ctx->restore2DTextureTarget();
    }
}

void glEGLImageTargetRenderbufferStorageOES(void *self, GLenum target, GLeglImageOES img)
{
    (void)self;
    (void)target;

    EGLImage_t *image = (EGLImage_t*)img;

    if (image->target == EGL_NATIVE_BUFFER_ANDROID) {
        DEFINE_AND_VALIDATE_HOST_CONNECTION();

        EGLClientBuffer buffer = image->buffer;
        if (!anwHelper->isValid(buffer)) {
            GFXSTREAM_ERROR("Invalid native buffer.");
            return;
        }

        const int hostHandle = anwHelper->getHostHandle(buffer, grallocHelper);
        rcEnc->rcBindRenderbuffer(rcEnc, hostHandle);
    } else {
        //TODO
    }

    return;
}

void * getProcAddress(const char * procname)
{
    // search in GL function table
    for (int i=0; i<gl_num_funcs; i++) {
        if (!strcmp(gl_funcs_by_name[i].name, procname)) {
            return gl_funcs_by_name[i].proc;
        }
    }
    return NULL;
}

void finish()
{
    glFinish();
}

void getIntegerv(unsigned int pname, int* param)
{
    glGetIntegerv((GLenum)pname, (GLint*)param);
}

const GLubyte *my_glGetString (void *self, GLenum name)
{
    (void)self;

    //see ref in https://www.khronos.org/opengles/sdk/docs/man
    //name in glGetString can be one of the following five values
    switch (name) {
        case GL_VERSION:
        case GL_VENDOR:
        case GL_RENDERER:
        case GL_SHADING_LANGUAGE_VERSION:
        case GL_EXTENSIONS:
            if (s_egl) {
                return (const GLubyte*)s_egl->getGLString(name);
            }
            break;
        default:
            GET_CONTEXT;
            ctx->setError(GL_INVALID_ENUM);
            break;
    }
    return NULL;
}

void init()
{
    GET_CONTEXT;
    ctx->m_glEGLImageTargetTexture2DOES_enc = ctx->glEGLImageTargetTexture2DOES;
    ctx->glEGLImageTargetTexture2DOES = &glEGLImageTargetTexture2DOES;
    ctx->glEGLImageTargetRenderbufferStorageOES = &glEGLImageTargetRenderbufferStorageOES;
    ctx->glGetString = &my_glGetString;
}

extern "C" {
EGLClient_glesInterface * init_emul_gles(EGLClient_eglInterface *eglIface)
{
    s_egl = eglIface;

    if (!s_gl) {
        s_gl = new EGLClient_glesInterface();
        s_gl->getProcAddress = getProcAddress;
        s_gl->finish = finish;
        s_gl->init = init;
        s_gl->getIntegerv = getIntegerv;
    }

    return s_gl;
}
} //extern


