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

#include "GL2Encoder.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES2/gl2platform.h>
#include <GLES3/gl3.h>
#include <GLES3/gl31.h>
#include <assert.h>
#include <ctype.h>

#include <map>
#include <string>

#include "EncoderDebug.h"
#include "GLESTextureUtils.h"
#include "GLESv2Validation.h"
#include "gfxstream/common/logging.h"

using gfxstream::guest::BufferData;
using gfxstream::guest::ChecksumCalculator;
using gfxstream::guest::FBO_ATTACHMENT_RENDERBUFFER;
using gfxstream::guest::FBO_ATTACHMENT_TEXTURE;
using gfxstream::guest::FboFormatInfo;
using gfxstream::guest::GLClientState;
using gfxstream::guest::GLSharedGroupPtr;
using gfxstream::guest::IOStream;
using gfxstream::guest::ProgramData;
using gfxstream::guest::ShaderData;
using gfxstream::guest::ShaderProgramData;
using gfxstream::guest::gles2::ProgramBinaryInfo;

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static GLubyte *gVendorString= (GLubyte *) "Android";
static GLubyte *gRendererString= (GLubyte *) "Android HW-GLES 3.0";
static GLubyte *gVersionString= (GLubyte *) "OpenGL ES 3.0";
static GLubyte *gExtensionsString= (GLubyte *) "GL_OES_EGL_image_external ";

#define SET_ERROR_IF(condition, err) if((condition)) { \
        GFXSTREAM_ERROR("GL error 0x%x condition [%s].", err, #condition); \
        ctx->setError(err); \
        return; \
    }

#define SET_ERROR_WITH_MESSAGE_IF(condition, err, generator, genargs) if ((condition)) { \
        std::string msg = generator genargs; \
        GFXSTREAM_ERROR("GL error 0x%x: %s", err, msg.c_str()); \
        ctx->setError(err); \
        return; \
    } \

#define RET_AND_SET_ERROR_IF(condition, err, ret) if((condition)) { \
        GFXSTREAM_ERROR("GL error 0x%x.", err); \
        ctx->setError(err);  \
        return ret; \
    } \

#define RET_AND_SET_ERROR_WITH_MESSAGE_IF(condition, err, ret, generator, genargs) if((condition)) { \
        std::string msg = generator genargs; \
        GFXSTREAM_ERROR("GL error 0x%x: %s", err, msg.c_str()); \
        ctx->setError(err);   \
        return ret; \
    } \

GL2Encoder::GL2Encoder(IOStream *stream, ChecksumCalculator *protocol)
        : gl2_encoder_context_t(stream, protocol)
{
    m_currMajorVersion = 2;
    m_currMinorVersion = 0;
    m_hasAsyncUnmapBuffer = false;
    m_hasSyncBufferData = false;
    m_initialized = false;
    m_noHostError = false;
    m_state = NULL;
    m_error = GL_NO_ERROR;

    m_num_compressedTextureFormats = 0;
    m_max_combinedTextureImageUnits = 0;
    m_max_vertexTextureImageUnits = 0;
    m_max_array_texture_layers = 0;
    m_max_textureImageUnits = 0;
    m_max_cubeMapTextureSize = 0;
    m_max_renderBufferSize = 0;
    m_max_textureSize = 0;
    m_max_3d_textureSize = 0;
    m_max_vertexAttribStride = 0;

    m_max_transformFeedbackSeparateAttribs = 0;
    m_max_uniformBufferBindings = 0;
    m_max_colorAttachments = 0;
    m_max_drawBuffers = 0;

    m_max_atomicCounterBufferBindings = 0;
    m_max_shaderStorageBufferBindings = 0;
    m_max_vertexAttribBindings = 0;

    m_textureBufferOffsetAlign = 0;

    m_compressedTextureFormats = NULL;

    m_ssbo_offset_align = 0;
    m_ubo_offset_align = 0;

    m_drawCallFlushInterval = 800;
    m_drawCallFlushCount = 0;
    m_primitiveRestartEnabled = false;
    m_primitiveRestartIndex = 0;

    // overrides
#define OVERRIDE(name)  m_##name##_enc = this-> name ; this-> name = &s_##name
#define OVERRIDE_CUSTOM(name)  this-> name = &s_##name
#define OVERRIDEWITH(name, target)  do { \
    m_##target##_enc = this-> target; \
    this-> target = &s_##name; \
} while(0)
#define OVERRIDEOES(name) OVERRIDEWITH(name, name##OES)

    OVERRIDE(glFlush);
    OVERRIDE(glPixelStorei);
    OVERRIDE(glGetString);
    OVERRIDE(glBindBuffer);
    OVERRIDE(glBufferData);
    OVERRIDE(glBufferSubData);
    OVERRIDE(glDeleteBuffers);
    OVERRIDE(glDrawArrays);
    OVERRIDE(glDrawElements);
    OVERRIDE(glDrawArraysNullAEMU);
    OVERRIDE(glDrawElementsNullAEMU);
    OVERRIDE(glGetIntegerv);
    OVERRIDE(glGetFloatv);
    OVERRIDE(glGetBooleanv);
    OVERRIDE(glVertexAttribPointer);
    OVERRIDE(glEnableVertexAttribArray);
    OVERRIDE(glDisableVertexAttribArray);
    OVERRIDE(glGetVertexAttribiv);
    OVERRIDE(glGetVertexAttribfv);
    OVERRIDE(glGetVertexAttribPointerv);

    this->glShaderBinary = &s_glShaderBinary;
    this->glShaderSource = &s_glShaderSource;
    this->glFinish = &s_glFinish;

    OVERRIDE(glGetError);
    OVERRIDE(glLinkProgram);
    OVERRIDE(glDeleteProgram);
    OVERRIDE(glGetUniformiv);
    OVERRIDE(glGetUniformfv);
    OVERRIDE(glCreateProgram);
    OVERRIDE(glCreateShader);
    OVERRIDE(glDeleteShader);
    OVERRIDE(glAttachShader);
    OVERRIDE(glDetachShader);
    OVERRIDE(glGetAttachedShaders);
    OVERRIDE(glGetShaderSource);
    OVERRIDE(glGetShaderInfoLog);
    OVERRIDE(glGetProgramInfoLog);

    OVERRIDE(glGetUniformLocation);
    OVERRIDE(glUseProgram);

    OVERRIDE(glUniform1f);
    OVERRIDE(glUniform1fv);
    OVERRIDE(glUniform1i);
    OVERRIDE(glUniform1iv);
    OVERRIDE(glUniform2f);
    OVERRIDE(glUniform2fv);
    OVERRIDE(glUniform2i);
    OVERRIDE(glUniform2iv);
    OVERRIDE(glUniform3f);
    OVERRIDE(glUniform3fv);
    OVERRIDE(glUniform3i);
    OVERRIDE(glUniform3iv);
    OVERRIDE(glUniform4f);
    OVERRIDE(glUniform4fv);
    OVERRIDE(glUniform4i);
    OVERRIDE(glUniform4iv);
    OVERRIDE(glUniformMatrix2fv);
    OVERRIDE(glUniformMatrix3fv);
    OVERRIDE(glUniformMatrix4fv);

    OVERRIDE(glActiveTexture);
    OVERRIDE(glBindTexture);
    OVERRIDE(glDeleteTextures);
    OVERRIDE(glGetTexParameterfv);
    OVERRIDE(glGetTexParameteriv);
    OVERRIDE(glTexParameterf);
    OVERRIDE(glTexParameterfv);
    OVERRIDE(glTexParameteri);
    OVERRIDE(glTexParameteriv);
    OVERRIDE(glTexImage2D);
    OVERRIDE(glTexSubImage2D);
    OVERRIDE(glCopyTexImage2D);
    OVERRIDE(glTexBufferOES);
    OVERRIDE(glTexBufferRangeOES);
    OVERRIDE(glTexBufferEXT);
    OVERRIDE(glTexBufferRangeEXT);

    OVERRIDE(glEnableiEXT);
    OVERRIDE(glDisableiEXT);
    OVERRIDE(glBlendEquationiEXT);
    OVERRIDE(glBlendEquationSeparateiEXT);
    OVERRIDE(glBlendFunciEXT);
    OVERRIDE(glBlendFuncSeparateiEXT);
    OVERRIDE(glColorMaskiEXT);
    OVERRIDE(glIsEnablediEXT);

    OVERRIDE(glGenRenderbuffers);
    OVERRIDE(glDeleteRenderbuffers);
    OVERRIDE(glBindRenderbuffer);
    OVERRIDE(glRenderbufferStorage);
    OVERRIDE(glFramebufferRenderbuffer);

    OVERRIDE(glGenFramebuffers);
    OVERRIDE(glDeleteFramebuffers);
    OVERRIDE(glBindFramebuffer);
    OVERRIDE(glFramebufferParameteri);
    OVERRIDE(glFramebufferTexture2D);
    OVERRIDE(glFramebufferTexture3DOES);
    OVERRIDE(glGetFramebufferAttachmentParameteriv);

    OVERRIDE(glCheckFramebufferStatus);

    OVERRIDE(glGenVertexArrays);
    OVERRIDE(glDeleteVertexArrays);
    OVERRIDE(glBindVertexArray);
    OVERRIDEOES(glGenVertexArrays);
    OVERRIDEOES(glDeleteVertexArrays);
    OVERRIDEOES(glBindVertexArray);

    OVERRIDE_CUSTOM(glMapBufferOES);
    OVERRIDE_CUSTOM(glUnmapBufferOES);
    OVERRIDE_CUSTOM(glMapBufferRange);
    OVERRIDE_CUSTOM(glUnmapBuffer);
    OVERRIDE_CUSTOM(glFlushMappedBufferRange);

    OVERRIDE(glCompressedTexImage2D);
    OVERRIDE(glCompressedTexSubImage2D);

    OVERRIDE(glBindBufferRange);
    OVERRIDE(glBindBufferBase);

    OVERRIDE(glCopyBufferSubData);

    OVERRIDE(glGetBufferParameteriv);
    OVERRIDE(glGetBufferParameteri64v);
    OVERRIDE(glGetBufferPointerv);

    OVERRIDE_CUSTOM(glGetUniformIndices);

    OVERRIDE(glUniform1ui);
    OVERRIDE(glUniform2ui);
    OVERRIDE(glUniform3ui);
    OVERRIDE(glUniform4ui);
    OVERRIDE(glUniform1uiv);
    OVERRIDE(glUniform2uiv);
    OVERRIDE(glUniform3uiv);
    OVERRIDE(glUniform4uiv);
    OVERRIDE(glUniformMatrix2x3fv);
    OVERRIDE(glUniformMatrix3x2fv);
    OVERRIDE(glUniformMatrix2x4fv);
    OVERRIDE(glUniformMatrix4x2fv);
    OVERRIDE(glUniformMatrix3x4fv);
    OVERRIDE(glUniformMatrix4x3fv);

    OVERRIDE(glGetUniformuiv);
    OVERRIDE(glGetActiveUniformBlockiv);

    OVERRIDE(glGetVertexAttribIiv);
    OVERRIDE(glGetVertexAttribIuiv);

    OVERRIDE_CUSTOM(glVertexAttribIPointer);

    OVERRIDE(glVertexAttribDivisor);

    OVERRIDE(glRenderbufferStorageMultisample);
    OVERRIDE(glDrawBuffers);
    OVERRIDE(glReadBuffer);
    OVERRIDE(glFramebufferTextureLayer);
    OVERRIDE(glTexStorage2D);

    OVERRIDE_CUSTOM(glTransformFeedbackVaryings);
    OVERRIDE(glBeginTransformFeedback);
    OVERRIDE(glEndTransformFeedback);
    OVERRIDE(glPauseTransformFeedback);
    OVERRIDE(glResumeTransformFeedback);

    OVERRIDE(glTexImage3D);
    OVERRIDE(glTexSubImage3D);
    OVERRIDE(glTexStorage3D);
    OVERRIDE(glCompressedTexImage3D);
    OVERRIDE(glCompressedTexSubImage3D);

    OVERRIDE(glDrawArraysInstanced);
    OVERRIDE_CUSTOM(glDrawElementsInstanced);
    OVERRIDE_CUSTOM(glDrawRangeElements);

    OVERRIDE_CUSTOM(glGetStringi);
    OVERRIDE(glGetProgramBinary);
    OVERRIDE(glReadPixels);

    OVERRIDE(glEnable);
    OVERRIDE(glDisable);
    OVERRIDE(glClearBufferiv);
    OVERRIDE(glClearBufferuiv);
    OVERRIDE(glClearBufferfv);
    OVERRIDE(glBlitFramebuffer);
    OVERRIDE_CUSTOM(glGetInternalformativ);

    OVERRIDE(glGenerateMipmap);

    OVERRIDE(glBindSampler);
    OVERRIDE(glDeleteSamplers);

    OVERRIDE_CUSTOM(glFenceSync);
    OVERRIDE_CUSTOM(glClientWaitSync);
    OVERRIDE_CUSTOM(glWaitSync);
    OVERRIDE_CUSTOM(glDeleteSync);
    OVERRIDE_CUSTOM(glIsSync);
    OVERRIDE_CUSTOM(glGetSynciv);

    OVERRIDE(glGetIntegeri_v);
    OVERRIDE(glGetInteger64i_v);
    OVERRIDE(glGetInteger64v);
    OVERRIDE(glGetBooleani_v);

    OVERRIDE(glGetShaderiv);

    OVERRIDE(glActiveShaderProgram);
    OVERRIDE_CUSTOM(glCreateShaderProgramv);
    OVERRIDE(glProgramUniform1f);
    OVERRIDE(glProgramUniform1fv);
    OVERRIDE(glProgramUniform1i);
    OVERRIDE(glProgramUniform1iv);
    OVERRIDE(glProgramUniform1ui);
    OVERRIDE(glProgramUniform1uiv);
    OVERRIDE(glProgramUniform2f);
    OVERRIDE(glProgramUniform2fv);
    OVERRIDE(glProgramUniform2i);
    OVERRIDE(glProgramUniform2iv);
    OVERRIDE(glProgramUniform2ui);
    OVERRIDE(glProgramUniform2uiv);
    OVERRIDE(glProgramUniform3f);
    OVERRIDE(glProgramUniform3fv);
    OVERRIDE(glProgramUniform3i);
    OVERRIDE(glProgramUniform3iv);
    OVERRIDE(glProgramUniform3ui);
    OVERRIDE(glProgramUniform3uiv);
    OVERRIDE(glProgramUniform4f);
    OVERRIDE(glProgramUniform4fv);
    OVERRIDE(glProgramUniform4i);
    OVERRIDE(glProgramUniform4iv);
    OVERRIDE(glProgramUniform4ui);
    OVERRIDE(glProgramUniform4uiv);
    OVERRIDE(glProgramUniformMatrix2fv);
    OVERRIDE(glProgramUniformMatrix2x3fv);
    OVERRIDE(glProgramUniformMatrix2x4fv);
    OVERRIDE(glProgramUniformMatrix3fv);
    OVERRIDE(glProgramUniformMatrix3x2fv);
    OVERRIDE(glProgramUniformMatrix3x4fv);
    OVERRIDE(glProgramUniformMatrix4fv);
    OVERRIDE(glProgramUniformMatrix4x2fv);
    OVERRIDE(glProgramUniformMatrix4x3fv);

    OVERRIDE(glProgramParameteri);
    OVERRIDE(glUseProgramStages);
    OVERRIDE(glBindProgramPipeline);

    OVERRIDE(glGetProgramResourceiv);
    OVERRIDE(glGetProgramResourceIndex);
    OVERRIDE(glGetProgramResourceLocation);
    OVERRIDE(glGetProgramResourceName);
    OVERRIDE(glGetProgramPipelineInfoLog);

    OVERRIDE(glVertexAttribFormat);
    OVERRIDE(glVertexAttribIFormat);
    OVERRIDE(glVertexBindingDivisor);
    OVERRIDE(glVertexAttribBinding);
    OVERRIDE(glBindVertexBuffer);

    OVERRIDE_CUSTOM(glDrawArraysIndirect);
    OVERRIDE_CUSTOM(glDrawElementsIndirect);

    OVERRIDE(glTexStorage2DMultisample);

    OVERRIDE_CUSTOM(glGetGraphicsResetStatusEXT);
    OVERRIDE_CUSTOM(glReadnPixelsEXT);
    OVERRIDE_CUSTOM(glGetnUniformfvEXT);
    OVERRIDE_CUSTOM(glGetnUniformivEXT);

    OVERRIDE(glInvalidateFramebuffer);
    OVERRIDE(glInvalidateSubFramebuffer);

    OVERRIDE(glDispatchCompute);
    OVERRIDE(glDispatchComputeIndirect);

    OVERRIDE(glGenTransformFeedbacks);
    OVERRIDE(glDeleteTransformFeedbacks);
    OVERRIDE(glGenSamplers);
    OVERRIDE(glGenQueries);
    OVERRIDE(glDeleteQueries);

    OVERRIDE(glBindTransformFeedback);
    OVERRIDE(glBeginQuery);
    OVERRIDE(glEndQuery);

    OVERRIDE(glClear);
    OVERRIDE(glClearBufferfi);
    OVERRIDE(glCopyTexSubImage2D);
    OVERRIDE(glCopyTexSubImage3D);
    OVERRIDE(glCompileShader);
    OVERRIDE(glValidateProgram);
    OVERRIDE(glProgramBinary);

    OVERRIDE(glGetSamplerParameterfv);
    OVERRIDE(glGetSamplerParameteriv);
    OVERRIDE(glSamplerParameterf);
    OVERRIDE(glSamplerParameteri);
    OVERRIDE(glSamplerParameterfv);
    OVERRIDE(glSamplerParameteriv);

    OVERRIDE(glGetAttribLocation);

    OVERRIDE(glBindAttribLocation);
    OVERRIDE(glUniformBlockBinding);
    OVERRIDE(glGetTransformFeedbackVarying);
    OVERRIDE(glScissor);
    OVERRIDE(glDepthFunc);
    OVERRIDE(glViewport);
    OVERRIDE(glStencilFunc);
    OVERRIDE(glStencilFuncSeparate);
    OVERRIDE(glStencilOp);
    OVERRIDE(glStencilOpSeparate);
    OVERRIDE(glStencilMaskSeparate);
    OVERRIDE(glBlendEquation);
    OVERRIDE(glBlendEquationSeparate);
    OVERRIDE(glBlendFunc);
    OVERRIDE(glBlendFuncSeparate);
    OVERRIDE(glCullFace);
    OVERRIDE(glFrontFace);
    OVERRIDE(glLineWidth);
    OVERRIDE(glVertexAttrib1f);
    OVERRIDE(glVertexAttrib2f);
    OVERRIDE(glVertexAttrib3f);
    OVERRIDE(glVertexAttrib4f);
    OVERRIDE(glVertexAttrib1fv);
    OVERRIDE(glVertexAttrib2fv);
    OVERRIDE(glVertexAttrib3fv);
    OVERRIDE(glVertexAttrib4fv);
    OVERRIDE(glVertexAttribI4i);
    OVERRIDE(glVertexAttribI4ui);
    OVERRIDE(glVertexAttribI4iv);
    OVERRIDE(glVertexAttribI4uiv);

    OVERRIDE(glGetShaderPrecisionFormat);
    OVERRIDE(glGetProgramiv);
    OVERRIDE(glGetActiveUniform);
    OVERRIDE(glGetActiveUniformsiv);
    OVERRIDE(glGetActiveUniformBlockName);
    OVERRIDE(glGetActiveAttrib);
    OVERRIDE(glGetRenderbufferParameteriv);
    OVERRIDE(glGetQueryiv);
    OVERRIDE(glGetQueryObjectuiv);
    OVERRIDE(glIsEnabled);
    OVERRIDE(glHint);

    OVERRIDE(glGetFragDataLocation);

    OVERRIDE(glStencilMask);
    OVERRIDE(glClearStencil);
}

GL2Encoder::~GL2Encoder()
{
    delete m_compressedTextureFormats;
}

GLenum GL2Encoder::s_glGetError(void * self)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLenum err = ctx->getError();
    if(err != GL_NO_ERROR) {
        if (!ctx->m_noHostError) {
            ctx->m_glGetError_enc(ctx); // also clear host error
        }
        ctx->setError(GL_NO_ERROR);
        return err;
    }

    if (ctx->m_noHostError) {
        return GL_NO_ERROR;
    } else {
        return ctx->m_glGetError_enc(self);
    }
}

class GL2Encoder::ErrorUpdater {
public:
    ErrorUpdater(GL2Encoder* ctx) :
        mCtx(ctx),
        guest_error(ctx->getError()),
        host_error(ctx->m_glGetError_enc(ctx)) {
            if (ctx->m_noHostError) {
                host_error = GL_NO_ERROR;
            }
            // Preserve any existing GL error in the guest:
            // OpenGL ES 3.0.5 spec:
            // The command enum GetError( void ); is used to obtain error information.
            // Each detectable error is assigned a numeric code. When an error is
            // detected, a flag is set and the code is recorded. Further errors, if
            // they occur, do not affect this recorded code. When GetError is called,
            // the code is returned and the flag is cleared, so that a further error
            // will again record its code. If a call to GetError returns NO_ERROR, then
            // there has been no detectable error since the last call to GetError (or
            // since the GL was initialized).
            if (guest_error == GL_NO_ERROR) {
                guest_error = host_error;
            }
        }

    GLenum getHostErrorAndUpdate() {
        host_error = mCtx->m_glGetError_enc(mCtx);
        if (guest_error == GL_NO_ERROR) {
            guest_error = host_error;
        }
        return host_error;
    }

    void updateGuestErrorState() {
        mCtx->setError(guest_error);
    }

private:
    GL2Encoder* mCtx;
    GLenum guest_error;
    GLenum host_error;
};

template<class T>
class GL2Encoder::ScopedQueryUpdate {
public:
    ScopedQueryUpdate(GL2Encoder* ctx, uint32_t bytes, T* target) :
        mCtx(ctx),
        mBuf(bytes, 0),
        mTarget(target),
        mErrorUpdater(ctx) {
    }
    T* hostStagingBuffer() {
        return (T*)&mBuf[0];
    }
    ~ScopedQueryUpdate() {
        GLint hostError = mErrorUpdater.getHostErrorAndUpdate();
        if (hostError == GL_NO_ERROR && mTarget) {
            memcpy(mTarget, &mBuf[0], mBuf.size());
        }
        mErrorUpdater.updateGuestErrorState();
    }
private:
    GL2Encoder* mCtx;
    std::vector<char> mBuf;
    T* mTarget;
    ErrorUpdater mErrorUpdater;
};

void GL2Encoder::safe_glGetBooleanv(GLenum param, GLboolean* val) {
    ScopedQueryUpdate<GLboolean> query(this, glUtilsParamSize(param) * sizeof(GLboolean), val);
    m_glGetBooleanv_enc(this, param, query.hostStagingBuffer());
}

void GL2Encoder::safe_glGetFloatv(GLenum param, GLfloat* val) {
    ScopedQueryUpdate<GLfloat> query(this, glUtilsParamSize(param) * sizeof(GLfloat), val);
    m_glGetFloatv_enc(this, param, query.hostStagingBuffer());
}

void GL2Encoder::safe_glGetIntegerv(GLenum param, GLint* val) {
    ScopedQueryUpdate<GLint> query(this, glUtilsParamSize(param) * sizeof(GLint), val);
    m_glGetIntegerv_enc(this, param, query.hostStagingBuffer());
}

void GL2Encoder::safe_glGetInteger64v(GLenum param, GLint64* val) {
    ScopedQueryUpdate<GLint64> query(this, glUtilsParamSize(param) * sizeof(GLint64), val);
    m_glGetInteger64v_enc(this, param, query.hostStagingBuffer());
}

void GL2Encoder::safe_glGetIntegeri_v(GLenum param, GLuint index, GLint* val) {
    ScopedQueryUpdate<GLint> query(this, sizeof(GLint), val);
    m_glGetIntegeri_v_enc(this, param, index, query.hostStagingBuffer());
}

void GL2Encoder::safe_glGetInteger64i_v(GLenum param, GLuint index, GLint64* val) {
    ScopedQueryUpdate<GLint64> query(this, sizeof(GLint64), val);
    m_glGetInteger64i_v_enc(this, param, index, query.hostStagingBuffer());
}

void GL2Encoder::safe_glGetBooleani_v(GLenum param, GLuint index, GLboolean* val) {
    ScopedQueryUpdate<GLboolean> query(this, sizeof(GLboolean), val);
    m_glGetBooleani_v_enc(this, param, index, query.hostStagingBuffer());
}

void GL2Encoder::s_glFlush(void *self)
{
    GL2Encoder *ctx = (GL2Encoder *) self;
    ctx->m_glFlush_enc(self);
    ctx->m_stream->flush();
}

const GLubyte *GL2Encoder::s_glGetString(void *self, GLenum name)
{
    GL2Encoder *ctx = (GL2Encoder *)self;

    GLubyte *retval =  (GLubyte *) "";
    RET_AND_SET_ERROR_IF(
        name != GL_VENDOR &&
        name != GL_RENDERER &&
        name != GL_VERSION &&
        name != GL_EXTENSIONS,
        GL_INVALID_ENUM,
        retval);
    switch(name) {
    case GL_VENDOR:
        retval = gVendorString;
        break;
    case GL_RENDERER:
        retval = gRendererString;
        break;
    case GL_VERSION:
        retval = gVersionString;
        break;
    case GL_EXTENSIONS:
        retval = gExtensionsString;
        break;
    }
    return retval;
}

void GL2Encoder::s_glPixelStorei(void *self, GLenum param, GLint value)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    SET_ERROR_IF(!GLESv2Validation::pixelStoreParam(ctx, param), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelStoreValue(param, value), GL_INVALID_VALUE);
    ctx->m_glPixelStorei_enc(ctx, param, value);
    assert(ctx->m_state != NULL);
    ctx->m_state->setPixelStore(param, value);
}
void GL2Encoder::s_glBindBuffer(void *self, GLenum target, GLuint id)
{
    GL2Encoder *ctx = (GL2Encoder *) self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);

    bool nop = ctx->m_state->isNonIndexedBindNoOp(target, id);

    if (nop) return;

    ctx->m_state->bindBuffer(target, id);
    ctx->m_state->addBuffer(id);
    ctx->m_glBindBuffer_enc(ctx, target, id);
    ctx->m_state->setLastEncodedBufferBind(target, id);
}

void GL2Encoder::doBindBufferEncodeCached(GLenum target, GLuint id) {
    bool encode = id != m_state->getLastEncodedBufferBind(target);

    if (encode) {
        m_glBindBuffer_enc(this, target, id);
    }

    m_state->setLastEncodedBufferBind(target, id);
}

void GL2Encoder::s_glBufferData(void * self, GLenum target, GLsizeiptr size, const GLvoid * data, GLenum usage)
{
    GL2Encoder *ctx = (GL2Encoder *) self;
    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);
    GLuint bufferId = ctx->m_state->getBuffer(target);
    SET_ERROR_IF(bufferId==0, GL_INVALID_OPERATION);
    SET_ERROR_IF(size<0, GL_INVALID_VALUE);
    SET_ERROR_IF(!GLESv2Validation::bufferUsage(ctx, usage), GL_INVALID_ENUM);

    ctx->m_shared->updateBufferData(bufferId, size, data);
    ctx->m_shared->setBufferUsage(bufferId, usage);
    if (ctx->m_hasSyncBufferData) {
        ctx->glBufferDataSyncAEMU(self, target, size, data, usage);
    } else {
        ctx->m_glBufferData_enc(self, target, size, data, usage);
    }
}

void GL2Encoder::s_glBufferSubData(void * self, GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid * data)
{
    GL2Encoder *ctx = (GL2Encoder *) self;
    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);
    GLuint bufferId = ctx->m_state->getBuffer(target);
    SET_ERROR_IF(bufferId==0, GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->isBufferTargetMapped(target), GL_INVALID_OPERATION);

    GLenum res = ctx->m_shared->subUpdateBufferData(bufferId, offset, size, data);
    SET_ERROR_IF(res, res);

    ctx->m_glBufferSubData_enc(self, target, offset, size, data);
}

void GL2Encoder::s_glGenBuffers(void* self, GLsizei n, GLuint* buffers) {
    GL2Encoder *ctx = (GL2Encoder *) self;
    SET_ERROR_IF(n<0, GL_INVALID_VALUE);
    ctx->m_glGenBuffers_enc(self, n, buffers);
    for (int i = 0; i < n; i++) {
        ctx->m_state->addBuffer(buffers[i]);
    }
}

void GL2Encoder::s_glDeleteBuffers(void * self, GLsizei n, const GLuint * buffers)
{
    GL2Encoder *ctx = (GL2Encoder *) self;
    SET_ERROR_IF(n<0, GL_INVALID_VALUE);
    for (int i=0; i<n; i++) {
        // Technically if the buffer is mapped, we should unmap it, but we won't
        // use it anymore after this :)
        ctx->m_shared->deleteBufferData(buffers[i]);
        ctx->m_state->unBindBuffer(buffers[i]);
        ctx->m_state->removeBuffer(buffers[i]);
        ctx->m_glDeleteBuffers_enc(self,1,&buffers[i]);
    }
}

#define VALIDATE_VERTEX_ATTRIB_INDEX(index) \
    SET_ERROR_IF(index >= CODEC_MAX_VERTEX_ATTRIBUTES, GL_INVALID_VALUE); \

void GL2Encoder::s_glVertexAttribPointer(void *self, GLuint indx, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid * ptr)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    SET_ERROR_IF((size < 1 || size > 4), GL_INVALID_VALUE);
    SET_ERROR_IF(!GLESv2Validation::vertexAttribType(ctx, type), GL_INVALID_ENUM);
    SET_ERROR_IF(stride < 0, GL_INVALID_VALUE);
    SET_ERROR_IF((type == GL_INT_2_10_10_10_REV ||
                  type == GL_UNSIGNED_INT_2_10_10_10_REV) &&
                 size != 4,
                 GL_INVALID_OPERATION);
    ctx->m_state->setVertexAttribBinding(indx, indx);
    ctx->m_state->setVertexAttribFormat(indx, size, type, normalized, 0, false);

    GLsizei effectiveStride = stride;
    if (stride == 0) {
        effectiveStride = glSizeof(type) * size;
        switch (type) {
            case GL_INT_2_10_10_10_REV:
            case GL_UNSIGNED_INT_2_10_10_10_REV:
                effectiveStride /= 4;
                break;
            default:
                break;
        }
    }

    ctx->m_state->bindIndexedBuffer(0, indx, ctx->m_state->currentArrayVbo(), (uintptr_t)ptr, 0, stride, effectiveStride);

    if (ctx->m_state->currentArrayVbo() != 0) {
        ctx->glVertexAttribPointerOffset(ctx, indx, size, type, normalized, stride, (uintptr_t)ptr);
    } else {
        SET_ERROR_IF(ctx->m_state->currentVertexArrayObject() != 0 && ptr, GL_INVALID_OPERATION);
        // wait for client-array handler
    }
}

void GL2Encoder::s_glGetIntegerv(void *self, GLenum param, GLint *ptr)
{
    GL2Encoder *ctx = (GL2Encoder *) self;
    GLClientState* state = ctx->m_state;

    switch (param) {
    case GL_NUM_EXTENSIONS:
        *ptr = (int)ctx->m_currExtensionsArray.size();
        break;
    case GL_MAJOR_VERSION:
        *ptr = ctx->m_deviceMajorVersion;
        break;
    case GL_MINOR_VERSION:
        *ptr = ctx->m_deviceMinorVersion;
        break;
    case GL_NUM_SHADER_BINARY_FORMATS:
        *ptr = 0;
        break;
    case GL_SHADER_BINARY_FORMATS:
        // do nothing
        break;

    case GL_COMPRESSED_TEXTURE_FORMATS: {
        GLint *compressedTextureFormats = ctx->getCompressedTextureFormats();
        if (ctx->m_num_compressedTextureFormats > 0 &&
                compressedTextureFormats != NULL) {
            memcpy(ptr, compressedTextureFormats,
                    ctx->m_num_compressedTextureFormats * sizeof(GLint));
        }
        break;
    }

    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        if (ctx->m_max_combinedTextureImageUnits != 0) {
            *ptr = ctx->m_max_combinedTextureImageUnits;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_combinedTextureImageUnits = *ptr;
        }
        break;
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
        if (ctx->m_max_vertexTextureImageUnits != 0) {
            *ptr = ctx->m_max_vertexTextureImageUnits;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_vertexTextureImageUnits = *ptr;
        }
        break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS:
        if (ctx->m_max_array_texture_layers != 0) {
            *ptr = ctx->m_max_array_texture_layers;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_array_texture_layers = *ptr;
        }
        break;
    case GL_MAX_TEXTURE_IMAGE_UNITS:
        if (ctx->m_max_textureImageUnits != 0) {
            *ptr = ctx->m_max_textureImageUnits;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_textureImageUnits = *ptr;
        }
        break;
    case GL_TEXTURE_BINDING_2D:
        if (!state) return;
        *ptr = state->getBoundTexture(GL_TEXTURE_2D);
        break;
    case GL_TEXTURE_BINDING_EXTERNAL_OES:
        if (!state) return;
        *ptr = state->getBoundTexture(GL_TEXTURE_EXTERNAL_OES);
        break;
    case GL_MAX_VERTEX_ATTRIBS:
        *ptr = CODEC_MAX_VERTEX_ATTRIBUTES;
        break;
    case GL_MAX_VERTEX_ATTRIB_STRIDE:
        if (ctx->m_max_vertexAttribStride != 0) {
            *ptr = ctx->m_max_vertexAttribStride;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_vertexAttribStride = *ptr;
        }
        break;
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
        if (ctx->m_max_cubeMapTextureSize != 0) {
            *ptr = ctx->m_max_cubeMapTextureSize;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_cubeMapTextureSize = *ptr;
        }
        break;
    case GL_MAX_RENDERBUFFER_SIZE:
        if (ctx->m_max_renderBufferSize != 0) {
            *ptr = ctx->m_max_renderBufferSize;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_renderBufferSize = *ptr;
        }
        break;
    case GL_MAX_TEXTURE_SIZE:
        if (ctx->m_max_textureSize != 0) {
            *ptr = ctx->m_max_textureSize;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_textureSize = *ptr;
            if (ctx->m_max_textureSize > 0) {
                uint32_t current = 1;
                while (current < ctx->m_max_textureSize) {
                    ++ctx->m_log2MaxTextureSize;
                    current = current << 1;
                }
            }
        }
        break;
    case GL_MAX_3D_TEXTURE_SIZE:
        if (ctx->m_max_3d_textureSize != 0) {
            *ptr = ctx->m_max_3d_textureSize;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_3d_textureSize = *ptr;
        }
        break;
    case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT:
        if (ctx->m_ssbo_offset_align != 0) {
            *ptr = ctx->m_ssbo_offset_align;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_ssbo_offset_align = *ptr;
        }
        break;
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
        if (ctx->m_ubo_offset_align != 0) {
            *ptr = ctx->m_ubo_offset_align;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_ubo_offset_align = *ptr;
        }
        break;
    // Desktop OpenGL can allow a mindboggling # samples per pixel (such as 64).
    // Limit to 4 (spec minimum) to keep dEQP tests from timing out.
    case GL_MAX_SAMPLES:
    case GL_MAX_COLOR_TEXTURE_SAMPLES:
    case GL_MAX_INTEGER_SAMPLES:
    case GL_MAX_DEPTH_TEXTURE_SAMPLES:
        *ptr = 4;
        break;
    // Checks for version-incompatible enums.
    // Not allowed in vanilla ES 2.0.
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
        SET_ERROR_IF(ctx->majorVersion() < 3, GL_INVALID_ENUM);
        if (ctx->m_max_transformFeedbackSeparateAttribs != 0) {
            *ptr = ctx->m_max_transformFeedbackSeparateAttribs;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_transformFeedbackSeparateAttribs = *ptr;
        }
        break;
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
        SET_ERROR_IF(ctx->majorVersion() < 3, GL_INVALID_ENUM);
        if (ctx->m_max_uniformBufferBindings != 0) {
            *ptr = ctx->m_max_uniformBufferBindings;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_uniformBufferBindings = *ptr;
        }
        break;
    case GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT_OES:
        SET_ERROR_IF(!ctx->es32Plus() && !ctx->getExtensions().textureBufferAny(), GL_INVALID_ENUM);
        if(ctx->m_textureBufferOffsetAlign != 0) {
            *ptr = ctx->m_textureBufferOffsetAlign;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_textureBufferOffsetAlign = *ptr;
        }
        break;
    case GL_MAX_COLOR_ATTACHMENTS:
        SET_ERROR_IF(ctx->majorVersion() < 3 &&
                     !ctx->hasExtension("GL_EXT_draw_buffers"), GL_INVALID_ENUM);
        if (ctx->m_max_colorAttachments != 0) {
            *ptr = ctx->m_max_colorAttachments;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_colorAttachments = *ptr;
        }
        break;
    case GL_MAX_DRAW_BUFFERS:
        SET_ERROR_IF(ctx->majorVersion() < 3 &&
                     !ctx->hasExtension("GL_EXT_draw_buffers"), GL_INVALID_ENUM);
        if (ctx->m_max_drawBuffers != 0) {
            *ptr = ctx->m_max_drawBuffers;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_drawBuffers = *ptr;
        }
        break;
    // Not allowed in ES 3.0.
    case GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS:
        SET_ERROR_IF(ctx->majorVersion() < 3 ||
                     (ctx->majorVersion() == 3 &&
                      ctx->minorVersion() == 0), GL_INVALID_ENUM);
        if (ctx->m_max_atomicCounterBufferBindings != 0) {
            *ptr = ctx->m_max_atomicCounterBufferBindings;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_atomicCounterBufferBindings = *ptr;
        }
        break;
    case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:
        SET_ERROR_IF(ctx->majorVersion() < 3 ||
                     (ctx->majorVersion() == 3 &&
                      ctx->minorVersion() == 0), GL_INVALID_ENUM);
        if (ctx->m_max_shaderStorageBufferBindings != 0) {
            *ptr = ctx->m_max_shaderStorageBufferBindings;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_shaderStorageBufferBindings = *ptr;
        }
        break;
    case GL_MAX_VERTEX_ATTRIB_BINDINGS:
        SET_ERROR_IF(ctx->majorVersion() < 3 ||
                     (ctx->majorVersion() == 3 &&
                      ctx->minorVersion() == 0), GL_INVALID_ENUM);
        if (ctx->m_max_vertexAttribBindings != 0) {
            *ptr = ctx->m_max_vertexAttribBindings;
        } else {
            ctx->safe_glGetIntegerv(param, ptr);
            ctx->m_max_vertexAttribBindings = *ptr;
        }
        break;
    case GL_RESET_NOTIFICATION_STRATEGY_EXT:
        // BUG: 121414786
        *ptr = GL_LOSE_CONTEXT_ON_RESET_EXT;
        break;
    default:
        if (!state) return;
        if (!state->getClientStateParameter<GLint>(param, ptr)) {
            ctx->safe_glGetIntegerv(param, ptr);
        }
        break;
    }
}


void GL2Encoder::s_glGetFloatv(void *self, GLenum param, GLfloat *ptr)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    switch (param) {
    case GL_NUM_SHADER_BINARY_FORMATS:
        *ptr = 0;
        break;
    case GL_SHADER_BINARY_FORMATS:
        // do nothing
        break;

    case GL_COMPRESSED_TEXTURE_FORMATS: {
        GLint *compressedTextureFormats = ctx->getCompressedTextureFormats();
        if (ctx->m_num_compressedTextureFormats > 0 &&
                compressedTextureFormats != NULL) {
            for (int i = 0; i < ctx->m_num_compressedTextureFormats; i++) {
                ptr[i] = (GLfloat) compressedTextureFormats[i];
            }
        }
        break;
    }

    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_ATTRIBS:
    case GL_MAX_VERTEX_ATTRIB_STRIDE:
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
    case GL_MAX_RENDERBUFFER_SIZE:
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_3D_TEXTURE_SIZE:
    case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT:
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
    case GL_MAX_SAMPLES:
    case GL_MAX_COLOR_TEXTURE_SAMPLES:
    case GL_MAX_INTEGER_SAMPLES:
    case GL_MAX_DEPTH_TEXTURE_SAMPLES:
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    case GL_MAX_COLOR_ATTACHMENTS:
    case GL_MAX_DRAW_BUFFERS:
    case GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS:
    case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_ATTRIB_BINDINGS:
    case GL_TEXTURE_BINDING_2D:
    case GL_TEXTURE_BINDING_EXTERNAL_OES: {
        GLint res;
        s_glGetIntegerv(ctx, param, &res);
        *ptr = (GLfloat)res;
        break;
    }

    default:
        if (!state) return;
        if (!state->getClientStateParameter<GLfloat>(param, ptr)) {
            ctx->safe_glGetFloatv(param, ptr);
        }
        break;
    }
}


void GL2Encoder::s_glGetBooleanv(void *self, GLenum param, GLboolean *ptr)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    switch (param) {
    case GL_NUM_SHADER_BINARY_FORMATS:
        *ptr = GL_FALSE;
        break;
    case GL_SHADER_BINARY_FORMATS:
        // do nothing
        break;

    case GL_COMPRESSED_TEXTURE_FORMATS: {
        GLint *compressedTextureFormats = ctx->getCompressedTextureFormats();
        if (ctx->m_num_compressedTextureFormats > 0 &&
                compressedTextureFormats != NULL) {
            for (int i = 0; i < ctx->m_num_compressedTextureFormats; i++) {
                ptr[i] = compressedTextureFormats[i] != 0 ? GL_TRUE : GL_FALSE;
            }
        }
        break;
    }

    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_TEXTURE_IMAGE_UNITS:
    case GL_MAX_VERTEX_ATTRIBS:
    case GL_MAX_VERTEX_ATTRIB_STRIDE:
    case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
    case GL_MAX_RENDERBUFFER_SIZE:
    case GL_MAX_TEXTURE_SIZE:
    case GL_MAX_3D_TEXTURE_SIZE:
    case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT:
    case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
    case GL_MAX_SAMPLES:
    case GL_MAX_COLOR_TEXTURE_SAMPLES:
    case GL_MAX_INTEGER_SAMPLES:
    case GL_MAX_DEPTH_TEXTURE_SAMPLES:
    case GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS:
    case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    case GL_MAX_COLOR_ATTACHMENTS:
    case GL_MAX_DRAW_BUFFERS:
    case GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS:
    case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:
    case GL_MAX_VERTEX_ATTRIB_BINDINGS:
    case GL_TEXTURE_BINDING_2D:
    case GL_TEXTURE_BINDING_EXTERNAL_OES: {
        GLint res;
        s_glGetIntegerv(ctx, param, &res);
        *ptr = res == 0 ? GL_FALSE : GL_TRUE;
        break;
    }

    default:
        if (!state) return;
        {
            GLint intVal;
            if (!state->getClientStateParameter<GLint>(param, &intVal)) {
                ctx->safe_glGetBooleanv(param, ptr);
            } else {
                *ptr = (intVal != 0) ? GL_TRUE : GL_FALSE;
            }
        }
        break;
    }
}


void GL2Encoder::s_glEnableVertexAttribArray(void *self, GLuint index)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state);
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_glEnableVertexAttribArray_enc(ctx, index);
    ctx->m_state->enable(index, 1);
}

void GL2Encoder::s_glDisableVertexAttribArray(void *self, GLuint index)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state);
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_glDisableVertexAttribArray_enc(ctx, index);
    ctx->m_state->enable(index, 0);
}


void GL2Encoder::s_glGetVertexAttribiv(void *self, GLuint index, GLenum pname, GLint *params)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF(!GLESv2Validation::allowedGetVertexAttrib(pname), GL_INVALID_ENUM);

    if (!ctx->m_state->getVertexAttribParameter<GLint>(index, pname, params)) {
        ctx->m_glGetVertexAttribiv_enc(self, index, pname, params);
    }
}

void GL2Encoder::s_glGetVertexAttribfv(void *self, GLuint index, GLenum pname, GLfloat *params)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF(!GLESv2Validation::allowedGetVertexAttrib(pname), GL_INVALID_ENUM);

    if (!ctx->m_state->getVertexAttribParameter<GLfloat>(index, pname, params)) {
        ctx->m_glGetVertexAttribfv_enc(self, index, pname, params);
    }
}

void GL2Encoder::s_glGetVertexAttribPointerv(void *self, GLuint index, GLenum pname, GLvoid **pointer)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    if (ctx->m_state == NULL) return;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF(pname != GL_VERTEX_ATTRIB_ARRAY_POINTER, GL_INVALID_ENUM);
    (void)pname;

    *pointer = (GLvoid*)(ctx->m_state->getCurrAttributeBindingInfo(index).offset);
}

void GL2Encoder::calcIndexRange(const void* indices,
                                GLenum type,
                                GLsizei count,
                                int* minIndex_out,
                                int* maxIndex_out) {
    switch(type) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE:
        GLUtils::minmaxExcept(
                (unsigned char *)indices, count,
                minIndex_out, maxIndex_out,
                m_primitiveRestartEnabled, GLUtils::primitiveRestartIndex<unsigned char>());
        break;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT:
        GLUtils::minmaxExcept(
                (unsigned short *)indices, count,
                minIndex_out, maxIndex_out,
                m_primitiveRestartEnabled, GLUtils::primitiveRestartIndex<unsigned short>());
        break;
    case GL_INT:
    case GL_UNSIGNED_INT:
        GLUtils::minmaxExcept(
                (unsigned int *)indices, count,
                minIndex_out, maxIndex_out,
                m_primitiveRestartEnabled, GLUtils::primitiveRestartIndex<unsigned int>());
        break;
    default:
        GFXSTREAM_ERROR("unsupported index buffer type %d.", type);
    }
}

void* GL2Encoder::recenterIndices(const void* src,
                                  GLenum type,
                                  GLsizei count,
                                  int minIndex) {

    void* adjustedIndices = (void*)src;

    if (minIndex != 0) {
        m_fixedBuffer.resize(glSizeof(type) * count);
        adjustedIndices = m_fixedBuffer.data();
        switch(type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            GLUtils::shiftIndicesExcept(
                    (unsigned char *)src,
                    (unsigned char *)adjustedIndices,
                    count, -minIndex,
                    m_primitiveRestartEnabled,
                    (unsigned char)m_primitiveRestartIndex);
            break;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            GLUtils::shiftIndicesExcept(
                    (unsigned short *)src,
                    (unsigned short *)adjustedIndices,
                    count, -minIndex,
                    m_primitiveRestartEnabled,
                    (unsigned short)m_primitiveRestartIndex);
            break;
        case GL_INT:
        case GL_UNSIGNED_INT:
            GLUtils::shiftIndicesExcept(
                    (unsigned int *)src,
                    (unsigned int *)adjustedIndices,
                    count, -minIndex,
                    m_primitiveRestartEnabled,
                    (unsigned int)m_primitiveRestartIndex);
            break;
        default:
            GFXSTREAM_ERROR("unsupported index buffer type %d.", type);
        }
    }

    return adjustedIndices;
}

void GL2Encoder::getBufferIndexRange(BufferData* buf,
                                     const void* dataWithOffset,
                                     GLenum type,
                                     size_t count,
                                     size_t offset,
                                     int* minIndex_out,
                                     int* maxIndex_out) {

    if (buf->m_indexRangeCache.findRange(
                type, offset, count,
                m_primitiveRestartEnabled,
                minIndex_out,
                maxIndex_out)) {
        return;
    }

    calcIndexRange(dataWithOffset, type, count, minIndex_out, maxIndex_out);

    buf->m_indexRangeCache.addRange(
            type, offset, count, m_primitiveRestartEnabled,
            *minIndex_out, *maxIndex_out);

    GFXSTREAM_VERBOSE("Got range [%u %u] pr? %d", *minIndex_out, *maxIndex_out, m_primitiveRestartEnabled);
}

// For detecting legacy usage of glVertexAttribPointer
void GL2Encoder::getVBOUsage(bool* hasClientArrays, bool* hasVBOs) const {
    if (hasClientArrays) *hasClientArrays = false;
    if (hasVBOs) *hasVBOs = false;

    m_state->getVBOUsage(hasClientArrays, hasVBOs);
}

void GL2Encoder::sendVertexAttributes(GLint first, GLsizei count, bool hasClientArrays, GLsizei primcount)
{
    assert(m_state);

    m_state->updateEnableDirtyArrayForDraw();

    GLuint lastBoundVbo = m_state->currentArrayVbo();
    const GLClientState::VAOState& vaoState = m_state->currentVaoState();

    for (int k = 0; k < vaoState.numAttributesNeedingUpdateForDraw; k++) {
        int i = vaoState.attributesNeedingUpdateForDraw[k];

        const GLClientState::VertexAttribState& state = vaoState.attribState[i];

        if (state.enabled) {
            const GLClientState::BufferBinding& curr_binding = m_state->getCurrAttributeBindingInfo(i);
            GLuint bufferObject = curr_binding.buffer;
            if (hasClientArrays && lastBoundVbo != bufferObject) {
                doBindBufferEncodeCached(GL_ARRAY_BUFFER, bufferObject);
                lastBoundVbo = bufferObject;
            }

            int divisor = curr_binding.divisor;
            int stride = curr_binding.stride;
            int effectiveStride = curr_binding.effectiveStride;
            uintptr_t offset = curr_binding.offset;

            int firstIndex = effectiveStride * first;
            if (firstIndex && divisor && !primcount) {
                // If firstIndex != 0 according to effectiveStride * first,
                // it needs to be adjusted if a divisor has been specified,
                // even if we are not in glDraw***Instanced.
                firstIndex = 0;
            }

            if (bufferObject == 0) {
                unsigned int datalen = state.elementSize * count;
                if (divisor) {
                    GFXSTREAM_VERBOSE(
                        "Divisor for att %d: %d, w/ stride %d (effective stride %d) size %d type 0x%x) datalen %u",
                        i, divisor, state.stride, effectiveStride, state.elementSize, state.type, datalen);
                    int actual_count = std::max(1, (int)((primcount + divisor - 1) / divisor));
                    datalen = state.elementSize * actual_count;
                    GFXSTREAM_VERBOSE("Actual datalen %u", datalen);
                }
                if (state.elementSize == 0) {
                    // The vertex attribute array is uninitialized. Abandon it.
                    this->m_glDisableVertexAttribArray_enc(this, i);
                    continue;
                }
                m_glEnableVertexAttribArray_enc(this, i);

                if (datalen && (!offset || !((unsigned char*)offset + firstIndex))) {
                    continue;
                }

                unsigned char* data = (unsigned char*)offset + firstIndex;
                if (!m_state->isAttribIndexUsedByProgram(i)) {
                    continue;
                }

                if (state.isInt) {
                    this->glVertexAttribIPointerDataAEMU(this, i, state.size, state.type, stride, data, datalen);
                } else {
                    this->glVertexAttribPointerData(this, i, state.size, state.type, state.normalized, stride, data, datalen);
                }
            } else {
                const BufferData* buf = m_shared->getBufferData(bufferObject);
                // The following expression actually means bufLen = stride*count;
                // But the last element doesn't have to fill up the whole stride.
                // So it becomes the current form.
                unsigned int bufLen = effectiveStride * (count ? (count - 1) : 0) + state.elementSize;
                if (divisor) {
                    int actual_count = std::max(1, (int)((primcount + divisor - 1) / divisor));
                    bufLen = effectiveStride * (actual_count ? (actual_count - 1) : 0) + state.elementSize;
                }
                if (buf && firstIndex >= 0 && firstIndex + bufLen <= buf->m_size) {
                    if (hasClientArrays) {
                        m_glEnableVertexAttribArray_enc(this, i);
                        if (firstIndex) {
                            if (state.isInt) {
                                this->glVertexAttribIPointerOffsetAEMU(this, i, state.size, state.type, stride, offset + firstIndex);
                            } else {
                                this->glVertexAttribPointerOffset(this, i, state.size, state.type, state.normalized, stride, offset + firstIndex);
                            }
                        }
                    }
                } else {
                    if (m_state->isAttribIndexUsedByProgram(i)) {
                        GFXSTREAM_ERROR("a vertex attribute index out of boundary is detected. Skipping corresponding vertex attribute. buf=%p", buf);
                        if (buf) {
                            GFXSTREAM_ERROR(
                                "Out of bounds vertex attribute info: "
                                "clientArray? %d attribute %d vbo %u allocedBufferSize %u bufferDataSpecified? %d wantedStart %u wantedEnd %u",
                                hasClientArrays, i, bufferObject, (unsigned int)buf->m_size, buf != NULL, firstIndex, firstIndex + bufLen);
                        }
                        m_glDisableVertexAttribArray_enc(this, i);
                    }
                }
            }
        } else {
            if (hasClientArrays) {
                this->m_glDisableVertexAttribArray_enc(this, i);
            }
        }
    }

    if (hasClientArrays && lastBoundVbo != m_state->currentArrayVbo()) {
        doBindBufferEncodeCached(GL_ARRAY_BUFFER, m_state->currentArrayVbo());
    }
}

void GL2Encoder::flushDrawCall() {
    if (m_drawCallFlushCount % m_drawCallFlushInterval == 0) {
        m_stream->flush();
    }
    m_drawCallFlushCount++;
}

static bool isValidDrawMode(GLenum mode)
{
    bool retval = false;
    switch (mode) {
    case GL_POINTS:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
    case GL_LINES:
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_TRIANGLES:
        retval = true;
    }
    return retval;
}

void GL2Encoder::s_glDrawArrays(void *self, GLenum mode, GLint first, GLsizei count)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    ctx->getVBOUsage(&has_client_vertex_arrays,
                     &has_indirect_arrays);

    if (has_client_vertex_arrays ||
        (!has_client_vertex_arrays &&
         !has_indirect_arrays)) {
        ctx->sendVertexAttributes(first, count, true);
        ctx->m_glDrawArrays_enc(ctx, mode, 0, count);
    } else {
        ctx->m_glDrawArrays_enc(ctx, mode, first, count);
    }

    ctx->m_state->postDraw();
}


void GL2Encoder::s_glDrawElements(void *self, GLenum mode, GLsizei count, GLenum type, const void *indices)
{

    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!(type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT), GL_INVALID_ENUM);
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    GLintptr offset = 0;

    ctx->getVBOUsage(&has_client_vertex_arrays, &has_indirect_arrays);

    if (!has_client_vertex_arrays && !has_indirect_arrays) {
        // GFXSTREAM_WARNING("glDrawElements: no vertex arrays / buffers bound to the command\n");
        GLenum status = ctx->glCheckFramebufferStatus(self, GL_FRAMEBUFFER);
        SET_ERROR_IF(status != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);
    }

    BufferData* buf = NULL;
    int minIndex = 0, maxIndex = 0;

    // For validation/immediate index array purposes,
    // we need the min/max vertex index of the index array.
    // If the VBO != 0, this may not be the first time we have
    // used this particular index buffer. getBufferIndexRange
    // can more quickly get min/max vertex index by
    // caching previous results.
    if (ctx->m_state->currentIndexVbo() != 0) {
        buf = ctx->m_shared->getBufferData(ctx->m_state->currentIndexVbo());
        offset = (GLintptr)indices;
        indices = &buf->m_fixedBuffer[offset];
        ctx->getBufferIndexRange(buf,
                                 indices,
                                 type,
                                 (size_t)count,
                                 (size_t)offset,
                                 &minIndex, &maxIndex);
    } else {
        // In this case, the |indices| field holds a real
        // array, so calculate the indices now. They will
        // also be needed to know how much data to
        // transfer to host.
        ctx->calcIndexRange(indices,
                            type,
                            count,
                            &minIndex,
                            &maxIndex);
    }

    if (count == 0) return;

    bool adjustIndices = true;
    if (ctx->m_state->currentIndexVbo() != 0) {
        if (!has_client_vertex_arrays) {
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, ctx->m_state->currentIndexVbo());
            ctx->glDrawElementsOffset(ctx, mode, count, type, offset);
            ctx->flushDrawCall();
            adjustIndices = false;
        } else {
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    }
    if (adjustIndices) {
        void *adjustedIndices =
            ctx->recenterIndices(indices,
                                 type,
                                 count,
                                 minIndex);

        if (has_indirect_arrays || 1) {
            ctx->sendVertexAttributes(minIndex, maxIndex - minIndex + 1, true);
            ctx->glDrawElementsData(ctx, mode, count, type, adjustedIndices,
                                    count * glSizeof(type));
            // XXX - OPTIMIZATION (see the other else branch) should be implemented
            if(!has_indirect_arrays) {
                //GFXSTREAM_DEBUG("unoptimized drawelements !!!\n");
            }
        } else {
            // we are all direct arrays and immidate mode index array -
            // rebuild the arrays and the index array;
            GFXSTREAM_ERROR("Direct index & direct buffer data - will be implemented in later versions.");
        }
    }

    ctx->m_state->postDraw();
}

void GL2Encoder::s_glDrawArraysNullAEMU(void *self, GLenum mode, GLint first, GLsizei count)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    ctx->getVBOUsage(&has_client_vertex_arrays,
                     &has_indirect_arrays);

    if (has_client_vertex_arrays ||
        (!has_client_vertex_arrays &&
         !has_indirect_arrays)) {
        ctx->sendVertexAttributes(first, count, true);
        ctx->m_glDrawArraysNullAEMU_enc(ctx, mode, 0, count);
    } else {
        ctx->m_glDrawArraysNullAEMU_enc(ctx, mode, first, count);
    }
    ctx->flushDrawCall();
    ctx->m_state->postDraw();
}

void GL2Encoder::s_glDrawElementsNullAEMU(void *self, GLenum mode, GLsizei count, GLenum type, const void *indices)
{

    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!(type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT), GL_INVALID_ENUM);
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    GLintptr offset = (GLintptr)indices;

    ctx->getVBOUsage(&has_client_vertex_arrays, &has_indirect_arrays);

    if (!has_client_vertex_arrays && !has_indirect_arrays) {
        // GFXSTREAM_WARNING("glDrawElements: no vertex arrays / buffers bound to the command\n");
        GLenum status = ctx->glCheckFramebufferStatus(self, GL_FRAMEBUFFER);
        SET_ERROR_IF(status != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);
    }

    BufferData* buf = NULL;
    int minIndex = 0, maxIndex = 0;

    // For validation/immediate index array purposes,
    // we need the min/max vertex index of the index array.
    // If the VBO != 0, this may not be the first time we have
    // used this particular index buffer. getBufferIndexRange
    // can more quickly get min/max vertex index by
    // caching previous results.
    if (ctx->m_state->currentIndexVbo() != 0) {
        if (!has_client_vertex_arrays && has_indirect_arrays) {
            // Don't do anything
        } else {
            buf = ctx->m_shared->getBufferData(ctx->m_state->currentIndexVbo());
            offset = (GLintptr)indices;
            indices = &buf->m_fixedBuffer[offset];
            ctx->getBufferIndexRange(buf,
                                     indices,
                                     type,
                                     (size_t)count,
                                     (size_t)offset,
                                     &minIndex, &maxIndex);
        }
    } else {
        // In this case, the |indices| field holds a real
        // array, so calculate the indices now. They will
        // also be needed to know how much data to
        // transfer to host.
        ctx->calcIndexRange(indices,
                            type,
                            count,
                            &minIndex,
                            &maxIndex);
    }

    if (count == 0) return;

    bool adjustIndices = true;
    if (ctx->m_state->currentIndexVbo() != 0) {
        if (!has_client_vertex_arrays) {
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, ctx->m_state->currentIndexVbo());
            ctx->glDrawElementsOffsetNullAEMU(ctx, mode, count, type, offset);
            ctx->flushDrawCall();
            adjustIndices = false;
        } else {
            ctx->m_glBindBuffer_enc(self, GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    }
    if (adjustIndices) {
        void *adjustedIndices =
            ctx->recenterIndices(indices,
                                 type,
                                 count,
                                 minIndex);

        if (has_indirect_arrays || 1) {
            ctx->sendVertexAttributes(minIndex, maxIndex - minIndex + 1, true);
            ctx->glDrawElementsDataNullAEMU(ctx, mode, count, type, adjustedIndices,
                                    count * glSizeof(type));
            // XXX - OPTIMIZATION (see the other else branch) should be implemented
            if(!has_indirect_arrays) {
                //GFXSTREAM_DEBUG("unoptimized drawelements !!!\n");
            }
        } else {
            // we are all direct arrays and immidate mode index array -
            // rebuild the arrays and the index array;
            GFXSTREAM_ERROR("Direct index & direct buffer data - will be implemented in later versions.");
        }
    }
    ctx->m_state->postDraw();
}

GLint * GL2Encoder::getCompressedTextureFormats()
{
    if (m_compressedTextureFormats == NULL) {
        this->glGetIntegerv(this, GL_NUM_COMPRESSED_TEXTURE_FORMATS,
                            &m_num_compressedTextureFormats);
        if (m_num_compressedTextureFormats > 0) {
            // get number of texture formats;
            m_compressedTextureFormats = new GLint[m_num_compressedTextureFormats];
            this->glGetCompressedTextureFormats(this, m_num_compressedTextureFormats, m_compressedTextureFormats);
        }
    }
    return m_compressedTextureFormats;
}

// Replace uses of samplerExternalOES with sampler2D, recording the names of
// modified shaders in data. Also remove
//   #extension GL_OES_EGL_image_external : require
//   #extension GL_OES_EGL_image_external_essl3 : require
// statements.
//
// This implementation assumes the input has already been pre-processed. If not,
// a few cases will be mishandled:
//
// 1. "mySampler" will be incorrectly recorded as being a samplerExternalOES in
//    the following code:
//      #if 1
//      uniform sampler2D mySampler;
//      #else
//      uniform samplerExternalOES mySampler;
//      #endif
//
// 2. Comments that look like sampler declarations will be incorrectly modified
//    and recorded:
//      // samplerExternalOES hahaFooledYou
//
// 3. However, GLSL ES does not have a concatentation operator, so things like
//    this (valid in C) are invalid and not a problem:
//      #define SAMPLER(TYPE, NAME) uniform sampler#TYPE NAME
//      SAMPLER(ExternalOES, mySampler);
//

static const char STR_SAMPLER_EXTERNAL_OES[] = "samplerExternalOES";
static const char STR_SAMPLER2D_SPACE[]      = "sampler2D         ";
static const char STR_DEFINE[] = "#define";

static std::vector<std::string> getSamplerExternalAliases(char* str) {
    std::vector<std::string> res;

    res.push_back(STR_SAMPLER_EXTERNAL_OES);

    // -- capture #define x samplerExternalOES
    char* c = str;
    while ((c = strstr(c, STR_DEFINE))) {
        // Don't push it if samplerExternalOES is not even there.
        char* samplerExternalOES_next = strstr(c, STR_SAMPLER_EXTERNAL_OES);
        if (!samplerExternalOES_next) break;

        bool prevIdent = false;

        std::vector<std::string> idents;
        std::string curr;

        while (*c != '\0') {

            if (isspace(*c)) {
                if (prevIdent) {
                    idents.push_back(curr);
                    curr = "";
                }
            }

            if (*c == '\n' || idents.size() == 3) break;

            if (isalpha(*c) || *c == '_') {
                curr.push_back(*c);
                prevIdent = true;
            }

            ++c;
        }

        if (idents.size() != 3) continue;

        const std::string& defineLhs = idents[1];
        const std::string& defineRhs = idents[2];

        if (defineRhs == STR_SAMPLER_EXTERNAL_OES) {
            res.push_back(defineLhs);
        }

        if (*c == '\0') break;
    }

    return res;
}

static bool replaceExternalSamplerUniformDefinition(char* str, const std::string& samplerExternalType, ShaderData* data) {
    // -- replace "samplerExternalOES" with "sampler2D" and record name
    char* c = str;
    while ((c = strstr(c, samplerExternalType.c_str()))) {
        // Make sure "samplerExternalOES" isn't a substring of a larger token
        if (c == str || !isspace(*(c-1))) {
            c++;
            continue;
        }
        char* sampler_start = c;
        c += samplerExternalType.size();
        if (!isspace(*c) && *c != '\0' && *c != ';') {
            continue;
        } else {
            // capture sampler name
            while (isspace(*c) && *c != '\0') {
                c++;
            }
        }

        if ((!isalpha(*c) && *c != '_') || *c == ';') {
            // not an identifier, but might have some effect anyway.
            if (samplerExternalType == STR_SAMPLER_EXTERNAL_OES) {
                memcpy(sampler_start, STR_SAMPLER2D_SPACE, sizeof(STR_SAMPLER2D_SPACE)-1);
            }
        } else {
            char* name_start = c;
            do {
                c++;
            } while (isalnum(*c) || *c == '_');

            size_t len = (size_t)(c - name_start);
            if (len) {
                data->samplerExternalNames.push_back(
                        std::string(name_start, len));
            }

            // We only need to perform a string replacement for the original
            // occurrence of samplerExternalOES if a #define was used.
            //
            // The important part was to record the name in
            // |data->samplerExternalNames|.
            if (samplerExternalType == STR_SAMPLER_EXTERNAL_OES) {
                memcpy(sampler_start, STR_SAMPLER2D_SPACE, sizeof(STR_SAMPLER2D_SPACE)-1);
            }
        }
    }

    return true;
}

static bool replaceSamplerExternalWith2D(char* const str, ShaderData* const data)
{
    static const char STR_HASH_EXTENSION[] = "#extension";
    static const char STR_GL_OES_EGL_IMAGE_EXTERNAL[] = "GL_OES_EGL_image_external";
    static const char STR_GL_OES_EGL_IMAGE_EXTERNAL_ESSL3[] = "GL_OES_EGL_image_external_essl3";

    // -- overwrite all "#extension GL_OES_EGL_image_external : xxx" statements
    char* c = str;
    while ((c = strstr(c, STR_HASH_EXTENSION))) {
        char* start = c;
        c += sizeof(STR_HASH_EXTENSION)-1;
        while (isspace(*c) && *c != '\0') {
            c++;
        }

        bool hasBaseImageExternal =
            !strncmp(c, STR_GL_OES_EGL_IMAGE_EXTERNAL,
                     sizeof(STR_GL_OES_EGL_IMAGE_EXTERNAL) - 1);
        bool hasEssl3ImageExternal =
            !strncmp(c, STR_GL_OES_EGL_IMAGE_EXTERNAL_ESSL3,
                     sizeof(STR_GL_OES_EGL_IMAGE_EXTERNAL_ESSL3) - 1);

        if (hasBaseImageExternal || hasEssl3ImageExternal)
        {
            // #extension statements are terminated by end of line
            c = start;
            while (*c != '\0' && *c != '\r' && *c != '\n') {
                *c++ = ' ';
            }
        }
    }

    std::vector<std::string> samplerExternalAliases =
        getSamplerExternalAliases(str);

    for (size_t i = 0; i < samplerExternalAliases.size(); i++) {
        if (!replaceExternalSamplerUniformDefinition(
                str, samplerExternalAliases[i], data))
            return false;
    }

    return true;
}

void GL2Encoder::s_glShaderBinary(void *self, GLsizei, const GLuint *, GLenum, const void*, GLsizei)
{
    // Although it is not supported, need to set proper error code.
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(1, GL_INVALID_ENUM);
}

void GL2Encoder::s_glShaderSource(void *self, GLuint shader, GLsizei count, const GLchar * const *string, const GLint *length)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    ShaderData* shaderData = ctx->m_shared->getShaderData(shader);
    SET_ERROR_IF(!ctx->m_shared->isShaderOrProgramObject(shader), GL_INVALID_VALUE);
    SET_ERROR_IF(!shaderData, GL_INVALID_OPERATION);
    SET_ERROR_IF((count<0), GL_INVALID_VALUE);

    // Track original sources---they may be translated in the backend
    std::vector<std::string> orig_sources;
    if (length) {
        for (int i = 0; i < count; i++) {
            // Each element in the length array may contain the length of the corresponding
            // string (the null character is not counted as part of the string length) or a
            // value less than 0 to indicate that the string is null terminated.
            if (length[i] >= 0) {
                orig_sources.push_back(std::string((const char*)(string[i]),
                                                   (const char*)(string[i]) + length[i]));
            } else {
                orig_sources.push_back(std::string((const char*)(string[i])));
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            orig_sources.push_back(std::string((const char*)(string[i])));
        }
    }
    shaderData->sources = orig_sources;

    int len = glUtilsCalcShaderSourceLen((char**)string, (GLint*)length, count);
    char *str = new char[len + 1];
    glUtilsPackStrings(str, (char**)string, (GLint*)length, count);

    // TODO: pre-process str before calling replaceSamplerExternalWith2D().
    // Perhaps we can borrow Mesa's pre-processor?

    if (!replaceSamplerExternalWith2D(str, shaderData)) {
        delete[] str;
        ctx->setError(GL_OUT_OF_MEMORY);
        return;
    }
    ctx->glShaderString(ctx, shader, str, len + 1);
    delete[] str;
}

void GL2Encoder::s_glFinish(void *self)
{
    GL2Encoder *ctx = (GL2Encoder *)self;
    ctx->glFinishRoundTrip(self);
}

void GL2Encoder::updateProgramInfoAfterLink(GLuint program) {
    GL2Encoder* ctx = this;

    GLint linkStatus = 0;
    ctx->m_glGetProgramiv_enc(ctx, program, GL_LINK_STATUS, &linkStatus);
    ctx->m_shared->setProgramLinkStatus(program, linkStatus);
    if (!linkStatus) {
        return;
    }

    // get number of active uniforms and attributes in the program
    GLint numUniforms=0;
    GLint numAttributes=0;
    ctx->m_glGetProgramiv_enc(ctx, program, GL_ACTIVE_UNIFORMS, &numUniforms);
    ctx->m_glGetProgramiv_enc(ctx, program, GL_ACTIVE_ATTRIBUTES, &numAttributes);
    ctx->m_shared->initProgramData(program,numUniforms,numAttributes);

    //get the length of the longest uniform name
    GLint maxLength=0;
    GLint maxAttribLength=0;
    ctx->m_glGetProgramiv_enc(ctx, program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    ctx->m_glGetProgramiv_enc(ctx, program, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxAttribLength);

    GLint size;
    GLenum type;
    size_t bufLen = maxLength > maxAttribLength ? maxLength : maxAttribLength;
    GLchar *name = new GLchar[bufLen + 1];
    GLint location;
    //for each active uniform, get its size and starting location.
    for (GLint i=0 ; i<numUniforms ; ++i)
    {
        ctx->m_glGetActiveUniform_enc(ctx, program, i, maxLength, NULL, &size, &type, name);
        location = ctx->m_glGetUniformLocation_enc(ctx, program, name);
        ctx->m_shared->setProgramIndexInfo(program, i, location, size, type, name);
    }

    for (GLint i = 0; i < numAttributes; ++i) {
        ctx->m_glGetActiveAttrib_enc(ctx, program, i, maxAttribLength, NULL, &size, &type, name);
        location = ctx->m_glGetAttribLocation_enc(ctx, program, name);
        ctx->m_shared->setProgramAttribInfo(program, i, location, size, type, name);
    }

    if (ctx->majorVersion() > 2) {
        GLint numBlocks;
        ctx->m_glGetProgramiv_enc(ctx, program, GL_ACTIVE_UNIFORM_BLOCKS, &numBlocks);
        ctx->m_shared->setActiveUniformBlockCountForProgram(program, numBlocks);

        GLint tfVaryingsCount;
        ctx->m_glGetProgramiv_enc(ctx, program, GL_TRANSFORM_FEEDBACK_VARYINGS, &tfVaryingsCount);
        ctx->m_shared->setTransformFeedbackVaryingsCountForProgram(program, tfVaryingsCount);
    }

    delete[] name;
}

void GL2Encoder::s_glLinkProgram(void* self, GLuint program) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    bool isProgram = ctx->m_shared->isProgram(program);
    SET_ERROR_IF(!isProgram && !ctx->m_shared->isShader(program), GL_INVALID_VALUE);
    SET_ERROR_IF(!isProgram, GL_INVALID_OPERATION);

    if (program == ctx->m_state->currentProgram() ||
        (!ctx->m_state->currentProgram() && (program == ctx->m_state->currentShaderProgram()))) {
        SET_ERROR_IF(ctx->m_state->getTransformFeedbackActive(), GL_INVALID_OPERATION);
    }

    ctx->m_glLinkProgram_enc(self, program);

    ctx->updateProgramInfoAfterLink(program);
}

#define VALIDATE_PROGRAM_NAME(program) \
    bool isShaderOrProgramObject = \
        ctx->m_shared->isShaderOrProgramObject(program); \
    bool isProgram = \
        ctx->m_shared->isProgram(program); \
    SET_ERROR_IF(!isShaderOrProgramObject, GL_INVALID_VALUE); \
    SET_ERROR_IF(!isProgram, GL_INVALID_OPERATION); \

#define VALIDATE_PROGRAM_NAME_RET(program, ret) \
    bool isShaderOrProgramObject = \
        ctx->m_shared->isShaderOrProgramObject(program); \
    bool isProgram = \
        ctx->m_shared->isProgram(program); \
    RET_AND_SET_ERROR_IF(!isShaderOrProgramObject, GL_INVALID_VALUE, ret); \
    RET_AND_SET_ERROR_IF(!isProgram, GL_INVALID_OPERATION, ret); \

#define VALIDATE_SHADER_NAME(shader) \
    bool isShaderOrProgramObject = \
        ctx->m_shared->isShaderOrProgramObject(shader); \
    bool isShader = \
        ctx->m_shared->isShader(shader); \
    SET_ERROR_IF(!isShaderOrProgramObject, GL_INVALID_VALUE); \
    SET_ERROR_IF(!isShader, GL_INVALID_OPERATION); \

void GL2Encoder::s_glDeleteProgram(void *self, GLuint program)
{
    GL2Encoder *ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);

    ctx->m_glDeleteProgram_enc(self, program);

    ctx->m_shared->deleteProgramData(program);
}

void GL2Encoder::s_glGetUniformiv(void *self, GLuint program, GLint location, GLint* params)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_shared->isShaderOrProgramObject(program), GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->m_shared->isProgram(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->isProgramInitialized(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_shared->getProgramUniformType(program,location)==0, GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->isProgramUniformLocationValid(program,location), GL_INVALID_OPERATION);
    ctx->m_glGetUniformiv_enc(self, program, location, params);
}
void GL2Encoder::s_glGetUniformfv(void *self, GLuint program, GLint location, GLfloat* params)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_shared->isShaderOrProgramObject(program), GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->m_shared->isProgram(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->isProgramInitialized(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_shared->getProgramUniformType(program,location)==0, GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->isProgramUniformLocationValid(program,location), GL_INVALID_OPERATION);
    ctx->m_glGetUniformfv_enc(self, program, location, params);
}

GLuint GL2Encoder::s_glCreateProgram(void * self)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLuint program = ctx->m_glCreateProgram_enc(self);
    if (program!=0)
        ctx->m_shared->addProgramData(program);
    return program;
}

GLuint GL2Encoder::s_glCreateShader(void *self, GLenum shaderType)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    RET_AND_SET_ERROR_IF(!GLESv2Validation::shaderType(ctx, shaderType), GL_INVALID_ENUM, 0);
    GLuint shader = ctx->m_glCreateShader_enc(self, shaderType);
    if (shader != 0) {
        if (!ctx->m_shared->addShaderData(shader, shaderType)) {
            ctx->m_glDeleteShader_enc(self, shader);
            return 0;
        }
    }
    return shader;
}

void GL2Encoder::s_glGetAttachedShaders(void *self, GLuint program, GLsizei maxCount,
        GLsizei* count, GLuint* shaders)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(maxCount < 0, GL_INVALID_VALUE);
    ctx->m_glGetAttachedShaders_enc(self, program, maxCount, count, shaders);
}

void GL2Encoder::s_glGetShaderSource(void *self, GLuint shader, GLsizei bufsize,
            GLsizei* length, GLchar* source)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    VALIDATE_SHADER_NAME(shader);
    SET_ERROR_IF(bufsize < 0, GL_INVALID_VALUE);
    ctx->m_glGetShaderSource_enc(self, shader, bufsize, length, source);
    ShaderData* shaderData = ctx->m_shared->getShaderData(shader);
    if (shaderData) {
        std::string returned;
        int curr_len = 0;
        for (int i = 0; i < shaderData->sources.size(); i++) {
            if (curr_len + shaderData->sources[i].size() < bufsize - 1) {
                returned += shaderData->sources[i];
            } else {
                returned += shaderData->sources[i].substr(0, bufsize - 1 - curr_len);
                break;
            }
        }
        std::string ret = returned.substr(0, bufsize - 1);

        size_t toCopy = bufsize < (ret.size() + 1) ? bufsize : ret.size() + 1;
        memcpy(source, ret.c_str(), toCopy);
    }
}

void GL2Encoder::s_glGetShaderInfoLog(void *self, GLuint shader, GLsizei bufsize,
        GLsizei* length, GLchar* infolog)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    VALIDATE_SHADER_NAME(shader);
    SET_ERROR_IF(bufsize < 0, GL_INVALID_VALUE);
    ctx->m_glGetShaderInfoLog_enc(self, shader, bufsize, length, infolog);
}

void GL2Encoder::s_glGetProgramInfoLog(void *self, GLuint program, GLsizei bufsize,
        GLsizei* length, GLchar* infolog)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(bufsize < 0, GL_INVALID_VALUE);
    ctx->m_glGetProgramInfoLog_enc(self, program, bufsize, length, infolog);
}

void GL2Encoder::s_glDeleteShader(void *self, GLenum shader)
{
    GL2Encoder *ctx = (GL2Encoder*)self;

    bool isShaderOrProgramObject =
        ctx->m_shared->isShaderOrProgramObject(shader);
    bool isShader =
        ctx->m_shared->isShader(shader);

    SET_ERROR_IF(isShaderOrProgramObject && !isShader, GL_INVALID_OPERATION);
    SET_ERROR_IF(!isShaderOrProgramObject && !isShader, GL_INVALID_VALUE);

    ctx->m_glDeleteShader_enc(self,shader);
    ctx->m_shared->unrefShaderData(shader);
}

void GL2Encoder::s_glAttachShader(void *self, GLuint program, GLuint shader)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    bool programIsShaderOrProgram = ctx->m_shared->isShaderOrProgramObject(program);
    bool programIsProgram = ctx->m_shared->isProgram(program);
    bool shaderIsShaderOrProgram = ctx->m_shared->isShaderOrProgramObject(shader);
    bool shaderIsShader = ctx->m_shared->isShader(shader);

    SET_ERROR_IF(!programIsShaderOrProgram, GL_INVALID_VALUE);
    SET_ERROR_IF(!shaderIsShaderOrProgram, GL_INVALID_VALUE);
    SET_ERROR_IF(!programIsProgram, GL_INVALID_OPERATION);
    SET_ERROR_IF(!shaderIsShader, GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->attachShader(program, shader), GL_INVALID_OPERATION);

    ctx->m_glAttachShader_enc(self, program, shader);
}

void GL2Encoder::s_glDetachShader(void *self, GLuint program, GLuint shader)
{
    GL2Encoder *ctx = (GL2Encoder*)self;

    bool programIsShaderOrProgram = ctx->m_shared->isShaderOrProgramObject(program);
    bool programIsProgram = ctx->m_shared->isProgram(program);
    bool shaderIsShaderOrProgram = ctx->m_shared->isShaderOrProgramObject(shader);
    bool shaderIsShader = ctx->m_shared->isShader(shader);

    SET_ERROR_IF(!programIsShaderOrProgram, GL_INVALID_VALUE);
    SET_ERROR_IF(!shaderIsShaderOrProgram, GL_INVALID_VALUE);
    SET_ERROR_IF(!programIsProgram, GL_INVALID_OPERATION);
    SET_ERROR_IF(!shaderIsShader, GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->detachShader(program, shader), GL_INVALID_OPERATION);

    ctx->m_glDetachShader_enc(self, program, shader);
}

int sArrIndexOfUniformExpr(const char* name, int* err) {
    *err = 0;
    int arrIndex = 0;
    int namelen = strlen(name);
    if (name[namelen-1] == ']') {
        const char *brace = strrchr(name,'[');
        if (!brace || sscanf(brace+1,"%d",&arrIndex) != 1) {
            *err = 1; return 0;
        }
    }
    return arrIndex;
}

int GL2Encoder::s_glGetUniformLocation(void *self, GLuint program, const GLchar *name)
{
    if (!name) return -1;
    GL2Encoder *ctx = (GL2Encoder*)self;

    bool isShaderOrProgramObject =
        ctx->m_shared->isShaderOrProgramObject(program);
    bool isProgram =
        ctx->m_shared->isProgram(program);

    RET_AND_SET_ERROR_IF(!isShaderOrProgramObject, GL_INVALID_VALUE, -1);
    RET_AND_SET_ERROR_IF(!isProgram, GL_INVALID_OPERATION, -1);
    RET_AND_SET_ERROR_IF(!ctx->m_shared->getProgramLinkStatus(program), GL_INVALID_OPERATION, -1);

    return ctx->m_glGetUniformLocation_enc(self, program, name);
}

bool GL2Encoder::updateHostTexture2DBinding(GLenum texUnit, GLenum newTarget)
{
    if (newTarget != GL_TEXTURE_2D && newTarget != GL_TEXTURE_EXTERNAL_OES)
        return false;

    m_state->setActiveTextureUnit(texUnit);

    GLenum oldTarget = m_state->getPriorityEnabledTarget(GL_TEXTURE_2D);
    if (newTarget != oldTarget) {
        if (newTarget == GL_TEXTURE_EXTERNAL_OES) {
            m_state->disableTextureTarget(GL_TEXTURE_2D);
            m_state->enableTextureTarget(GL_TEXTURE_EXTERNAL_OES);
        } else {
            m_state->disableTextureTarget(GL_TEXTURE_EXTERNAL_OES);
            m_state->enableTextureTarget(GL_TEXTURE_2D);
        }
        m_glActiveTexture_enc(this, texUnit);
        m_glBindTexture_enc(this, GL_TEXTURE_2D,
                m_state->getBoundTexture(newTarget));
        return true;
    }

    return false;
}

void GL2Encoder::updateHostTexture2DBindingsFromProgramData(GLuint program) {
    GL2Encoder *ctx = this;
    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;

    GLenum origActiveTexture = state->getActiveTextureUnit();
    GLenum hostActiveTexture = origActiveTexture;
    GLint samplerIdx = -1;
    GLint samplerVal;
    GLenum samplerTarget;
    while ((samplerIdx = shared->getNextSamplerUniform(program, samplerIdx, &samplerVal, &samplerTarget)) != -1) {
        if (samplerVal < 0 || samplerVal >= GLClientState::MAX_TEXTURE_UNITS)
            continue;
        if (ctx->updateHostTexture2DBinding(GL_TEXTURE0 + samplerVal,
                    samplerTarget))
        {
            hostActiveTexture = GL_TEXTURE0 + samplerVal;
        }
    }
    state->setActiveTextureUnit(origActiveTexture);
    if (hostActiveTexture != origActiveTexture) {
        ctx->m_glActiveTexture_enc(ctx, origActiveTexture);
    }
}

void GL2Encoder::s_glUseProgram(void *self, GLuint program)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLSharedGroupPtr shared = ctx->m_shared;

    SET_ERROR_IF(program && !shared->isShaderOrProgramObject(program), GL_INVALID_VALUE);
    SET_ERROR_IF(program && !shared->isProgram(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);

    ctx->m_glUseProgram_enc(self, program);

    GLuint currProgram = ctx->m_state->currentProgram();
    ctx->m_shared->onUseProgram(currProgram, program);

    ctx->m_state->setCurrentProgram(program);
    ctx->m_state->setCurrentShaderProgram(program);
    ctx->updateHostTexture2DBindingsFromProgramData(program);

    if (program) {
        ctx->m_state->currentUniformValidationInfo = ctx->m_shared->getUniformValidationInfo(program);
        ctx->m_state->currentAttribValidationInfo = ctx->m_shared->getAttribValidationInfo(program);
    }
}

void GL2Encoder::s_glUniform1f(void *self , GLint location, GLfloat x)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 1 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform1f_enc(self, location, x);
}

void GL2Encoder::s_glUniform1fv(void *self , GLint location, GLsizei count, const GLfloat* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 1 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform1fv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform1i(void *self , GLint location, GLint x)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;

    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 1 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());

    ctx->m_glUniform1i_enc(self, location, x);

    GLenum target;
    if (shared->setSamplerUniform(state->currentShaderProgram(), location, x, &target)) {
        GLenum origActiveTexture = state->getActiveTextureUnit();
        if (ctx->updateHostTexture2DBinding(GL_TEXTURE0 + x, target)) {
            ctx->m_glActiveTexture_enc(self, origActiveTexture);
        }
        state->setActiveTextureUnit(origActiveTexture);
    }
}

void GL2Encoder::s_glUniform1iv(void *self , GLint location, GLsizei count, const GLint* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 1 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform1iv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform2f(void *self , GLint location, GLfloat x, GLfloat y)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 2 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform2f_enc(self, location, x, y);
}

void GL2Encoder::s_glUniform2fv(void *self , GLint location, GLsizei count, const GLfloat* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 2 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform2fv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform2i(void *self , GLint location, GLint x, GLint y)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 2 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform2i_enc(self, location, x, y);
}

void GL2Encoder::s_glUniform2iv(void *self , GLint location, GLsizei count, const GLint* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 2 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform2iv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform3f(void *self , GLint location, GLfloat x, GLfloat y, GLfloat z)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 3 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform3f_enc(self, location, x, y, z);
}

void GL2Encoder::s_glUniform3fv(void *self , GLint location, GLsizei count, const GLfloat* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 3 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform3fv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform3i(void *self , GLint location, GLint x, GLint y, GLint z)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 3 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform3i_enc(self, location, x, y, z);
}

void GL2Encoder::s_glUniform3iv(void *self , GLint location, GLsizei count, const GLint* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 3 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform3iv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform4f(void *self , GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 4 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform4f_enc(self, location, x, y, z, w);
}

void GL2Encoder::s_glUniform4fv(void *self , GLint location, GLsizei count, const GLfloat* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 4 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform4fv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniform4i(void *self , GLint location, GLint x, GLint y, GLint z, GLint w)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 4 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform4i_enc(self, location, x, y, z, w);
}

void GL2Encoder::s_glUniform4iv(void *self , GLint location, GLsizei count, const GLint* v)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, false /* is unsigned? */, 4 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform4iv_enc(self, location, count, v);
}

void GL2Encoder::s_glUniformMatrix2fv(void *self , GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 2 /* columns */, 2 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix2fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix3fv(void *self , GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 3 /* columns */, 3 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix3fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix4fv(void *self , GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 4 /* columns */, 4 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix4fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glActiveTexture(void* self, GLenum texture)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    GLenum err;

    GLint maxCombinedUnits;
    ctx->glGetIntegerv(ctx, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedUnits);

    SET_ERROR_IF(texture - GL_TEXTURE0 > maxCombinedUnits - 1, GL_INVALID_ENUM);
    SET_ERROR_IF((err = state->setActiveTextureUnit(texture)) != GL_NO_ERROR, err);

    ctx->m_glActiveTexture_enc(ctx, texture);
}

void GL2Encoder::s_glBindTexture(void* self, GLenum target, GLuint texture)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    GLenum err;
    GLboolean firstUse;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF((err = state->bindTexture(target, texture, &firstUse)) != GL_NO_ERROR, err);

    if (target != GL_TEXTURE_2D && target != GL_TEXTURE_EXTERNAL_OES) {
        ctx->m_glBindTexture_enc(ctx, target, texture);
        return;
    }

    GLenum priorityTarget = state->getPriorityEnabledTarget(GL_TEXTURE_2D);

    if (target == GL_TEXTURE_EXTERNAL_OES && firstUse) {
        ctx->m_glBindTexture_enc(ctx, GL_TEXTURE_2D, texture);
        ctx->m_glTexParameteri_enc(ctx, GL_TEXTURE_2D,
                GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        ctx->m_glTexParameteri_enc(ctx, GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ctx->m_glTexParameteri_enc(ctx, GL_TEXTURE_2D,
                GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (target != priorityTarget) {
            ctx->m_glBindTexture_enc(ctx, GL_TEXTURE_2D,
                    state->getBoundTexture(GL_TEXTURE_2D));
        }
    }

    if (target == priorityTarget) {
        ctx->m_glBindTexture_enc(ctx, GL_TEXTURE_2D, texture);
    }
}

void GL2Encoder::s_glDeleteTextures(void* self, GLsizei n, const GLuint* textures)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    state->deleteTextures(n, textures);
    ctx->m_glDeleteTextures_enc(ctx, n, textures);
}

void GL2Encoder::s_glGetTexParameterfv(void* self,
        GLenum target, GLenum pname, GLfloat* params)
{
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParams(ctx, pname), GL_INVALID_ENUM);
    if (!params) return;

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
        ctx->m_glGetTexParameterfv_enc(ctx, GL_TEXTURE_2D, pname, params);
        ctx->restore2DTextureTarget(target);
    } else {
        ctx->m_glGetTexParameterfv_enc(ctx, target, pname, params);
    }
}

void GL2Encoder::s_glGetTexParameteriv(void* self,
        GLenum target, GLenum pname, GLint* params)
{
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParams(ctx, pname), GL_INVALID_ENUM);

    if (!params) return;

    switch (pname) {
    case GL_REQUIRED_TEXTURE_IMAGE_UNITS_OES:
        *params = 1;
        break;

    default:
        if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
            ctx->override2DTextureTarget(target);
            ctx->m_glGetTexParameteriv_enc(ctx, GL_TEXTURE_2D, pname, params);
            ctx->restore2DTextureTarget(target);
        } else {
            ctx->m_glGetTexParameteriv_enc(ctx, target, pname, params);
        }
        break;
    }
}

static bool isValidTextureExternalParam(GLenum pname, GLenum param)
{
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
    case GL_TEXTURE_MAG_FILTER:
        return param == GL_NEAREST || param == GL_LINEAR;

    case GL_TEXTURE_WRAP_S:
    case GL_TEXTURE_WRAP_T:
        return param == GL_CLAMP_TO_EDGE;

    default:
        return true;
    }
}

void GL2Encoder::s_glTexParameterf(void* self,
        GLenum target, GLenum pname, GLfloat param)
{
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF((target == GL_TEXTURE_EXTERNAL_OES &&
            !isValidTextureExternalParam(pname, (GLenum)param)),
            GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, (GLint)param, param, (GLenum)param), GL_INVALID_ENUM);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
        ctx->m_glTexParameterf_enc(ctx, GL_TEXTURE_2D, pname, param);
        ctx->restore2DTextureTarget(target);
    } else {
        ctx->m_glTexParameterf_enc(ctx, target, pname, param);
    }
}

void GL2Encoder::s_glTexParameterfv(void* self,
        GLenum target, GLenum pname, const GLfloat* params)
{
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF((target == GL_TEXTURE_EXTERNAL_OES &&
            !isValidTextureExternalParam(pname, (GLenum)params[0])),
            GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!params, GL_INVALID_VALUE);
    GLfloat param = *params;
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, (GLint)param, param, (GLenum)param), GL_INVALID_ENUM);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
        ctx->m_glTexParameterfv_enc(ctx, GL_TEXTURE_2D, pname, params);
        ctx->restore2DTextureTarget(target);
    } else {
        ctx->m_glTexParameterfv_enc(ctx, target, pname, params);
    }
}

void GL2Encoder::s_glTexParameteri(void* self,
        GLenum target, GLenum pname, GLint param)
{
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF((target == GL_TEXTURE_EXTERNAL_OES &&
            !isValidTextureExternalParam(pname, (GLenum)param)),
            GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, param, (GLfloat)param, (GLenum)param), GL_INVALID_ENUM);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
        ctx->m_glTexParameteri_enc(ctx, GL_TEXTURE_2D, pname, param);
        ctx->restore2DTextureTarget(target);
    } else {
        ctx->m_glTexParameteri_enc(ctx, target, pname, param);
    }
}

bool GL2Encoder::validateTexBuffer(void* self, GLenum target, GLenum internalFormat, GLuint buffer) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    RET_AND_SET_ERROR_IF((target != GL_TEXTURE_BUFFER_OES), GL_INVALID_ENUM, false);
    RET_AND_SET_ERROR_IF(!GLESv2Validation::textureBufferFormat(ctx, internalFormat), GL_INVALID_ENUM, false);
    RET_AND_SET_ERROR_IF(buffer != 0 && !ctx->getBufferDataById(buffer), GL_INVALID_OPERATION, false);
    return true;
}

bool GL2Encoder::validateTexBufferRange(void* self, GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    RET_AND_SET_ERROR_IF((target != GL_TEXTURE_BUFFER_OES), GL_INVALID_ENUM, false);
    RET_AND_SET_ERROR_IF(!GLESv2Validation::textureBufferFormat(ctx, internalFormat), GL_INVALID_ENUM, false);
    if (buffer != 0) {
        BufferData* buf = ctx->getBufferDataById(buffer);
        RET_AND_SET_ERROR_IF(((!buf) || (buf->m_size < offset+size) || (offset < 0) || (size<0)), GL_INVALID_VALUE, false);
    }
    GLint tex_buffer_offset_align = 1;
    ctx->s_glGetIntegerv(ctx, GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT_OES, &tex_buffer_offset_align);
    RET_AND_SET_ERROR_IF((offset % tex_buffer_offset_align) != 0, GL_INVALID_VALUE, false);
    return true;
}

void GL2Encoder::s_glTexBufferOES(void* self,
          GLenum target, GLenum internalFormat, GLuint buffer)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->getExtensions().textureBufferOES, GL_INVALID_OPERATION);
    if(!validateTexBuffer(ctx, target, internalFormat, buffer)) return;
    GLClientState* state = ctx->m_state;
    state->setBoundTextureInternalFormat(target, internalFormat);
    ctx->m_glTexBufferOES_enc(ctx, target, internalFormat, buffer);
}


void GL2Encoder::s_glTexBufferRangeOES(void* self,
          GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->getExtensions().textureBufferOES, GL_INVALID_OPERATION);
    if(!validateTexBufferRange(ctx, target, internalFormat, buffer, offset, size)) return;
    GLClientState* state = ctx->m_state;
    state->setBoundTextureInternalFormat(target, internalFormat);
    ctx->m_glTexBufferRangeOES_enc(ctx, target, internalFormat, buffer, offset, size);
}

void GL2Encoder::s_glTexBufferEXT(void* self,
          GLenum target, GLenum internalFormat, GLuint buffer)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->getExtensions().textureBufferEXT, GL_INVALID_OPERATION);
    if(!validateTexBuffer(ctx, target, internalFormat, buffer)) return;
    GLClientState* state = ctx->m_state;
    state->setBoundTextureInternalFormat(target, internalFormat);
    ctx->m_glTexBufferEXT_enc(ctx, target, internalFormat, buffer);
}


void GL2Encoder::s_glTexBufferRangeEXT(void* self,
          GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().textureBufferEXT, GL_INVALID_OPERATION);
     if(!validateTexBufferRange(ctx, target, internalFormat, buffer, offset, size)) return;
     GLClientState* state = ctx->m_state;
     state->setBoundTextureInternalFormat(target, internalFormat);
     ctx->m_glTexBufferRangeEXT_enc(ctx, target, internalFormat, buffer, offset, size);
}

bool GL2Encoder::validateAllowedEnablei(void* self, GLenum cap, GLuint index) {
     GL2Encoder* ctx = (GL2Encoder*)self;
     switch(cap)
     {
     case GL_BLEND:
       RET_AND_SET_ERROR_IF(index >= ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE, false);
       break;
     default:
       RET_AND_SET_ERROR_IF(false, GL_INVALID_ENUM, false);
     }
     return true;
}

void GL2Encoder::s_glEnableiEXT(void * self, GLenum cap, GLuint index)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     if(!validateAllowedEnablei(ctx, cap, index)) return;
     ctx->m_glEnableiEXT_enc(ctx, cap, index);
}

void GL2Encoder::s_glDisableiEXT(void* self, GLenum cap, GLuint index)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     if(!validateAllowedEnablei(ctx, cap, index)) return;
     ctx->m_glDisableiEXT_enc(ctx, cap, index);
}

void GL2Encoder::s_glBlendEquationiEXT(void* self, GLuint buf, GLenum mode)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     SET_ERROR_IF(buf >= ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE);
     SET_ERROR_IF(
        !GLESv2Validation::allowedBlendEquation(mode),
        GL_INVALID_ENUM);
     ctx->m_glBlendEquationiEXT_enc(ctx, buf, mode);
}

void GL2Encoder::s_glBlendEquationSeparateiEXT(void* self, GLuint buf, GLenum modeRGB, GLenum modeAlpha)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     SET_ERROR_IF(buf >= ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE);
     SET_ERROR_IF(
        !GLESv2Validation::allowedBlendEquation(modeRGB) ||
        !GLESv2Validation::allowedBlendEquation(modeAlpha),
        GL_INVALID_ENUM);
     ctx->m_glBlendEquationSeparateiEXT_enc(ctx, buf, modeRGB, modeAlpha);
}

void GL2Encoder::s_glBlendFunciEXT(void* self, GLuint buf, GLenum sfactor, GLenum dfactor)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     SET_ERROR_IF(buf >= ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE);
     SET_ERROR_IF(
        !GLESv2Validation::allowedBlendFunc(sfactor) ||
        !GLESv2Validation::allowedBlendFunc(dfactor),
        GL_INVALID_ENUM);
     ctx->m_glBlendFunciEXT_enc(ctx, buf, sfactor, dfactor);
}

void GL2Encoder::s_glBlendFuncSeparateiEXT(void* self, GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     SET_ERROR_IF(buf >= ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE);
     SET_ERROR_IF(
        !GLESv2Validation::allowedBlendFunc(srcRGB) ||
        !GLESv2Validation::allowedBlendFunc(dstRGB) ||
        !GLESv2Validation::allowedBlendFunc(srcAlpha) ||
        !GLESv2Validation::allowedBlendFunc(dstAlpha),
        GL_INVALID_ENUM);
     ctx->m_glBlendFuncSeparateiEXT_enc(ctx, buf, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

void GL2Encoder::s_glColorMaskiEXT(void* self, GLuint buf, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION);
     SET_ERROR_IF(buf >= ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE);
     ctx->m_glColorMaskiEXT_enc(ctx, buf, red, green, blue, alpha);
}

GLboolean GL2Encoder::s_glIsEnablediEXT(void* self, GLenum cap, GLuint index)
{
     GL2Encoder* ctx = (GL2Encoder*)self;
     RET_AND_SET_ERROR_IF(!ctx->getExtensions().drawBuffersIndexedEXT, GL_INVALID_OPERATION, GL_FALSE);
     if(!validateAllowedEnablei(ctx, cap, index)) return GL_FALSE;
     return ctx->m_glIsEnablediEXT_enc(ctx, cap, index);
}

static int ilog2(uint32_t x) {
    int p = 0;
    while ((1 << p) < x)
        p++;
    return p;
}

void GL2Encoder::s_glTexImage2D(void* self, GLenum target, GLint level,
        GLint internalformat, GLsizei width, GLsizei height, GLint border,
        GLenum format, GLenum type, const GLvoid* pixels)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelType(ctx, type), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelFormat(ctx, format), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelFormat(ctx, internalformat) && !GLESv2Validation::pixelInternalFormat(internalformat), GL_INVALID_VALUE);
    SET_ERROR_IF(!(GLESv2Validation::pixelOp(format,type)),GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::pixelSizedFormat(ctx, internalformat, format, type), GL_INVALID_OPERATION);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);

    GLint max_texture_size;
    GLint max_cube_map_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_map_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF((target == GL_TEXTURE_CUBE_MAP) &&
                 (level > ilog2(max_cube_map_texture_size)), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && width > max_cube_map_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && height > max_cube_map_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && (width != height), GL_INVALID_VALUE);
    SET_ERROR_IF(border != 0, GL_INVALID_VALUE);
    // If unpack buffer is nonzero, verify buffer data fits and is evenly divisible by the type.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (ctx->m_state->pboNeededDataSize(width, height, 1, format, type, 0) >
                  ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size %
                  glSizeof(type)),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 ((uintptr_t)pixels % glSizeof(type)),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(state->isBoundTextureImmutableFormat(target), GL_INVALID_OPERATION);

    GLenum stateTarget = target;
    if (target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        stateTarget = GL_TEXTURE_CUBE_MAP;

    state->setBoundTextureInternalFormat(stateTarget, internalformat);
    state->setBoundTextureFormat(stateTarget, format);
    state->setBoundTextureType(stateTarget, type);
    state->setBoundTextureDims(stateTarget, target, level, width, height, 1);
    state->addTextureCubeMapImage(stateTarget, target);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
    }

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glTexImage2DOffsetAEMU(
                ctx, target, level, internalformat,
                width, height, border,
                format, type, (uintptr_t)pixels);
    } else {
        ctx->m_glTexImage2D_enc(
                ctx, target, level, internalformat,
                width, height, border,
                format, type, pixels);
    }

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->restore2DTextureTarget(target);
    }
}

void GL2Encoder::s_glTexSubImage2D(void* self, GLenum target, GLint level,
        GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format,
        GLenum type, const GLvoid* pixels)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelType(ctx, type), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelFormat(ctx, format), GL_INVALID_ENUM);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);

    GLint max_texture_size;
    GLint max_cube_map_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_map_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) &&
                 level > ilog2(max_cube_map_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0, GL_INVALID_VALUE);

    GLuint tex = state->getBoundTexture(target);
    GLsizei neededWidth = xoffset + width;
    GLsizei neededHeight = yoffset + height;
    GLsizei neededDepth = 1;

    if (tex && !state->queryTexEGLImageBacked(tex)) {
        SET_ERROR_IF(
                (neededWidth > state->queryTexWidth(level, tex) ||
                 neededHeight > state->queryTexHeight(level, tex) ||
                 neededDepth > state->queryTexDepth(level, tex)),
                GL_INVALID_VALUE);
    }

    // If unpack buffer is nonzero, verify buffer data fits and is evenly divisible by the type.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (state->pboNeededDataSize(width, height, 1, format, type, 0, 1) + (uintptr_t)pixels >
                  ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 ((uintptr_t)pixels %
                  glSizeof(type)),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) && !pixels, GL_INVALID_OPERATION);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
    }

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glTexSubImage2DOffsetAEMU(
                ctx, target, level,
                xoffset, yoffset, width, height,
                format, type, (uintptr_t)pixels);
    } else {
        ctx->m_glTexSubImage2D_enc(ctx, target, level, xoffset, yoffset, width,
                height, format, type, pixels);
    }

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->restore2DTextureTarget(target);
    }
}

void GL2Encoder::s_glCopyTexImage2D(void* self, GLenum target, GLint level,
        GLenum internalformat, GLint x, GLint y,
        GLsizei width, GLsizei height, GLint border)
{
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelFormat(ctx, internalformat) && !GLESv2Validation::pixelInternalFormat(internalformat), GL_INVALID_VALUE);
    GLint max_texture_size;
    GLint max_cube_map_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_map_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF((target == GL_TEXTURE_CUBE_MAP) &&
                 (level > ilog2(max_cube_map_texture_size)), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && width > max_cube_map_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && height > max_cube_map_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && (width != height), GL_INVALID_VALUE);
    SET_ERROR_IF(border != 0, GL_INVALID_VALUE);

    GLenum stateTarget = target;
    if (target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        stateTarget = GL_TEXTURE_CUBE_MAP;

    SET_ERROR_IF(state->isBoundTextureImmutableFormat(target), GL_INVALID_OPERATION);

    SET_ERROR_IF(ctx->glCheckFramebufferStatus(ctx, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE,
                 GL_INVALID_FRAMEBUFFER_OPERATION);
    // This is needed to work around underlying OpenGL drivers
    // (such as those feeding some some AMD GPUs) that expect
    // positive components of cube maps to be defined _before_
    // the negative components (otherwise a segfault occurs).
    GLenum extraTarget =
        state->copyTexImageLuminanceCubeMapAMDWorkaround
            (target, level, internalformat);

    state->setBoundTextureInternalFormat(stateTarget, internalformat);
    state->setBoundTextureDims(stateTarget, target, level, width, height, 1);
    state->addTextureCubeMapImage(stateTarget, target);

    if (extraTarget) {
        ctx->m_glCopyTexImage2D_enc(ctx, extraTarget, level, internalformat,
                                    x, y, width, height, border);
    }

    ctx->m_glCopyTexImage2D_enc(ctx, target, level, internalformat,
                                x, y, width, height, border);
}

void GL2Encoder::s_glTexParameteriv(void* self,
        GLenum target, GLenum pname, const GLint* params)
{
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF((target == GL_TEXTURE_EXTERNAL_OES &&
            !isValidTextureExternalParam(pname, (GLenum)params[0])),
            GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!params, GL_INVALID_VALUE);
    GLint param = *params;
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, param, (GLfloat)param, (GLenum)param), GL_INVALID_ENUM);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
        ctx->m_glTexParameteriv_enc(ctx, GL_TEXTURE_2D, pname, params);
        ctx->restore2DTextureTarget(target);
    } else {
        ctx->m_glTexParameteriv_enc(ctx, target, pname, params);
    }
}

bool GL2Encoder::texture2DNeedsOverride(GLenum target) const {
    return (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) &&
           target != m_state->getPriorityEnabledTarget(GL_TEXTURE_2D);
}

void GL2Encoder::override2DTextureTarget(GLenum target)
{
    if (texture2DNeedsOverride(target)) {
        m_glBindTexture_enc(this, GL_TEXTURE_2D,
                m_state->getBoundTexture(target));
    }
}

void GL2Encoder::restore2DTextureTarget(GLenum target)
{
    if (texture2DNeedsOverride(target)) {
        GLuint priorityEnabledBoundTexture =
                m_state->getBoundTexture(
                    m_state->getPriorityEnabledTarget(GL_TEXTURE_2D));
        GLuint texture2DBoundTexture =
                m_state->getBoundTexture(GL_TEXTURE_2D);
        if (!priorityEnabledBoundTexture) {
            m_glBindTexture_enc(this, GL_TEXTURE_2D, texture2DBoundTexture);
        } else {
            m_glBindTexture_enc(this, GL_TEXTURE_2D, priorityEnabledBoundTexture);
        }
    }
}

void GL2Encoder::associateEGLImage(GLenum target, GLeglImageOES eglImage, int width, int height) {
    m_state->setBoundEGLImage(target, eglImage, width, height);
}


GLuint GL2Encoder::boundBuffer(GLenum target) const {
    return m_state->getBuffer(target);
}

BufferData* GL2Encoder::getBufferData(GLenum target) const {
    GLuint bufferId = m_state->getBuffer(target);
    if (!bufferId) return NULL;
    return m_shared->getBufferData(bufferId);
}

BufferData* GL2Encoder::getBufferDataById(GLuint bufferId) const {
    if (!bufferId) return NULL;
    return m_shared->getBufferData(bufferId);
}

bool GL2Encoder::isBufferMapped(GLuint buffer) const {
    return m_shared->getBufferData(buffer)->m_mapped;
}

bool GL2Encoder::isBufferTargetMapped(GLenum target) const {
    BufferData* buf = getBufferData(target);
    if (!buf) return false;
    return buf->m_mapped;
}

void GL2Encoder::s_glGenRenderbuffers(void* self,
        GLsizei n, GLuint* renderbuffers) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);

    ctx->m_glGenRenderbuffers_enc(self, n, renderbuffers);
    state->addRenderbuffers(n, renderbuffers);
}

void GL2Encoder::s_glDeleteRenderbuffers(void* self,
        GLsizei n, const GLuint* renderbuffers) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);

    ctx->m_glDeleteRenderbuffers_enc(self, n, renderbuffers);

    // Nope, lets just leak those for now.
    // The spec has an *amazingly* convoluted set of conditions for when
    // render buffers are actually deleted:
    // glDeleteRenderbuffers deletes the n renderbuffer objects whose names are stored in the array addressed by renderbuffers. Unused names in renderbuffers that have been marked as used for the purposes of glGenRenderbuffers are marked as unused again. The name zero is reserved by the GL and is silently ignored, should it occur in renderbuffers, as are other unused names. Once a renderbuffer object is deleted, its name is again unused and it has no contents. If a renderbuffer that is currently bound to the target GL_RENDERBUFFER is deleted, it is as though glBindRenderbuffer had been executed with a target of GL_RENDERBUFFER and a name of zero.
    //
    // If a renderbuffer object is attached to one or more attachment points in the currently bound framebuffer, then it as if glFramebufferRenderbuffer had been called, with a renderbuffer of zero for each attachment point to which this image was attached in the currently bound framebuffer. In other words, this renderbuffer object is first detached from all attachment ponits in the currently bound framebuffer. ***Note that the renderbuffer image is specifically not detached from any non-bound framebuffers***
    //
    // So, just detach this one from the bound FBO, and ignore the rest.
    for (int i = 0; i < n; i++) {
        state->detachRbo(renderbuffers[i]);
    }
    state->removeRenderbuffers(n, renderbuffers);
}

void GL2Encoder::s_glBindRenderbuffer(void* self,
        GLenum target, GLuint renderbuffer) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF((target != GL_RENDERBUFFER),
                 GL_INVALID_ENUM);

    ctx->m_glBindRenderbuffer_enc(self, target, renderbuffer);
    state->bindRenderbuffer(target, renderbuffer);
}

void GL2Encoder::s_glRenderbufferStorage(void* self,
        GLenum target, GLenum internalformat,
        GLsizei width, GLsizei height) {
    GL2Encoder* ctx = (GL2Encoder*) self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_RENDERBUFFER, GL_INVALID_ENUM);
    SET_ERROR_IF(0 == ctx->m_state->boundRenderbuffer(), GL_INVALID_OPERATION);
    SET_ERROR_IF(
        !GLESv2Validation::rboFormat(ctx, internalformat),
        GL_INVALID_ENUM);

    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    GLint max_rb_size;
    ctx->glGetIntegerv(ctx, GL_MAX_RENDERBUFFER_SIZE, &max_rb_size);
    SET_ERROR_IF(width > max_rb_size || height > max_rb_size, GL_INVALID_VALUE);

    state->setBoundRenderbufferFormat(internalformat);
    state->setBoundRenderbufferSamples(0);
    state->setBoundRenderbufferDimensions(width, height);

    ctx->m_glRenderbufferStorage_enc(self, target, internalformat,
                                     width, height);
}

void GL2Encoder::s_glFramebufferRenderbuffer(void* self,
        GLenum target, GLenum attachment,
        GLenum renderbuffertarget, GLuint renderbuffer) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::framebufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::framebufferAttachment(ctx, attachment), GL_INVALID_ENUM);
    SET_ERROR_IF(GL_RENDERBUFFER != renderbuffertarget, GL_INVALID_ENUM);
    SET_ERROR_IF(!state->getBoundFramebuffer(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(!state->isRenderbufferThatWasBound(renderbuffer), GL_INVALID_OPERATION);

    state->attachRbo(target, attachment, renderbuffer);

    ctx->m_glFramebufferRenderbuffer_enc(self, target, attachment, renderbuffertarget, renderbuffer);
}

void GL2Encoder::s_glGenFramebuffers(void* self,
        GLsizei n, GLuint* framebuffers) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);

    ctx->m_glGenFramebuffers_enc(self, n, framebuffers);
    state->addFramebuffers(n, framebuffers);
}

void GL2Encoder::s_glDeleteFramebuffers(void* self,
        GLsizei n, const GLuint* framebuffers) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);

    ctx->m_glDeleteFramebuffers_enc(self, n, framebuffers);
    state->removeFramebuffers(n, framebuffers);
}

void GL2Encoder::s_glBindFramebuffer(void* self,
        GLenum target, GLuint framebuffer) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::framebufferTarget(ctx, target), GL_INVALID_ENUM);

    state->bindFramebuffer(target, framebuffer);

    ctx->m_glBindFramebuffer_enc(self, target, framebuffer);
}

void GL2Encoder::s_glFramebufferParameteri(void *self,
        GLenum target, GLenum pname, GLint param) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    state->setFramebufferParameter(target, pname, param);
    ctx->m_glFramebufferParameteri_enc(self, target, pname, param);
}

void GL2Encoder::s_glFramebufferTexture2D(void* self,
        GLenum target, GLenum attachment,
        GLenum textarget, GLuint texture, GLint level) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::framebufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, textarget), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::framebufferAttachment(ctx, attachment), GL_INVALID_ENUM);
    SET_ERROR_IF(!state->getBoundFramebuffer(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(texture && !state->isTexture(texture), GL_INVALID_OPERATION);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(textarget) && !state->isTextureCubeMap(texture), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::isCubeMapTarget(textarget) && state->isTextureCubeMap(texture), GL_INVALID_OPERATION);
    SET_ERROR_IF((texture && (level < 0)), GL_INVALID_VALUE);

    if (textarget == GL_TEXTURE_2D) {
        SET_ERROR_IF(level > ilog2(ctx->m_state->getMaxTextureSize()), GL_INVALID_VALUE);
    } else {
        SET_ERROR_IF(level > ilog2(ctx->m_state->getMaxTextureSizeCubeMap()), GL_INVALID_VALUE);
    }

    state->attachTextureObject(target, attachment, texture, level, 0);

    ctx->m_glFramebufferTexture2D_enc(self, target, attachment, textarget, texture, level);
}

void GL2Encoder::s_glFramebufferTexture3DOES(void* self,
        GLenum target, GLenum attachment,
        GLenum textarget, GLuint texture, GLint level, GLint zoffset) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    state->attachTextureObject(target, attachment, texture, level, zoffset);

    ctx->m_glFramebufferTexture3DOES_enc(self, target, attachment, textarget, texture, level, zoffset);
}

void GL2Encoder::s_glGetFramebufferAttachmentParameteriv(void* self,
        GLenum target, GLenum attachment, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    const GLClientState* state = ctx->m_state;
    SET_ERROR_IF(!GLESv2Validation::framebufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!state->boundFramebuffer(target) &&
                 attachment != GL_BACK &&
                 attachment != GL_FRONT &&
                 attachment != GL_DEPTH &&
                 attachment != GL_STENCIL,
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(pname != GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME &&
                 pname != GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE &&
                 !state->attachmentHasObject(target, attachment),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF((pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL ||
                  pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE ||
                  pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER) &&
                 (!state->attachmentHasObject(target, attachment) ||
                  state->getBoundFramebufferAttachmentType(target, attachment) !=
                  FBO_ATTACHMENT_TEXTURE),
                 !state->attachmentHasObject(target, attachment) ?
                 GL_INVALID_OPERATION : GL_INVALID_ENUM);
    SET_ERROR_IF(
        (attachment == GL_FRONT ||
         attachment == GL_BACK) &&
        (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME),
        GL_INVALID_ENUM);
    SET_ERROR_IF(attachment == GL_DEPTH_STENCIL_ATTACHMENT &&
                 pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME &&
                 !state->depthStencilHasSameObject(target),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(state->boundFramebuffer(target) &&
                 (attachment == GL_BACK ||
                  attachment == GL_FRONT ||
                  attachment == GL_DEPTH ||
                  attachment == GL_STENCIL),
                 GL_INVALID_OPERATION);
    ctx->m_glGetFramebufferAttachmentParameteriv_enc(self, target, attachment, pname, params);
}

GLenum GL2Encoder::s_glCheckFramebufferStatus(void* self, GLenum target) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    RET_AND_SET_ERROR_IF(
        target != GL_DRAW_FRAMEBUFFER && target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER,
        GL_INVALID_ENUM, 0);

    GLClientState* state = ctx->m_state;

    return state->checkFramebufferCompleteness(target);
}

void GL2Encoder::s_glGenVertexArrays(void* self, GLsizei n, GLuint* arrays) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);

    ctx->m_glGenVertexArrays_enc(self, n, arrays);
    for (int i = 0; i < n; i++) {
        GFXSTREAM_VERBOSE("gen vao %u", arrays[i]);
    }
    state->addVertexArrayObjects(n, arrays);
}

void GL2Encoder::s_glDeleteVertexArrays(void* self, GLsizei n, const GLuint* arrays) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(n < 0, GL_INVALID_VALUE);

    ctx->m_glDeleteVertexArrays_enc(self, n, arrays);
    for (int i = 0; i < n; i++) {
        GFXSTREAM_VERBOSE("delete vao %u", arrays[i]);
    }
    state->removeVertexArrayObjects(n, arrays);
}

void GL2Encoder::s_glBindVertexArray(void* self, GLuint array) {
    GFXSTREAM_VERBOSE("call. array=%u\n", array);
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(!state->isVertexArrayObject(array), GL_INVALID_OPERATION);
    ctx->m_glBindVertexArray_enc(self, array);
    state->setVertexArrayObject(array);
}

void* GL2Encoder::s_glMapBufferOES(void* self, GLenum target, GLenum access) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    RET_AND_SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM, NULL);

    GLuint boundBuffer = ctx->m_state->getBuffer(target);

    RET_AND_SET_ERROR_IF(boundBuffer == 0, GL_INVALID_OPERATION, NULL);

    BufferData* buf = ctx->m_shared->getBufferData(boundBuffer);
    RET_AND_SET_ERROR_IF(!buf, GL_INVALID_VALUE, NULL);

    return ctx->glMapBufferRange(ctx, target, 0, buf->m_size, access);
}

GLboolean GL2Encoder::s_glUnmapBufferOES(void* self, GLenum target) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    return ctx->glUnmapBuffer(ctx, target);
}

void* GL2Encoder::s_glMapBufferRangeAEMUImpl(GL2Encoder* ctx, GLenum target,
                                             GLintptr offset, GLsizeiptr length,
                                             GLbitfield access, BufferData* buf) {
    char* bits = &buf->m_fixedBuffer[offset];

    if ((access & GL_MAP_READ_BIT) ||
        ((access & GL_MAP_WRITE_BIT) &&
        (!(access & GL_MAP_INVALIDATE_RANGE_BIT) &&
         !(access & GL_MAP_INVALIDATE_BUFFER_BIT)))) {

        if (ctx->m_state->shouldSkipHostMapBuffer(target))
            return bits;

        ctx->glMapBufferRangeAEMU(
                ctx, target,
                offset, length,
                access,
                bits);

        ctx->m_state->onHostMappedBuffer(target);
    }

    return bits;
}

void* GL2Encoder::s_glMapBufferRange(void* self, GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    // begin validation (lots)

    RET_AND_SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM, NULL);

    GLuint boundBuffer = ctx->m_state->getBuffer(target);

    RET_AND_SET_ERROR_IF(boundBuffer == 0, GL_INVALID_OPERATION, NULL);

    BufferData* buf = ctx->m_shared->getBufferData(boundBuffer);
    RET_AND_SET_ERROR_IF(!buf, GL_INVALID_VALUE, NULL);

    GLsizeiptr bufferDataSize = buf->m_size;

    RET_AND_SET_ERROR_IF(offset < 0, GL_INVALID_VALUE, NULL);
    RET_AND_SET_ERROR_IF(length < 0, GL_INVALID_VALUE, NULL);
    RET_AND_SET_ERROR_IF(offset + length > bufferDataSize, GL_INVALID_VALUE, NULL);
    RET_AND_SET_ERROR_IF(access & ~GLESv2Validation::allBufferMapAccessFlags, GL_INVALID_VALUE, NULL);

    RET_AND_SET_ERROR_IF(buf->m_mapped, GL_INVALID_OPERATION, NULL);
    RET_AND_SET_ERROR_IF(!(access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT)), GL_INVALID_OPERATION, NULL);
    RET_AND_SET_ERROR_IF(
        (access & GL_MAP_READ_BIT) &&
             ((access & GL_MAP_INVALIDATE_RANGE_BIT) ||
              (access & GL_MAP_INVALIDATE_BUFFER_BIT) ||
              (access & GL_MAP_UNSYNCHRONIZED_BIT) ||
              (access & GL_MAP_FLUSH_EXPLICIT_BIT)), GL_INVALID_OPERATION, NULL);

    // end validation; actually do stuff now

    buf->m_mapped = true;
    buf->m_mappedAccess = access;
    buf->m_mappedOffset = offset;
    buf->m_mappedLength = length;

    return s_glMapBufferRangeAEMUImpl(ctx, target, offset, length, access, buf);
}

GLboolean GL2Encoder::s_glUnmapBuffer(void* self, GLenum target) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    RET_AND_SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM, GL_FALSE);

    GLuint boundBuffer = ctx->m_state->getBuffer(target);

    RET_AND_SET_ERROR_IF(boundBuffer == 0, GL_INVALID_OPERATION, GL_FALSE);

    BufferData* buf = ctx->m_shared->getBufferData(boundBuffer);
    RET_AND_SET_ERROR_IF(!buf, GL_INVALID_VALUE, GL_FALSE);
    RET_AND_SET_ERROR_IF(!buf->m_mapped, GL_INVALID_OPERATION, GL_FALSE);

    if (buf->m_mappedAccess & GL_MAP_WRITE_BIT) {
        // invalide index range cache here
        if (buf->m_mappedAccess & GL_MAP_INVALIDATE_BUFFER_BIT) {
            buf->m_indexRangeCache.invalidateRange(0, buf->m_size);
        } else {
            buf->m_indexRangeCache.invalidateRange(buf->m_mappedOffset, buf->m_mappedLength);
        }
    }

    GLboolean host_res = GL_TRUE;

    if (ctx->m_hasAsyncUnmapBuffer) {
        ctx->glUnmapBufferAsyncAEMU(
                ctx, target,
                buf->m_mappedOffset,
                buf->m_mappedLength,
                buf->m_mappedAccess,
                &buf->m_fixedBuffer[buf->m_mappedOffset],
                &host_res);
    } else {
        if (buf->m_mappedAccess & GL_MAP_WRITE_BIT) {
            ctx->glUnmapBufferAEMU(
                    ctx, target,
                    buf->m_mappedOffset,
                    buf->m_mappedLength,
                    buf->m_mappedAccess,
                    &buf->m_fixedBuffer[buf->m_mappedOffset],
                    &host_res);
        }
    }

    buf->m_mapped = false;
    buf->m_mappedAccess = 0;
    buf->m_mappedOffset = 0;
    buf->m_mappedLength = 0;

    return host_res;
}

void GL2Encoder::s_glFlushMappedBufferRange(void* self, GLenum target, GLintptr offset, GLsizeiptr length) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);

    GLuint boundBuffer = ctx->m_state->getBuffer(target);
    SET_ERROR_IF(!boundBuffer, GL_INVALID_OPERATION);

    BufferData* buf = ctx->m_shared->getBufferData(boundBuffer);
    SET_ERROR_IF(!buf, GL_INVALID_VALUE);
    SET_ERROR_IF(!buf->m_mapped, GL_INVALID_OPERATION);
    SET_ERROR_IF(!(buf->m_mappedAccess & GL_MAP_FLUSH_EXPLICIT_BIT), GL_INVALID_OPERATION);

    SET_ERROR_IF(offset < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(length < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(offset + length > buf->m_mappedLength, GL_INVALID_VALUE);

    GLintptr totalOffset = buf->m_mappedOffset + offset;

    buf->m_indexRangeCache.invalidateRange(totalOffset, length);

    if (ctx->m_hasAsyncUnmapBuffer) {
        ctx->glFlushMappedBufferRangeAEMU2(
                ctx, target,
                totalOffset,
                length,
                buf->m_mappedAccess,
                &buf->m_fixedBuffer[totalOffset]);
    } else {
        ctx->glFlushMappedBufferRangeAEMU(
                ctx, target,
                totalOffset,
                length,
                buf->m_mappedAccess,
                &buf->m_fixedBuffer[totalOffset]);
    }
}

void GL2Encoder::s_glCompressedTexImage2D(void* self, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(target == GL_TEXTURE_CUBE_MAP, GL_INVALID_ENUM);
    fprintf(stderr, "%s: format: 0x%x\n", __func__, internalformat);
    // Filter compressed formats support.
    SET_ERROR_IF(!GLESv2Validation::supportedCompressedFormat(ctx, internalformat), GL_INVALID_ENUM);
    // Verify level <= log2(GL_MAX_TEXTURE_SIZE).
    GLint max_texture_size;
    GLint max_cube_map_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_map_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_cube_map_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(border, GL_INVALID_VALUE);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);

    // If unpack buffer is nonzero, verify buffer data fits.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (imageSize > ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_state->compressedTexImageSizeCompatible(internalformat, width, height, 1, imageSize), GL_INVALID_VALUE);

    GLenum stateTarget = target;
    if (target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        stateTarget = GL_TEXTURE_CUBE_MAP;
    state->setBoundTextureInternalFormat(stateTarget, (GLint)internalformat);
    state->setBoundTextureDims(stateTarget, target, level, width, height, 1);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
    }

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glCompressedTexImage2DOffsetAEMU(
                ctx, target, level, internalformat,
                width, height, border,
                imageSize, (uintptr_t)data);
    } else {
        ctx->m_glCompressedTexImage2D_enc(
                ctx, target, level, internalformat,
                width, height, border,
                imageSize, data);
    }

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->restore2DTextureTarget(target);
    }
}

void GL2Encoder::s_glCompressedTexSubImage2D(void* self, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid* data) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(target == GL_TEXTURE_CUBE_MAP, GL_INVALID_ENUM);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);

    GLenum stateTarget = target;
    if (target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        stateTarget = GL_TEXTURE_CUBE_MAP;
    GLuint tex = ctx->m_state->getBoundTexture(stateTarget);

    GLint internalFormat = ctx->m_state->queryTexInternalFormat(tex);
    SET_ERROR_IF(internalFormat != format, GL_INVALID_OPERATION);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);

    GLint max_texture_size;
    GLint max_cube_map_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_map_texture_size);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_cube_map_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    // If unpack buffer is nonzero, verify buffer data fits.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (imageSize > ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0, GL_INVALID_VALUE);

    GLint totalWidth = ctx->m_state->queryTexWidth(level, tex);
    GLint totalHeight = ctx->m_state->queryTexHeight(level, tex);

    if (GLESTextureUtils::isEtc2Format(internalFormat)) {
        SET_ERROR_IF((width % 4) && (totalWidth != xoffset + width), GL_INVALID_OPERATION);
        SET_ERROR_IF((height % 4) && (totalHeight != yoffset + height), GL_INVALID_OPERATION);
        SET_ERROR_IF((xoffset % 4) || (yoffset % 4), GL_INVALID_OPERATION);
    }

    SET_ERROR_IF(totalWidth < xoffset + width, GL_INVALID_VALUE);
    SET_ERROR_IF(totalHeight < yoffset + height, GL_INVALID_VALUE);

    SET_ERROR_IF(!ctx->m_state->compressedTexImageSizeCompatible(internalFormat, width, height, 1, imageSize), GL_INVALID_VALUE);

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->override2DTextureTarget(target);
    }

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glCompressedTexSubImage2DOffsetAEMU(
                ctx, target, level,
                xoffset, yoffset,
                width, height, format,
                imageSize, (uintptr_t)data);
    } else {
        ctx->m_glCompressedTexSubImage2D_enc(
                ctx, target, level,
                xoffset, yoffset,
                width, height, format,
                imageSize, data);
    }

    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_EXTERNAL_OES) {
        ctx->restore2DTextureTarget(target);
    }
}

void GL2Encoder::s_glBindBufferRange(void* self, GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);

    // Only works with certain targets
    SET_ERROR_IF(
        !(target == GL_ATOMIC_COUNTER_BUFFER ||
          target == GL_SHADER_STORAGE_BUFFER ||
          target == GL_TRANSFORM_FEEDBACK_BUFFER ||
          target == GL_UNIFORM_BUFFER),
        GL_INVALID_ENUM);

    // Can't exceed range
    SET_ERROR_IF(index < 0 ||
                 index >= state->getMaxIndexedBufferBindings(target),
                 GL_INVALID_VALUE);
    SET_ERROR_IF(buffer && size <= 0, GL_INVALID_VALUE);
    SET_ERROR_IF((target == GL_ATOMIC_COUNTER_BUFFER ||
                  target == GL_TRANSFORM_FEEDBACK_BUFFER) &&
                 (size % 4 || offset % 4),
                 GL_INVALID_VALUE);

    GLint ssbo_offset_align, ubo_offset_align;

    if (ctx->majorVersion() >= 3 && ctx->minorVersion() >= 1) {
        ctx->s_glGetIntegerv(ctx, GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_offset_align);
        SET_ERROR_IF(target == GL_SHADER_STORAGE_BUFFER &&
                     offset % ssbo_offset_align,
                     GL_INVALID_VALUE);
    }

    ctx->s_glGetIntegerv(ctx, GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_align);
    SET_ERROR_IF(target == GL_UNIFORM_BUFFER &&
                 offset % ubo_offset_align,
                 GL_INVALID_VALUE);

    if (ctx->m_state->isIndexedBindNoOp(target, index, buffer, offset, size, 0, 0)) return;

    state->bindBuffer(target, buffer);
    ctx->m_state->addBuffer(buffer);
    state->bindIndexedBuffer(target, index, buffer, offset, size, 0, 0);

    ctx->m_glBindBufferRange_enc(ctx, target, index, buffer, offset, size);
    ctx->m_state->setLastEncodedBufferBind(target, buffer);
}

void GL2Encoder::s_glBindBufferBase(void* self, GLenum target, GLuint index, GLuint buffer) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);

    // Only works with certain targets
    SET_ERROR_IF(
        !(target == GL_ATOMIC_COUNTER_BUFFER ||
          target == GL_SHADER_STORAGE_BUFFER ||
          target == GL_TRANSFORM_FEEDBACK_BUFFER ||
          target == GL_UNIFORM_BUFFER),
        GL_INVALID_ENUM);
    // Can't exceed range
    SET_ERROR_IF(index < 0 ||
                 index >= state->getMaxIndexedBufferBindings(target),
                 GL_INVALID_VALUE);

    BufferData* buf = ctx->getBufferDataById(buffer);
    GLsizeiptr size = buf ? buf->m_size : 0;

    if (ctx->m_state->isIndexedBindNoOp(target, index, buffer, 0, size, 0, 0)) return;

    state->bindBuffer(target, buffer);
    ctx->m_state->addBuffer(buffer);

    state->bindIndexedBuffer(target, index, buffer, 0, size, 0, 0);

    ctx->m_glBindBufferBase_enc(ctx, target, index, buffer);
    ctx->m_state->setLastEncodedBufferBind(target, buffer);
}

void GL2Encoder::doIndexedBufferBindEncodeCached(IndexedBufferBindOp op, GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, GLintptr stride, GLintptr effectiveStride)
{
    if (m_state->isIndexedBindNoOp(target, index, buffer, offset, size, stride, effectiveStride)) return;

    switch (op) {
        case BindBufferBase:
            // can emulate with bindBufferRange
        case BindBufferRange:
            m_glBindBufferRange_enc(this, target, index, buffer, offset, size);
            break;
        // TODO: other ops
    }

    m_state->setLastEncodedBufferBind(target, buffer);
}

void GL2Encoder::s_glCopyBufferSubData(void *self , GLenum readtarget, GLenum writetarget, GLintptr readoffset, GLintptr writeoffset, GLsizeiptr size) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, readtarget), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, writetarget), GL_INVALID_ENUM);
    SET_ERROR_IF((readtarget == GL_ATOMIC_COUNTER_BUFFER ||
                  readtarget == GL_DISPATCH_INDIRECT_BUFFER ||
                  readtarget == GL_DRAW_INDIRECT_BUFFER ||
                  readtarget == GL_SHADER_STORAGE_BUFFER), GL_INVALID_ENUM);
    SET_ERROR_IF((writetarget == GL_ATOMIC_COUNTER_BUFFER ||
                  writetarget == GL_DISPATCH_INDIRECT_BUFFER ||
                  writetarget == GL_DRAW_INDIRECT_BUFFER ||
                  writetarget == GL_SHADER_STORAGE_BUFFER), GL_INVALID_ENUM);

    GLuint readBufferId = ctx->boundBuffer(readtarget);
    GLuint writeBufferId = ctx->boundBuffer(writetarget);

    SET_ERROR_IF(!readBufferId || !writeBufferId, GL_INVALID_OPERATION);

    SET_ERROR_IF(ctx->isBufferTargetMapped(readtarget), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->isBufferTargetMapped(writetarget), GL_INVALID_OPERATION);

    SET_ERROR_IF(readoffset < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(writeoffset < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(size < 0, GL_INVALID_VALUE);

    BufferData* readBufferData = ctx->getBufferData(readtarget);
    BufferData* writeBufferData = ctx->getBufferData(writetarget);

    SET_ERROR_IF(
        readBufferData &&
        (readoffset + size > readBufferData->m_size),
        GL_INVALID_VALUE);

    SET_ERROR_IF(
        writeBufferData &&
        (writeoffset + size > writeBufferData->m_size),
        GL_INVALID_VALUE);

    SET_ERROR_IF(readBufferId == writeBufferId &&
                 !((writeoffset >= readoffset + size) ||
                   (readoffset >= writeoffset + size)),
                 GL_INVALID_VALUE);

    ctx->m_glCopyBufferSubData_enc(self, readtarget, writetarget, readoffset, writeoffset, size);
}

void GL2Encoder::s_glGetBufferParameteriv(void* self, GLenum target, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(
        target != GL_ARRAY_BUFFER &&
        target != GL_ELEMENT_ARRAY_BUFFER &&
        target != GL_COPY_READ_BUFFER &&
        target != GL_COPY_WRITE_BUFFER &&
        target != GL_PIXEL_PACK_BUFFER &&
        target != GL_PIXEL_UNPACK_BUFFER &&
        target != GL_TRANSFORM_FEEDBACK_BUFFER &&
        target != GL_UNIFORM_BUFFER,
        GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::bufferParam(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->boundBuffer(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(pname != GL_BUFFER_ACCESS_FLAGS &&
                 pname != GL_BUFFER_MAPPED &&
                 pname != GL_BUFFER_SIZE &&
                 pname != GL_BUFFER_USAGE &&
                 pname != GL_BUFFER_MAP_LENGTH &&
                 pname != GL_BUFFER_MAP_OFFSET,
                 GL_INVALID_ENUM);

    if (!params) return;

    BufferData* buf = ctx->getBufferData(target);

    switch (pname) {
        case GL_BUFFER_ACCESS_FLAGS:
            *params = buf ? buf->m_mappedAccess : 0;
            break;
        case GL_BUFFER_MAPPED:
            *params = buf ? (buf->m_mapped ? GL_TRUE : GL_FALSE) : GL_FALSE;
            break;
        case GL_BUFFER_SIZE:
            *params = buf ? buf->m_size : 0;
            break;
        case GL_BUFFER_USAGE:
            *params = buf ? buf->m_usage : GL_STATIC_DRAW;
            break;
        case GL_BUFFER_MAP_LENGTH:
            *params = buf ? buf->m_mappedLength : 0;
            break;
        case GL_BUFFER_MAP_OFFSET:
            *params = buf ? buf->m_mappedOffset : 0;
            break;
        default:
            break;
    }
}

void GL2Encoder::s_glGetBufferParameteri64v(void* self, GLenum target, GLenum pname, GLint64* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(
        target != GL_ARRAY_BUFFER &&
        target != GL_ELEMENT_ARRAY_BUFFER &&
        target != GL_COPY_READ_BUFFER &&
        target != GL_COPY_WRITE_BUFFER &&
        target != GL_PIXEL_PACK_BUFFER &&
        target != GL_PIXEL_UNPACK_BUFFER &&
        target != GL_TRANSFORM_FEEDBACK_BUFFER &&
        target != GL_UNIFORM_BUFFER,
        GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::bufferParam(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->boundBuffer(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(pname != GL_BUFFER_ACCESS_FLAGS &&
                 pname != GL_BUFFER_MAPPED &&
                 pname != GL_BUFFER_SIZE &&
                 pname != GL_BUFFER_USAGE &&
                 pname != GL_BUFFER_MAP_LENGTH &&
                 pname != GL_BUFFER_MAP_OFFSET,
                 GL_INVALID_ENUM);

    if (!params) return;

    BufferData* buf = ctx->getBufferData(target);

    switch (pname) {
        case GL_BUFFER_ACCESS_FLAGS:
            *params = buf ? buf->m_mappedAccess : 0;
            break;
        case GL_BUFFER_MAPPED:
            *params = buf ? (buf->m_mapped ? GL_TRUE : GL_FALSE) : GL_FALSE;
            break;
        case GL_BUFFER_SIZE:
            *params = buf ? buf->m_size : 0;
            break;
        case GL_BUFFER_USAGE:
            *params = buf ? buf->m_usage : GL_STATIC_DRAW;
            break;
        case GL_BUFFER_MAP_LENGTH:
            *params = buf ? buf->m_mappedLength : 0;
            break;
        case GL_BUFFER_MAP_OFFSET:
            *params = buf ? buf->m_mappedOffset : 0;
            break;
        default:
            break;
    }
}

void GL2Encoder::s_glGetBufferPointerv(void* self, GLenum target, GLenum pname, GLvoid** params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::bufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(
        target == GL_ATOMIC_COUNTER_BUFFER ||
        target == GL_DISPATCH_INDIRECT_BUFFER ||
        target == GL_DRAW_INDIRECT_BUFFER ||
        target == GL_SHADER_STORAGE_BUFFER,
        GL_INVALID_ENUM);
    SET_ERROR_IF(pname != GL_BUFFER_MAP_POINTER, GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->boundBuffer(target), GL_INVALID_OPERATION);
    if (!params) return;

    BufferData* buf = ctx->getBufferData(target);

    if (!buf || !buf->m_mapped) { *params = NULL; return; }

    *params = &buf->m_fixedBuffer[buf->m_mappedOffset];
}

static const char* const kNameDelimiter = ";";

static std::string packVarNames(GLsizei count, const char** names, GLint* err_out) {

#define VALIDATE(cond, err) if (cond) { *err_out = err; return packed; } \

    std::string packed;
    // validate the array of char[]'s
    const char* currName;
    for (GLsizei i = 0; i < count; i++) {
        currName = names[i];
        VALIDATE(!currName, GL_INVALID_OPERATION);
        // check if has reasonable size
        size_t len = strlen(currName);
        VALIDATE(!len, GL_INVALID_OPERATION);
        // check for our delimiter, which if present
        // in the name, means an invalid name anyway.
        VALIDATE(strstr(currName, kNameDelimiter),
                 GL_INVALID_OPERATION);
        packed += currName;
        packed += ";";
    }

    *err_out = GL_NO_ERROR;
    return packed;
}

void GL2Encoder::s_glGetUniformIndices(void* self, GLuint program, GLsizei uniformCount, const GLchar ** uniformNames, GLuint* uniformIndices) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);

    if (!uniformCount) return;

    GLint err = GL_NO_ERROR;
    std::string packed = packVarNames(uniformCount, (const char**)uniformNames, &err);
    SET_ERROR_IF(err != GL_NO_ERROR, GL_INVALID_OPERATION);

    std::vector<int> arrIndices;
    for (size_t i = 0; i < uniformCount; i++) {
        int err;
        arrIndices.push_back(sArrIndexOfUniformExpr(uniformNames[i], &err));
        if (err) {
            GFXSTREAM_ERROR("Invalid uniform name %s!", uniformNames[i]);
            return;
        }
    }

    ctx->glGetUniformIndicesAEMU(ctx, program, uniformCount, (const GLchar*)&packed[0], packed.size() + 1, uniformIndices);
}

void GL2Encoder::s_glUniform1ui(void* self, GLint location, GLuint v0) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;

    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 1 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform1ui_enc(self, location, v0);

    GLenum target;
    if (shared->setSamplerUniform(state->currentShaderProgram(), location, v0, &target)) {
        GLenum origActiveTexture = state->getActiveTextureUnit();
        if (ctx->updateHostTexture2DBinding(GL_TEXTURE0 + v0, target)) {
            ctx->m_glActiveTexture_enc(self, origActiveTexture);
        }
        state->setActiveTextureUnit(origActiveTexture);
    }
}

void GL2Encoder::s_glUniform2ui(void* self, GLint location, GLuint v0, GLuint v1) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 2 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform2ui_enc(self, location, v0, v1);
}

void GL2Encoder::s_glUniform3ui(void* self, GLint location, GLuint v0, GLuint v1, GLuint v2) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 3 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform3ui_enc(self, location, v0, v1, v2);
}

void GL2Encoder::s_glUniform4ui(void* self, GLint location, GLint v0, GLuint v1, GLuint v2, GLuint v3) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 4 /* columns */, 1 /* rows */, location, 1 /* count */, ctx->getErrorPtr());
    ctx->m_glUniform4ui_enc(self, location, v0, v1, v2, v3);
}

void GL2Encoder::s_glUniform1uiv(void* self, GLint location, GLsizei count, const GLuint *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 1 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform1uiv_enc(self, location, count, value);
}

void GL2Encoder::s_glUniform2uiv(void* self, GLint location, GLsizei count, const GLuint *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 2 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform2uiv_enc(self, location, count, value);
}

void GL2Encoder::s_glUniform3uiv(void* self, GLint location, GLsizei count, const GLuint *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 3 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform3uiv_enc(self, location, count, value);
}

void GL2Encoder::s_glUniform4uiv(void* self, GLint location, GLsizei count, const GLuint *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(false /* is float? */, true /* is unsigned? */, 4 /* columns */, 1 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniform4uiv_enc(self, location, count, value);
}

void GL2Encoder::s_glUniformMatrix2x3fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 2 /* columns */, 3 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix2x3fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix3x2fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 3 /* columns */, 2 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix3x2fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix2x4fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 2 /* columns */, 4 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix2x4fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix4x2fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 4 /* columns */, 2 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix4x2fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix3x4fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 3 /* columns */, 4 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix3x4fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glUniformMatrix4x3fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->validateUniform(true /* is float? */, false /* is unsigned? */, 4 /* columns */, 3 /* rows */, location, count /* count */, ctx->getErrorPtr());
    ctx->m_glUniformMatrix4x3fv_enc(self, location, count, transpose, value);
}

void GL2Encoder::s_glGetUniformuiv(void* self, GLuint program, GLint location, GLuint* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_shared->isShaderOrProgramObject(program), GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->m_shared->isProgram(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->isProgramInitialized(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_shared->getProgramUniformType(program,location)==0, GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_shared->isProgramUniformLocationValid(program,location), GL_INVALID_OPERATION);
    ctx->m_glGetUniformuiv_enc(self, program, location, params);
}

void GL2Encoder::s_glGetActiveUniformBlockiv(void* self, GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(!GLESv2Validation::allowedGetActiveUniformBlock(pname), GL_INVALID_ENUM);
    SET_ERROR_IF(uniformBlockIndex >= ctx->m_shared->getActiveUniformBlockCount(program), GL_INVALID_VALUE);

    // refresh client state's # active uniforms in this block
    if (pname == GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES) {
        // TODO if worth it: cache uniform count and other params,
        // invalidate on program relinking.
        GLint numActiveUniforms;
        ctx->m_glGetActiveUniformBlockiv_enc(ctx,
                program, uniformBlockIndex,
                GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS,
                &numActiveUniforms);
        ctx->m_state->setNumActiveUniformsInUniformBlock(
                program, uniformBlockIndex, numActiveUniforms);
    }

    ctx->m_glGetActiveUniformBlockiv_enc(ctx,
            program, uniformBlockIndex,
            pname, params);
}

void GL2Encoder::s_glGetVertexAttribIiv(void* self, GLuint index, GLenum pname, GLint* params) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF(!GLESv2Validation::allowedGetVertexAttrib(pname), GL_INVALID_ENUM);

    if (!ctx->m_state->getVertexAttribParameter<GLint>(index, pname, params)) {
        ctx->m_glGetVertexAttribIiv_enc(self, index, pname, params);
    }
}

void GL2Encoder::s_glGetVertexAttribIuiv(void* self, GLuint index, GLenum pname, GLuint* params) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF(!GLESv2Validation::allowedGetVertexAttrib(pname), GL_INVALID_ENUM);

    if (!ctx->m_state->getVertexAttribParameter<GLuint>(index, pname, params)) {
        ctx->m_glGetVertexAttribIuiv_enc(self, index, pname, params);
    }
}

void GL2Encoder::s_glVertexAttribIPointer(void* self, GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid* pointer) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF((size < 1 || size > 4), GL_INVALID_VALUE);
    SET_ERROR_IF(
        !(type == GL_BYTE ||
          type == GL_UNSIGNED_BYTE ||
          type == GL_SHORT ||
          type == GL_UNSIGNED_SHORT ||
          type == GL_INT ||
          type == GL_UNSIGNED_INT),
        GL_INVALID_ENUM);
    SET_ERROR_IF(stride < 0, GL_INVALID_VALUE);

    ctx->m_state->setVertexAttribBinding(index, index);
    ctx->m_state->setVertexAttribFormat(index, size, type, false, 0, true);
    GLsizei effectiveStride = stride;
    if (stride == 0) {
        effectiveStride = glSizeof(type) * size;
    }
    ctx->m_state->bindIndexedBuffer(0, index, ctx->m_state->currentArrayVbo(), (uintptr_t)pointer, 0, stride, effectiveStride);

    if (ctx->m_state->currentArrayVbo() != 0) {
        ctx->glVertexAttribIPointerOffsetAEMU(ctx, index, size, type, stride, (uintptr_t)pointer);
    } else {
        SET_ERROR_IF(ctx->m_state->currentVertexArrayObject() != 0 && pointer, GL_INVALID_OPERATION);
        // wait for client-array handler
    }
}

void GL2Encoder::s_glVertexAttribDivisor(void* self, GLuint index, GLuint divisor) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_state->setVertexAttribBinding(index, index);
    ctx->m_state->setVertexBindingDivisor(index, divisor);
    ctx->m_glVertexAttribDivisor_enc(ctx, index, divisor);
}

void GL2Encoder::s_glRenderbufferStorageMultisample(void* self,
        GLenum target, GLsizei samples, GLenum internalformat,
        GLsizei width, GLsizei height) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_RENDERBUFFER, GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::rboFormat(ctx, internalformat), GL_INVALID_ENUM);

    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    GLint max_rb_size;
    ctx->glGetIntegerv(ctx, GL_MAX_RENDERBUFFER_SIZE, &max_rb_size);
    SET_ERROR_IF(width > max_rb_size || height > max_rb_size, GL_INVALID_VALUE);

    GLint max_samples;
    ctx->s_glGetInternalformativ(ctx, target, internalformat, GL_SAMPLES, 1, &max_samples);
    SET_ERROR_IF(samples > max_samples, GL_INVALID_OPERATION);

    state->setBoundRenderbufferFormat(internalformat);
    state->setBoundRenderbufferSamples(samples);
    state->setBoundRenderbufferDimensions(width, height);
    ctx->m_glRenderbufferStorageMultisample_enc(
            self, target, samples, internalformat, width, height);
}

void GL2Encoder::s_glDrawBuffers(void* self, GLsizei n, const GLenum* bufs) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) && n > 1, GL_INVALID_OPERATION);
    SET_ERROR_IF(n < 0 || n > ctx->m_state->getMaxDrawBuffers(), GL_INVALID_VALUE);
    for (int i = 0; i < n; i++) {
        SET_ERROR_IF(
            bufs[i] != GL_NONE &&
            bufs[i] != GL_BACK &&
            glUtilsColorAttachmentIndex(bufs[i]) == -1,
            GL_INVALID_ENUM);
        SET_ERROR_IF(
            !ctx->m_state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) &&
            glUtilsColorAttachmentIndex(bufs[i]) != -1,
            GL_INVALID_OPERATION);
        SET_ERROR_IF(
            ctx->m_state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) &&
            ((glUtilsColorAttachmentIndex(bufs[i]) != -1 &&
              glUtilsColorAttachmentIndex(bufs[i]) != i) ||
             (glUtilsColorAttachmentIndex(bufs[i]) == -1 &&
              bufs[i] != GL_NONE)),
            GL_INVALID_OPERATION);
    }

    ctx->m_glDrawBuffers_enc(ctx, n, bufs);
}

void GL2Encoder::s_glReadBuffer(void* self, GLenum src) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(
        glUtilsColorAttachmentIndex(src) != -1 &&
         (glUtilsColorAttachmentIndex(src) >=
         ctx->m_state->getMaxColorAttachments()),
        GL_INVALID_OPERATION);
    SET_ERROR_IF(
        src != GL_NONE &&
        src != GL_BACK &&
        src > GL_COLOR_ATTACHMENT0 &&
        src < GL_DEPTH_ATTACHMENT &&
        (src - GL_COLOR_ATTACHMENT0) >
        ctx->m_state->getMaxColorAttachments(),
        GL_INVALID_OPERATION);
    SET_ERROR_IF(
        src != GL_NONE &&
        src != GL_BACK &&
        glUtilsColorAttachmentIndex(src) == -1,
        GL_INVALID_ENUM);
    SET_ERROR_IF(
        !ctx->m_state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
        src != GL_NONE &&
        src != GL_BACK,
        GL_INVALID_OPERATION);
    SET_ERROR_IF(
        ctx->m_state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
        src != GL_NONE &&
        glUtilsColorAttachmentIndex(src) == -1,
        GL_INVALID_OPERATION);

    ctx->m_glReadBuffer_enc(ctx, src);
}

void GL2Encoder::s_glFramebufferTextureLayer(void* self, GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!GLESv2Validation::framebufferTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::framebufferAttachment(ctx, attachment), GL_INVALID_ENUM);
    SET_ERROR_IF(texture != 0 && layer < 0, GL_INVALID_VALUE);
    GLint maxArrayTextureLayers;
    ctx->glGetIntegerv(ctx, GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayTextureLayers);
    SET_ERROR_IF(texture != 0 && layer > maxArrayTextureLayers - 1, GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->m_state->boundFramebuffer(target), GL_INVALID_OPERATION);
    GLenum lastBoundTarget = state->queryTexLastBoundTarget(texture);
    SET_ERROR_IF(lastBoundTarget != GL_TEXTURE_2D_ARRAY &&
                 lastBoundTarget != GL_TEXTURE_3D,
                 GL_INVALID_OPERATION);
    state->attachTextureObject(target, attachment, texture, level, layer);

    GLint max3DTextureSize;
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max3DTextureSize);
    SET_ERROR_IF(
            layer >= max3DTextureSize,
            GL_INVALID_VALUE);

    ctx->m_glFramebufferTextureLayer_enc(self, target, attachment, texture, level, layer);
}

void GL2Encoder::s_glTexStorage2D(void* self, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(
        target != GL_TEXTURE_2D &&
        target != GL_TEXTURE_CUBE_MAP,
        GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelInternalFormat(internalformat), GL_INVALID_ENUM);
    SET_ERROR_IF(!state->getBoundTexture(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(levels < 1 || width < 1 || height < 1, GL_INVALID_VALUE);
    SET_ERROR_IF(levels > ilog2((uint32_t)std::max(width, height)) + 1,
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(state->isBoundTextureImmutableFormat(target), GL_INVALID_OPERATION);

    state->setBoundTextureInternalFormat(target, internalformat);
    state->setBoundTextureDims(target, -1 /* set all cube dimensions */, -1, width, height, 1);
    state->setBoundTextureImmutableFormat(target);

    if (target == GL_TEXTURE_2D) {
        ctx->override2DTextureTarget(target);
    }

    ctx->m_glTexStorage2D_enc(ctx, target, levels, internalformat, width, height);

    if (target == GL_TEXTURE_2D) {
        ctx->restore2DTextureTarget(target);
    }
}

void GL2Encoder::s_glTransformFeedbackVaryings(void* self, GLuint program, GLsizei count, const char** varyings, GLenum bufferMode) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!ctx->m_shared->isProgram(program), GL_INVALID_VALUE);

    GLint maxCount = 0;
    ctx->glGetIntegerv(ctx, GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &maxCount);

    SET_ERROR_IF(
        bufferMode == GL_SEPARATE_ATTRIBS &&
        maxCount < count,
        GL_INVALID_VALUE);
    SET_ERROR_IF(
        bufferMode != GL_INTERLEAVED_ATTRIBS &&
        bufferMode != GL_SEPARATE_ATTRIBS,
        GL_INVALID_ENUM);

    // NOTE: This only has an effect on the program that is being linked.
    // The dEQP test in dEQP-GLES3.functional.negative_api doesn't know
    // about this.
    ctx->m_state->setTransformFeedbackVaryingsCountForLinking(count);

    if (!count) return;

    GLint err = GL_NO_ERROR;
    std::string packed = packVarNames(count, varyings, &err);
    SET_ERROR_IF(err != GL_NO_ERROR, GL_INVALID_OPERATION);

    ctx->glTransformFeedbackVaryingsAEMU(ctx, program, count, (const char*)&packed[0], packed.size() + 1, bufferMode);
}

void GL2Encoder::s_glBeginTransformFeedback(void* self, GLenum primitiveMode) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(
        primitiveMode != GL_POINTS &&
        primitiveMode != GL_LINES &&
        primitiveMode != GL_TRIANGLES,
        GL_INVALID_ENUM);
    SET_ERROR_IF(
        ctx->m_state->getTransformFeedbackActive(),
        GL_INVALID_OPERATION);
    // TODO:
    // dEQP-GLES3.functional.lifetime.attach.deleted_output.buffer_transform_feedback
    // SET_ERROR_IF(
    //     !ctx->boundBuffer(GL_TRANSFORM_FEEDBACK_BUFFER),
    //     GL_INVALID_OPERATION);
    SET_ERROR_IF(
        !ctx->m_state->currentProgram(), GL_INVALID_OPERATION);
    ctx->m_glBeginTransformFeedback_enc(ctx, primitiveMode);
    state->setTransformFeedbackActive(true);
    state->setTransformFeedbackUnpaused(true);
}

void GL2Encoder::s_glEndTransformFeedback(void* self) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(!state->getTransformFeedbackActive(), GL_INVALID_OPERATION);
    ctx->m_glEndTransformFeedback_enc(ctx);
    state->setTransformFeedbackActive(false);
    state->setTransformFeedbackUnpaused(false);
}

void GL2Encoder::s_glPauseTransformFeedback(void* self) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(!state->getTransformFeedbackActive(), GL_INVALID_OPERATION);
    SET_ERROR_IF(!state->getTransformFeedbackUnpaused(), GL_INVALID_OPERATION);
    ctx->m_glPauseTransformFeedback_enc(ctx);
    state->setTransformFeedbackUnpaused(false);
}

void GL2Encoder::s_glResumeTransformFeedback(void* self) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(!state->getTransformFeedbackActive(), GL_INVALID_OPERATION);
    SET_ERROR_IF(state->getTransformFeedbackUnpaused(), GL_INVALID_OPERATION);
    ctx->m_glResumeTransformFeedback_enc(ctx);
    state->setTransformFeedbackUnpaused(true);
}

void GL2Encoder::s_glTexImage3D(void* self, GLenum target, GLint level, GLint internalFormat,
                               GLsizei width, GLsizei height, GLsizei depth,
                               GLint border, GLenum format, GLenum type, const GLvoid* data) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_TEXTURE_3D &&
                 target != GL_TEXTURE_2D_ARRAY,
                 GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelType(ctx, type), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelFormat(ctx, format), GL_INVALID_ENUM);
    SET_ERROR_IF(!(GLESv2Validation::pixelOp(format,type)),GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::pixelSizedFormat(ctx, internalFormat, format, type), GL_INVALID_OPERATION);
    SET_ERROR_IF(target == GL_TEXTURE_3D &&
        ((format == GL_DEPTH_COMPONENT) ||
         (format == GL_DEPTH_STENCIL)), GL_INVALID_OPERATION);

    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);

    GLint max_texture_size;
    GLint max_3d_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max_3d_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_3d_texture_size), GL_INVALID_VALUE);

    SET_ERROR_IF(width < 0 || height < 0 || depth < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    if (target == GL_TEXTURE_3D) {
        SET_ERROR_IF(depth > max_texture_size, GL_INVALID_VALUE);
    } else {
        GLint maxArrayTextureLayers;
        ctx->glGetIntegerv(ctx, GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayTextureLayers);
        SET_ERROR_IF(depth > maxArrayTextureLayers, GL_INVALID_VALUE);
    }
    SET_ERROR_IF(width > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(depth > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(border != 0, GL_INVALID_VALUE);
    // If unpack buffer is nonzero, verify buffer data fits and is evenly divisible by the type.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 ((uintptr_t)data + ctx->m_state->pboNeededDataSize(width, height, depth, format, type, 0) >
                  ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 ((uintptr_t)data %
                  glSizeof(type)),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(state->isBoundTextureImmutableFormat(target), GL_INVALID_OPERATION);

    state->setBoundTextureInternalFormat(target, internalFormat);
    state->setBoundTextureFormat(target, format);
    state->setBoundTextureType(target, type);
    state->setBoundTextureDims(target, target, level, width, height, depth);

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glTexImage3DOffsetAEMU(
                ctx, target, level, internalFormat,
                width, height, depth,
                border, format, type, (uintptr_t)data);
    } else {
        ctx->m_glTexImage3D_enc(ctx,
                target, level, internalFormat,
                width, height, depth,
                border, format, type, data);
    }
}

void GL2Encoder::s_glTexSubImage3D(void* self, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* data) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_TEXTURE_3D &&
                 target != GL_TEXTURE_2D_ARRAY,
                 GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelType(ctx, type), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelFormat(ctx, format), GL_INVALID_ENUM);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);
    GLint max_texture_size;
    GLint max_3d_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max_3d_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_3d_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0 || depth < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0 || zoffset < 0, GL_INVALID_VALUE);
    GLuint tex = state->getBoundTexture(target);
    GLsizei neededWidth = xoffset + width;
    GLsizei neededHeight = yoffset + height;
    GLsizei neededDepth = zoffset + depth;

    SET_ERROR_IF(tex &&
                 (neededWidth > state->queryTexWidth(level, tex) ||
                  neededHeight > state->queryTexHeight(level, tex) ||
                  neededDepth > state->queryTexDepth(level, tex)),
                 GL_INVALID_VALUE);
    // If unpack buffer is nonzero, verify buffer data fits and is evenly divisible by the type.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 ((uintptr_t)data + ctx->m_state->pboNeededDataSize(width, height, depth, format, type, 0) >
                  ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 ((uintptr_t)data % glSizeof(type)),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) && !data, GL_INVALID_OPERATION);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0 || zoffset < 0, GL_INVALID_VALUE);

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glTexSubImage3DOffsetAEMU(ctx,
                target, level,
                xoffset, yoffset, zoffset,
                width, height, depth,
                format, type, (uintptr_t)data);
    } else {
        ctx->m_glTexSubImage3D_enc(ctx,
                target, level,
                xoffset, yoffset, zoffset,
                width, height, depth,
                format, type, data);
    }
}

void GL2Encoder::s_glCompressedTexImage3D(void* self, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid* data) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_TEXTURE_3D &&
                 target != GL_TEXTURE_2D_ARRAY,
                 GL_INVALID_ENUM);
    // Filter compressed formats support.
    SET_ERROR_IF(!GLESv2Validation::supportedCompressedFormat(ctx, internalformat), GL_INVALID_ENUM);
    SET_ERROR_IF(target == GL_TEXTURE_CUBE_MAP, GL_INVALID_ENUM);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);
    SET_ERROR_IF(width < 0 || height < 0 || depth < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(border, GL_INVALID_VALUE);

    GLint max_texture_size;
    GLint max_3d_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max_3d_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_3d_texture_size), GL_INVALID_VALUE);

    SET_ERROR_IF(width < 0 || height < 0 || depth < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    if (target == GL_TEXTURE_3D) {
        SET_ERROR_IF(depth > max_texture_size, GL_INVALID_VALUE);
    } else {
        GLint maxArrayTextureLayers;
        ctx->glGetIntegerv(ctx, GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayTextureLayers);
        SET_ERROR_IF(depth > maxArrayTextureLayers, GL_INVALID_VALUE);
    }
    SET_ERROR_IF(width > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(depth > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESTextureUtils::isAstcFormat(internalformat) && GL_TEXTURE_3D == target, GL_INVALID_OPERATION);

    // If unpack buffer is nonzero, verify buffer data fits.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (imageSize > ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_state->compressedTexImageSizeCompatible(internalformat, width, height, depth, imageSize), GL_INVALID_VALUE);
    state->setBoundTextureInternalFormat(target, (GLint)internalformat);
    state->setBoundTextureDims(target, target, level, width, height, depth);

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glCompressedTexImage3DOffsetAEMU(
                ctx, target, level, internalformat,
                width, height, depth, border,
                imageSize, (uintptr_t)data);
    } else {
        ctx->m_glCompressedTexImage3D_enc(
                ctx, target, level, internalformat,
                width, height, depth, border,
                imageSize, data);
    }
}

void GL2Encoder::s_glCompressedTexSubImage3D(void* self, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid* data) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(target == GL_TEXTURE_CUBE_MAP, GL_INVALID_ENUM);
    // If unpack buffer is nonzero, verify unmapped state.
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_UNPACK_BUFFER), GL_INVALID_OPERATION);
    SET_ERROR_IF(width < 0 || height < 0 || depth < 0, GL_INVALID_VALUE);
    // If unpack buffer is nonzero, verify buffer data fits.
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER) &&
                 (imageSize > ctx->getBufferData(GL_PIXEL_UNPACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0 || zoffset < 0, GL_INVALID_VALUE);

    GLint max_texture_size;
    GLint max_3d_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max_3d_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_3d_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0 || depth < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0 || zoffset < 0, GL_INVALID_VALUE);
    GLenum stateTarget = target;
    if (target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        stateTarget = GL_TEXTURE_CUBE_MAP;

    GLuint tex = ctx->m_state->getBoundTexture(stateTarget);
    GLsizei neededWidth = xoffset + width;
    GLsizei neededHeight = yoffset + height;
    GLsizei neededDepth = zoffset + depth;

    SET_ERROR_IF(tex &&
                 (neededWidth > ctx->m_state->queryTexWidth(level, tex) ||
                  neededHeight > ctx->m_state->queryTexHeight(level, tex) ||
                  neededDepth > ctx->m_state->queryTexDepth(level, tex)),
                 GL_INVALID_VALUE);

    GLint internalFormat = ctx->m_state->queryTexInternalFormat(tex);
    SET_ERROR_IF(internalFormat != format, GL_INVALID_OPERATION);

    GLint totalWidth = ctx->m_state->queryTexWidth(level, tex);
    GLint totalHeight = ctx->m_state->queryTexHeight(level, tex);

    if (GLESTextureUtils::isEtc2Format(internalFormat)) {
        SET_ERROR_IF((width % 4) && (totalWidth != xoffset + width), GL_INVALID_OPERATION);
        SET_ERROR_IF((height % 4) && (totalHeight != yoffset + height), GL_INVALID_OPERATION);
        SET_ERROR_IF((xoffset % 4) || (yoffset % 4), GL_INVALID_OPERATION);
    }

    SET_ERROR_IF(totalWidth < xoffset + width, GL_INVALID_VALUE);
    SET_ERROR_IF(totalHeight < yoffset + height, GL_INVALID_VALUE);

    SET_ERROR_IF(!ctx->m_state->compressedTexImageSizeCompatible(internalFormat, width, height, depth, imageSize), GL_INVALID_VALUE);

    if (ctx->boundBuffer(GL_PIXEL_UNPACK_BUFFER)) {
        ctx->glCompressedTexSubImage3DOffsetAEMU(
                ctx, target, level,
                xoffset, yoffset, zoffset,
                width, height, depth,
                format, imageSize, (uintptr_t)data);
    } else {
        ctx->m_glCompressedTexSubImage3D_enc(
                ctx, target, level,
                xoffset, yoffset, zoffset,
                width, height, depth,
                format, imageSize, data);

    }
}

void GL2Encoder::s_glTexStorage3D(void* self, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(target != GL_TEXTURE_3D &&
                 target != GL_TEXTURE_2D_ARRAY,
                 GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelInternalFormat(internalformat), GL_INVALID_ENUM);
    SET_ERROR_IF(!state->getBoundTexture(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(levels < 1 || width < 1 || height < 1 || depth < 1, GL_INVALID_VALUE);
    GLint max_texture_size;
    GLint max_3d_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max_3d_texture_size);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    if (target == GL_TEXTURE_3D) {
        SET_ERROR_IF(depth > max_texture_size, GL_INVALID_VALUE);
    } else {
        GLint maxArrayTextureLayers;
        ctx->glGetIntegerv(ctx, GL_MAX_ARRAY_TEXTURE_LAYERS, &maxArrayTextureLayers);
        SET_ERROR_IF(depth > maxArrayTextureLayers, GL_INVALID_VALUE);
    }

    SET_ERROR_IF(width > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_3d_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(depth > max_3d_texture_size, GL_INVALID_VALUE);

    SET_ERROR_IF(GLESTextureUtils::isAstcFormat(internalformat) && GL_TEXTURE_3D == target, GL_INVALID_OPERATION);

    SET_ERROR_IF(target == GL_TEXTURE_3D && (levels > ilog2((uint32_t)std::max(width, std::max(height, depth))) + 1),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(target == GL_TEXTURE_2D_ARRAY && (levels > ilog2((uint32_t)std::max(width, height)) + 1),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(state->isBoundTextureImmutableFormat(target), GL_INVALID_OPERATION);

    state->setBoundTextureInternalFormat(target, internalformat);
    state->setBoundTextureDims(target, target, -1, width, height, depth);
    state->setBoundTextureImmutableFormat(target);
    ctx->m_glTexStorage3D_enc(ctx, target, levels, internalformat, width, height, depth);
    state->setBoundTextureImmutableFormat(target);
}

void GL2Encoder::s_glDrawArraysInstanced(void* self, GLenum mode, GLint first, GLsizei count, GLsizei primcount) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(primcount < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    ctx->getVBOUsage(&has_client_vertex_arrays,
                     &has_indirect_arrays);

    if (has_client_vertex_arrays ||
        (!has_client_vertex_arrays &&
         !has_indirect_arrays)) {
        ctx->sendVertexAttributes(first, count, true, primcount);
        ctx->m_glDrawArraysInstanced_enc(ctx, mode, 0, count, primcount);
    } else {
        ctx->sendVertexAttributes(0, count, false, primcount);
        ctx->m_glDrawArraysInstanced_enc(ctx, mode, first, count, primcount);
    }
    ctx->m_stream->flush();
    ctx->m_state->postDraw();
}

void GL2Encoder::s_glDrawElementsInstanced(void* self, GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount)
{

    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(primcount < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!(type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT), GL_INVALID_ENUM);
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    GLintptr offset = 0;

    ctx->getVBOUsage(&has_client_vertex_arrays, &has_indirect_arrays);

    if (!has_client_vertex_arrays && !has_indirect_arrays) {
        // GFXSTREAM_WARNING("glDrawElements: no vertex arrays / buffers bound to the command\n");
        GLenum status = ctx->glCheckFramebufferStatus(self, GL_FRAMEBUFFER);
        SET_ERROR_IF(status != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);
    }

    BufferData* buf = NULL;
    int minIndex = 0, maxIndex = 0;

    // For validation/immediate index array purposes,
    // we need the min/max vertex index of the index array.
    // If the VBO != 0, this may not be the first time we have
    // used this particular index buffer. getBufferIndexRange
    // can more quickly get min/max vertex index by
    // caching previous results.
    if (ctx->m_state->currentIndexVbo() != 0) {
        buf = ctx->m_shared->getBufferData(ctx->m_state->currentIndexVbo());
        offset = (GLintptr)indices;
        indices = &buf->m_fixedBuffer[offset];
        ctx->getBufferIndexRange(buf,
                                 indices,
                                 type,
                                 (size_t)count,
                                 (size_t)offset,
                                 &minIndex, &maxIndex);
    } else {
        // In this case, the |indices| field holds a real
        // array, so calculate the indices now. They will
        // also be needed to know how much data to
        // transfer to host.
        ctx->calcIndexRange(indices,
                            type,
                            count,
                            &minIndex,
                            &maxIndex);
    }

    if (count == 0) return;

    bool adjustIndices = true;
    if (ctx->m_state->currentIndexVbo() != 0) {
        if (!has_client_vertex_arrays) {
            ctx->sendVertexAttributes(0, maxIndex + 1, false, primcount);
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, ctx->m_state->currentIndexVbo());
            ctx->glDrawElementsInstancedOffsetAEMU(ctx, mode, count, type, offset, primcount);
            ctx->flushDrawCall();
            adjustIndices = false;
        } else {
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    }
    if (adjustIndices) {
        void *adjustedIndices =
            ctx->recenterIndices(indices,
                                 type,
                                 count,
                                 minIndex);

        if (has_indirect_arrays || 1) {
            ctx->sendVertexAttributes(minIndex, maxIndex - minIndex + 1, true, primcount);
            ctx->glDrawElementsInstancedDataAEMU(ctx, mode, count, type, adjustedIndices, primcount, count * glSizeof(type));
            ctx->m_stream->flush();
            // XXX - OPTIMIZATION (see the other else branch) should be implemented
            if(!has_indirect_arrays) {
                //GFXSTREAM_DEBUG("unoptimized drawelements !!!\n");
            }
        } else {
            // we are all direct arrays and immidate mode index array -
            // rebuild the arrays and the index array;
            GFXSTREAM_ERROR("Direct index & direct buffer data - will be implemented in later versions.");
        }
    }
    ctx->m_state->postDraw();
}

void GL2Encoder::s_glDrawRangeElements(void* self, GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices)
{

    GL2Encoder *ctx = (GL2Encoder *)self;
    assert(ctx->m_state != NULL);
    SET_ERROR_IF(!isValidDrawMode(mode), GL_INVALID_ENUM);
    SET_ERROR_IF(end < start, GL_INVALID_VALUE);
    SET_ERROR_IF(count < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!(type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_SHORT || type == GL_UNSIGNED_INT), GL_INVALID_ENUM);
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    bool has_client_vertex_arrays = false;
    bool has_indirect_arrays = false;
    GLintptr offset = 0;

    ctx->getVBOUsage(&has_client_vertex_arrays, &has_indirect_arrays);

    if (!has_client_vertex_arrays && !has_indirect_arrays) {
        // GFXSTREAM_WARNING("No vertex arrays / buffers bound to the command");
        GLenum status = ctx->glCheckFramebufferStatus(self, GL_FRAMEBUFFER);
        SET_ERROR_IF(status != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);
    }

    BufferData* buf = NULL;
    int minIndex = 0, maxIndex = 0;

    // For validation/immediate index array purposes,
    // we need the min/max vertex index of the index array.
    // If the VBO != 0, this may not be the first time we have
    // used this particular index buffer. getBufferIndexRange
    // can more quickly get min/max vertex index by
    // caching previous results.
    if (ctx->m_state->currentIndexVbo() != 0) {
        buf = ctx->m_shared->getBufferData(ctx->m_state->currentIndexVbo());
        GFXSTREAM_VERBOSE("Current index vbo: %p len %zu count %zu.", buf, buf->m_fixedBuffer.size(), (size_t)count);
        offset = (GLintptr)indices;
        void* oldIndices = (void*)indices;
        indices = &buf->m_fixedBuffer[offset];
        GFXSTREAM_VERBOSE("%s: indices arg: %p buffer start: %p indices: %p.",
                          (void*)(uintptr_t)(oldIndices),
                          buf->m_fixedBuffer.data(),
                          indices);
        ctx->getBufferIndexRange(buf,
                                 indices,
                                 type,
                                 (size_t)count,
                                 (size_t)offset,
                                 &minIndex, &maxIndex);
    } else {
        // In this case, the |indices| field holds a real
        // array, so calculate the indices now. They will
        // also be needed to know how much data to
        // transfer to host.
        ctx->calcIndexRange(indices,
                            type,
                            count,
                            &minIndex,
                            &maxIndex);
    }

    if (count == 0) return;

    bool adjustIndices = true;
    if (ctx->m_state->currentIndexVbo() != 0) {
        if (!has_client_vertex_arrays) {
            ctx->sendVertexAttributes(0, maxIndex + 1, false);
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, ctx->m_state->currentIndexVbo());
            ctx->glDrawElementsOffset(ctx, mode, count, type, offset);
            ctx->flushDrawCall();
            adjustIndices = false;
        } else {
            ctx->doBindBufferEncodeCached(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
    }
    if (adjustIndices) {
        void *adjustedIndices =
            ctx->recenterIndices(indices,
                                 type,
                                 count,
                                 minIndex);

        if (has_indirect_arrays || 1) {
            ctx->sendVertexAttributes(minIndex, maxIndex - minIndex + 1, true);
            ctx->glDrawElementsData(ctx, mode, count, type, adjustedIndices, count * glSizeof(type));
            ctx->m_stream->flush();
            // XXX - OPTIMIZATION (see the other else branch) should be implemented
            if(!has_indirect_arrays) {
                //GFXSTREAM_WARNING("unoptimized drawelements !!!");
            }
        } else {
            // we are all direct arrays and immidate mode index array -
            // rebuild the arrays and the index array;
            GFXSTREAM_ERROR("Direct index & direct buffer data - will be implemented in later versions.");
        }
    }
    ctx->m_state->postDraw();
}

const GLubyte* GL2Encoder::s_glGetStringi(void* self, GLenum name, GLuint index) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    const GLubyte *retval =  (GLubyte *) "";

    RET_AND_SET_ERROR_IF(
        name != GL_VENDOR &&
        name != GL_RENDERER &&
        name != GL_VERSION &&
        name != GL_EXTENSIONS,
        GL_INVALID_ENUM,
        retval);

    RET_AND_SET_ERROR_IF(
        (name == GL_VENDOR ||
         name == GL_RENDERER ||
         name == GL_VERSION) &&
        index != 0,
        GL_INVALID_VALUE,
        retval);

    RET_AND_SET_ERROR_IF(
        name == GL_EXTENSIONS &&
        index >= ctx->m_currExtensionsArray.size(),
        GL_INVALID_VALUE,
        retval);

    switch (name) {
    case GL_VENDOR:
        retval = gVendorString;
        break;
    case GL_RENDERER:
        retval = gRendererString;
        break;
    case GL_VERSION:
        retval = gVersionString;
        break;
    case GL_EXTENSIONS:
        retval = (const GLubyte*)(ctx->m_currExtensionsArray[index].c_str());
        break;
    }

    return retval;
}

std::optional<ProgramBinaryInfo> GL2Encoder::getProgramBinary(GLuint program) {
    GL2Encoder* ctx = this;

    VALIDATE_PROGRAM_NAME_RET(program, std::nullopt);

    GLint linkStatus = 0;
    ctx->m_glGetProgramiv_enc(ctx, program, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus) return std::nullopt;

    ProgramBinaryInfo info;

    {
        auto* guestProgramInfo = info.mutable_guest_program_info();

        // External sampler uniforms can not be reconstructed from the host program info
        // because the host only sees the modified shader where each `samplerExternalOES`
        // was rewritten to `sampler2D`.
        std::vector<GLuint> externalSamplerUnifomIndices;
        if (!ctx->m_shared->getExternalSamplerUniformIndices(program,
                                                             &externalSamplerUnifomIndices)) {
            return std::nullopt;
        }
        for (GLuint index : externalSamplerUnifomIndices) {
            guestProgramInfo->add_external_sampler_uniform_indices(index);
        }
    }

    {
        auto* hostProgramInfo = info.mutable_host_program_info();

        GLint hostProgramBinaryLength = 0;
        ctx->m_glGetProgramiv_enc(ctx, program, GL_PROGRAM_BINARY_LENGTH, &hostProgramBinaryLength);

        std::string hostProgramBinary;
        hostProgramBinary.resize(hostProgramBinaryLength, 'x');

        GLenum hostProgramBinaryFormat = 0;
        ctx->m_glGetProgramBinary_enc(ctx, program, hostProgramBinary.size(), nullptr,
                                      &hostProgramBinaryFormat, hostProgramBinary.data());

        hostProgramInfo->set_binary_format(static_cast<uint64_t>(hostProgramBinaryFormat));
        hostProgramInfo->set_binary(hostProgramBinary);
    }

    return info;
}

void GL2Encoder::getProgramBinaryLength(GLuint program, GLint* outLength) {
    GL2Encoder* ctx = (GL2Encoder*)this;

    VALIDATE_PROGRAM_NAME(program);

    auto programBinaryInfoOpt = ctx->getProgramBinary(program);
    SET_ERROR_IF(!programBinaryInfoOpt.has_value(), GL_INVALID_OPERATION);
    auto& programBinaryInfo = *programBinaryInfoOpt;

    std::string programBinaryInfoBytes;
    SET_ERROR_IF(!programBinaryInfo.SerializeToString(&programBinaryInfoBytes),
                 GL_INVALID_OPERATION);

    *outLength = static_cast<GLint>(programBinaryInfoBytes.size());
}

#define GL_PROGRAM_BINARY_FORMAT_GFXSTREAM_PROGRAM_BINARY_INFO_V1 0x0001

void GL2Encoder::s_glGetProgramBinary(void* self, GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    VALIDATE_PROGRAM_NAME(program);

    auto programBinaryInfoOpt = ctx->getProgramBinary(program);
    SET_ERROR_IF(!programBinaryInfoOpt.has_value(), GL_INVALID_OPERATION);
    auto& programBinaryInfo = *programBinaryInfoOpt;

    std::string programBinaryInfoBytes;
    SET_ERROR_IF(!programBinaryInfo.SerializeToString(&programBinaryInfoBytes),
                 GL_INVALID_OPERATION);

    SET_ERROR_IF(bufSize < programBinaryInfoBytes.size(), GL_INVALID_OPERATION);

    if (length) {
        *length = static_cast<GLsizei>(programBinaryInfoBytes.size());
    }
    *binaryFormat = GL_PROGRAM_BINARY_FORMAT_GFXSTREAM_PROGRAM_BINARY_INFO_V1;
    std::memcpy(binary, programBinaryInfoBytes.data(), programBinaryInfoBytes.size());
}

void GL2Encoder::s_glProgramBinary(void* self, GLuint program, GLenum binaryFormat,
                                   const void* binary, GLsizei length) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);

    SET_ERROR_IF(binaryFormat != GL_PROGRAM_BINARY_FORMAT_GFXSTREAM_PROGRAM_BINARY_INFO_V1,
                 GL_INVALID_ENUM);

    std::string programBinaryInfoBytes(reinterpret_cast<const char*>(binary), length);

    ProgramBinaryInfo programBinaryInfo;
    if (!programBinaryInfo.ParseFromString(programBinaryInfoBytes)) {
        ctx->m_shared->setProgramLinkStatus(program, GL_FALSE);
        return;
    }

    {
        const auto& hostProgramInfo = programBinaryInfo.host_program_info();

        const auto hostProgramBinaryFormat = static_cast<GLenum>(hostProgramInfo.binary_format());
        const auto& hostProgramBinary = hostProgramInfo.binary();
        ctx->m_glProgramBinary_enc(self, program, hostProgramBinaryFormat,
                                   hostProgramBinary.c_str(), hostProgramBinary.size());

        ctx->updateProgramInfoAfterLink(program);
    }

    {
        const auto& guestProgramInfo = programBinaryInfo.guest_program_info();

        for (uint64_t index : guestProgramInfo.external_sampler_uniform_indices()) {
            ctx->m_shared->setProgramIndexFlag(program, index,
                                               ProgramData::INDEX_FLAG_SAMPLER_EXTERNAL);
        }
    }
}

void GL2Encoder::s_glReadPixels(void* self, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* pixels) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(!GLESv2Validation::readPixelsFormat(format), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::readPixelsType(type), GL_INVALID_ENUM);
    SET_ERROR_IF(!(GLESv2Validation::pixelOp(format,type)),GL_INVALID_OPERATION);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(ctx->isBufferTargetMapped(GL_PIXEL_PACK_BUFFER), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->boundBuffer(GL_PIXEL_PACK_BUFFER) &&
                 ctx->getBufferData(GL_PIXEL_PACK_BUFFER) &&
                 (ctx->m_state->pboNeededDataSize(width, height, 1, format, type, 1) >
                  ctx->getBufferData(GL_PIXEL_PACK_BUFFER)->m_size),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->s_glCheckFramebufferStatus(ctx, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    // now is complete
    // GL_INVALID_OPERATION is generated if GL_READ_FRAMEBUFFER_BINDING is nonzero, the read fbo is complete, and the value of
    // GL_SAMPLE_BUFFERS for the read framebuffer is greater than zero
    if (ctx->m_state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
        ctx->s_glCheckFramebufferStatus(ctx, GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        FboFormatInfo resInfo;
        ctx->m_state->getBoundFramebufferFormat(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &resInfo);
        if (resInfo.type == FBO_ATTACHMENT_RENDERBUFFER) {
            SET_ERROR_IF(resInfo.rb_multisamples > 0, GL_INVALID_OPERATION);
        }
        if (resInfo.type == FBO_ATTACHMENT_TEXTURE) {
            SET_ERROR_IF(resInfo.tex_multisamples > 0, GL_INVALID_OPERATION);
        }
    }


    /*
GL_INVALID_OPERATION is generated if the readbuffer of the currently bound framebuffer is a fixed point normalized surface and format and type are neither GL_RGBA and GL_UNSIGNED_BYTE, respectively, nor the format/type pair returned by querying GL_IMPLEMENTATION_COLOR_READ_FORMAT and GL_IMPLEMENTATION_COLOR_READ_TYPE.

GL_INVALID_OPERATION is generated if the readbuffer of the currently bound framebuffer is a floating point surface and format and type are neither GL_RGBA and GL_FLOAT, respectively, nor the format/type pair returned by querying GL_IMPLEMENTATION_COLOR_READ_FORMAT and GL_IMPLEMENTATION_COLOR_READ_TYPE.

GL_INVALID_OPERATION is generated if the readbuffer of the currently bound framebuffer is a signed integer surface and format and type are neither GL_RGBA_INTEGER and GL_INT, respectively, nor the format/type pair returned by querying GL_IMPLEMENTATION_COLOR_READ_FORMAT and GL_IMPLEMENTATION_COLOR_READ_TYPE.

GL_INVALID_OPERATION is generated if the readbuffer of the currently bound framebuffer is an unsigned integer surface and format and type are neither GL_RGBA_INTEGER and GL_UNSIGNED_INT, respectively, nor the format/type pair returned by querying GL_IMPLEMENTATION_COLOR_READ_FORMAT and GL_IMPLEMENTATION_COLOR_READ_TYPE.
*/

    FboFormatInfo fbo_format_info;
    ctx->m_state->getBoundFramebufferFormat(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &fbo_format_info);
    SET_ERROR_IF(
        fbo_format_info.type == FBO_ATTACHMENT_TEXTURE &&
        !GLESv2Validation::readPixelsFboFormatMatch(
            format, type, fbo_format_info.tex_type),
        GL_INVALID_OPERATION);

    if (ctx->boundBuffer(GL_PIXEL_PACK_BUFFER)) {
        ctx->glReadPixelsOffsetAEMU(
                ctx, x, y, width, height,
                format, type, (uintptr_t)pixels);
    } else {
        ctx->m_glReadPixels_enc(
                ctx, x, y, width, height,
                format, type, pixels);
    }
    ctx->m_state->postReadPixels();
}

// Track enabled state for some things like:
// - Primitive restart
void GL2Encoder::s_glEnable(void* self, GLenum what) {
    GL2Encoder *ctx = (GL2Encoder *)self;

	SET_ERROR_IF(!GLESv2Validation::allowedEnable(ctx->majorVersion(), ctx->minorVersion(), what), GL_INVALID_ENUM);
    if (!ctx->m_state) return;

    switch (what) {
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
        ctx->m_primitiveRestartEnabled = true;
        break;
    case GL_STENCIL_TEST:
        ctx->m_state->state_GL_STENCIL_TEST = true;
        break;
    }

    ctx->m_glEnable_enc(ctx, what);
}

void GL2Encoder::s_glDisable(void* self, GLenum what) {
    GL2Encoder *ctx = (GL2Encoder *)self;

	SET_ERROR_IF(!GLESv2Validation::allowedEnable(ctx->majorVersion(), ctx->minorVersion(), what), GL_INVALID_ENUM);
    if (!ctx->m_state) return;

    switch (what) {
    case GL_PRIMITIVE_RESTART_FIXED_INDEX:
        ctx->m_primitiveRestartEnabled = false;
        break;
    case GL_STENCIL_TEST:
        ctx->m_state->state_GL_STENCIL_TEST = false;
        break;
    }

    ctx->m_glDisable_enc(ctx, what);
}

void GL2Encoder::s_glClearBufferiv(void* self, GLenum buffer, GLint drawBuffer, const GLint * value) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(buffer != GL_COLOR && buffer != GL_STENCIL, GL_INVALID_ENUM);

    GLint maxDrawBuffers;
    ctx->glGetIntegerv(ctx, GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);

    SET_ERROR_IF(!value, GL_INVALID_VALUE);

    if (buffer == GL_COLOR) {
        SET_ERROR_IF(drawBuffer < 0 || drawBuffer>= maxDrawBuffers, GL_INVALID_VALUE);
    } else {
        SET_ERROR_IF(drawBuffer != 0, GL_INVALID_VALUE);
    }

    ctx->m_glClearBufferiv_enc(ctx, buffer, drawBuffer, value);
}

void GL2Encoder::s_glClearBufferuiv(void* self, GLenum buffer, GLint drawBuffer, const GLuint * value) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(buffer != GL_COLOR, GL_INVALID_ENUM);
    SET_ERROR_IF(!value, GL_INVALID_VALUE);

    GLint maxDrawBuffers;
    ctx->glGetIntegerv(ctx, GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    SET_ERROR_IF(drawBuffer < 0 || drawBuffer>= maxDrawBuffers, GL_INVALID_VALUE);

    ctx->m_glClearBufferuiv_enc(ctx, buffer, drawBuffer, value);
}

void GL2Encoder::s_glClearBufferfv(void* self, GLenum buffer, GLint drawBuffer, const GLfloat * value) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(buffer != GL_COLOR && buffer != GL_DEPTH, GL_INVALID_ENUM);

    SET_ERROR_IF(!value, GL_INVALID_VALUE);

    GLint maxDrawBuffers;
    ctx->glGetIntegerv(ctx, GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);

    if (buffer == GL_COLOR) {
        SET_ERROR_IF(drawBuffer < 0 || drawBuffer>= maxDrawBuffers, GL_INVALID_VALUE);
    } else {
        SET_ERROR_IF(drawBuffer != 0, GL_INVALID_VALUE);
    }

    ctx->m_glClearBufferfv_enc(ctx, buffer, drawBuffer, value);
}

void GL2Encoder::s_glClearBufferfi(void* self, GLenum buffer, GLint drawBuffer, float depth, int stencil) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(buffer != GL_DEPTH_STENCIL, GL_INVALID_ENUM);
    SET_ERROR_IF(drawBuffer != 0, GL_INVALID_VALUE);

    ctx->m_glClearBufferfi_enc(ctx, buffer, drawBuffer, depth, stencil);
}

void GL2Encoder::s_glBlitFramebuffer(void* self, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    bool validateColor = mask & GL_COLOR_BUFFER_BIT;
    bool validateDepth = mask & GL_DEPTH_BUFFER_BIT;
    bool validateStencil = mask & GL_STENCIL_BUFFER_BIT;
    bool validateDepthOrStencil = validateDepth || validateStencil;

    FboFormatInfo read_fbo_format_info;
    FboFormatInfo draw_fbo_format_info;
    if (validateColor) {
        state->getBoundFramebufferFormat(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &read_fbo_format_info);
        state->getBoundFramebufferFormat(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &draw_fbo_format_info);

        if (read_fbo_format_info.type == FBO_ATTACHMENT_TEXTURE) {
            SET_ERROR_IF(
                GL_LINEAR == filter &&
                GLESv2Validation::isIntegerFormat(read_fbo_format_info.tex_format),
                    GL_INVALID_OPERATION);
        }

        if (read_fbo_format_info.type == FBO_ATTACHMENT_TEXTURE &&
            draw_fbo_format_info.type == FBO_ATTACHMENT_TEXTURE) {
            SET_ERROR_IF(
                    state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
                    state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) &&
                    !GLESv2Validation::blitFramebufferFormat(
                        read_fbo_format_info.tex_type,
                        draw_fbo_format_info.tex_type),
                    GL_INVALID_OPERATION);
        }
    }

    if (validateDepth) {
        state->getBoundFramebufferFormat(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, &read_fbo_format_info);
        state->getBoundFramebufferFormat(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, &draw_fbo_format_info);

        if (read_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            draw_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER) {
            SET_ERROR_IF(
                    state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
                    state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) &&
                    !GLESv2Validation::blitFramebufferFormat(
                        read_fbo_format_info.rb_format,
                        draw_fbo_format_info.rb_format),
                    GL_INVALID_OPERATION);
        }
    }

    if (validateStencil) {
        state->getBoundFramebufferFormat(GL_READ_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, &read_fbo_format_info);
        state->getBoundFramebufferFormat(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, &draw_fbo_format_info);

        if (read_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            draw_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER) {
            SET_ERROR_IF(
                    state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
                    state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) &&
                    !GLESv2Validation::blitFramebufferFormat(
                        read_fbo_format_info.rb_format,
                        draw_fbo_format_info.rb_format),
                    GL_INVALID_OPERATION);
        }
    }

    if (validateDepthOrStencil) {
        SET_ERROR_IF(filter != GL_NEAREST, GL_INVALID_OPERATION);
    }

    state->getBoundFramebufferFormat(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &draw_fbo_format_info);
    SET_ERROR_IF(
            draw_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            draw_fbo_format_info.rb_multisamples > 0,
            GL_INVALID_OPERATION);
    SET_ERROR_IF(
            draw_fbo_format_info.type == FBO_ATTACHMENT_TEXTURE &&
            draw_fbo_format_info.tex_multisamples > 0,
            GL_INVALID_OPERATION);

    state->getBoundFramebufferFormat(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, &read_fbo_format_info);
    SET_ERROR_IF(
            read_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            read_fbo_format_info.rb_multisamples > 0 &&
            draw_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            state->boundFramebuffer(GL_READ_FRAMEBUFFER) &&
            state->boundFramebuffer(GL_DRAW_FRAMEBUFFER) &&
            (read_fbo_format_info.rb_format !=
             draw_fbo_format_info.rb_format),
            GL_INVALID_OPERATION);
    SET_ERROR_IF(
            read_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            read_fbo_format_info.rb_multisamples > 0 &&
            draw_fbo_format_info.type == FBO_ATTACHMENT_RENDERBUFFER &&
            (srcX0 != dstX0 || srcY0 != dstY0 ||
             srcX1 != dstX1 || srcY1 != dstY1),
            GL_INVALID_OPERATION);

	ctx->m_glBlitFramebuffer_enc(ctx,
            srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1,
            mask, filter);
}

void GL2Encoder::s_glGetInternalformativ(void* self, GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(pname != GL_NUM_SAMPLE_COUNTS &&
                 pname != GL_SAMPLES,
                 GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::internalFormatTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::unsizedFormat(internalformat) &&
                 !GLESv2Validation::colorRenderableFormat(ctx, internalformat) &&
                 !GLESv2Validation::depthRenderableFormat(ctx, internalformat) &&
                 !GLESv2Validation::stencilRenderableFormat(ctx, internalformat),
                 GL_INVALID_ENUM);
    SET_ERROR_IF(bufSize < 0, GL_INVALID_VALUE);

    if (bufSize < 1) return;

    // Desktop OpenGL can allow a mindboggling # samples per pixel (such as 64).
    // Limit to 4 (spec minimum) to keep dEQP tests from timing out.
    switch (pname) {
        case GL_NUM_SAMPLE_COUNTS:
            *params = 3;
            break;
        case GL_SAMPLES:
            params[0] = 4;
            if (bufSize > 1) params[1] = 2;
            if (bufSize > 2) params[2] = 1;
            break;
        default:
            break;
    }
}

void GL2Encoder::s_glGenerateMipmap(void* self, GLenum target) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_TEXTURE_2D &&
                 target != GL_TEXTURE_3D &&
                 target != GL_TEXTURE_CUBE_MAP &&
                 target != GL_TEXTURE_2D_ARRAY,
                 GL_INVALID_ENUM);

    GLuint tex = state->getBoundTexture(target);
    GLenum internalformat = state->queryTexInternalFormat(tex);

    SET_ERROR_IF(tex && GLESv2Validation::isCompressedFormat(internalformat),
                 GL_INVALID_OPERATION);
    SET_ERROR_IF(tex &&
                 !GLESv2Validation::unsizedFormat(internalformat) &&
                 !(GLESv2Validation::colorRenderableFormat(ctx, internalformat) &&
                   GLESv2Validation::filterableTexFormat(ctx, internalformat)),
                 GL_INVALID_OPERATION);

    GLenum stateTarget = target;
    if (target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
        target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        stateTarget = GL_TEXTURE_CUBE_MAP;

    SET_ERROR_IF(!ctx->m_state->isBoundTextureComplete(stateTarget), GL_INVALID_OPERATION);

    if (target == GL_TEXTURE_2D) {
        ctx->override2DTextureTarget(target);
    }

    ctx->m_glGenerateMipmap_enc(ctx, target);

    if (target == GL_TEXTURE_2D) {
        ctx->restore2DTextureTarget(target);
    }
}

void GL2Encoder::s_glBindSampler(void* self, GLuint unit, GLuint sampler) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLint maxCombinedUnits;
    ctx->glGetIntegerv(ctx, GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedUnits);
    SET_ERROR_IF(unit >= maxCombinedUnits, GL_INVALID_VALUE);
    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    if (ctx->m_state->isSamplerBindNoOp(unit, sampler)) return;
    ctx->m_glBindSampler_enc(ctx, unit, sampler);
    ctx->m_state->bindSampler(unit, sampler);
}

void GL2Encoder::s_glDeleteSamplers(void* self, GLsizei n, const GLuint* samplers) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    ctx->m_state->onDeleteSamplers(n, samplers);
    ctx->m_state->setExistence(GLClientState::ObjectType::Sampler, false, n, samplers);
    ctx->m_glDeleteSamplers_enc(ctx, n, samplers);
}

GLsync GL2Encoder::s_glFenceSync(void* self, GLenum condition, GLbitfield flags) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    RET_AND_SET_ERROR_IF(condition != GL_SYNC_GPU_COMMANDS_COMPLETE, GL_INVALID_ENUM, 0);
    RET_AND_SET_ERROR_IF(flags != 0, GL_INVALID_VALUE, 0);
    uint64_t syncHandle = ctx->glFenceSyncAEMU(ctx, condition, flags);

    GLsync res = (GLsync)(uintptr_t)syncHandle;
    GLClientState::onFenceCreated(res);
    return res;
}

GLenum GL2Encoder::s_glClientWaitSync(void* self, GLsync wait_on, GLbitfield flags, GLuint64 timeout) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    RET_AND_SET_ERROR_IF(!GLClientState::fenceExists(wait_on), GL_INVALID_VALUE, GL_WAIT_FAILED);
    RET_AND_SET_ERROR_IF(flags && !(flags & GL_SYNC_FLUSH_COMMANDS_BIT), GL_INVALID_VALUE, GL_WAIT_FAILED);
    return ctx->glClientWaitSyncAEMU(ctx, (uint64_t)(uintptr_t)wait_on, flags, timeout);
}

void GL2Encoder::s_glWaitSync(void* self, GLsync wait_on, GLbitfield flags, GLuint64 timeout) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    SET_ERROR_IF(flags != 0, GL_INVALID_VALUE);
    SET_ERROR_IF(timeout != GL_TIMEOUT_IGNORED, GL_INVALID_VALUE);
    SET_ERROR_IF(!GLClientState::fenceExists(wait_on), GL_INVALID_VALUE);
    ctx->glWaitSyncAEMU(ctx, (uint64_t)(uintptr_t)wait_on, flags, timeout);
}

void GL2Encoder::s_glDeleteSync(void* self, GLsync sync) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    if (!sync) return;

    SET_ERROR_IF(!GLClientState::fenceExists(sync), GL_INVALID_VALUE);
    GLClientState::onFenceDestroyed(sync);
    ctx->glDeleteSyncAEMU(ctx, (uint64_t)(uintptr_t)sync);
}

GLboolean GL2Encoder::s_glIsSync(void* self, GLsync sync) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    return ctx->glIsSyncAEMU(ctx, (uint64_t)(uintptr_t)sync);
}

void GL2Encoder::s_glGetSynciv(void* self, GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values) {
    GL2Encoder *ctx = (GL2Encoder *)self;

    SET_ERROR_IF(!GLESv2Validation::allowedGetSyncParam(pname), GL_INVALID_ENUM);
    SET_ERROR_IF(bufSize < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!GLClientState::fenceExists(sync), GL_INVALID_VALUE);

    return ctx->glGetSyncivAEMU(ctx, (uint64_t)(uintptr_t)sync, pname, bufSize, length, values);
}

#define LIMIT_CASE(target, lim) \
    case target: \
        ctx->glGetIntegerv(ctx, lim, &limit); \
        SET_ERROR_IF(index < 0 || index >= limit, GL_INVALID_VALUE); \
        break; \

void GL2Encoder::s_glGetIntegeri_v(void* self, GLenum target, GLuint index, GLint* params) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    GLint limit;

    switch (target) {
    LIMIT_CASE(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS)
    LIMIT_CASE(GL_UNIFORM_BUFFER_BINDING, GL_MAX_UNIFORM_BUFFER_BINDINGS)
    LIMIT_CASE(GL_ATOMIC_COUNTER_BUFFER_BINDING, GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS)
    LIMIT_CASE(GL_SHADER_STORAGE_BUFFER_BINDING, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS)
    default:
        break;
    }

    const GLClientState::VertexAttribBindingVector& currBindings =
        state->currentVertexBufferBindings();

    switch (target) {
    case GL_VERTEX_BINDING_DIVISOR:
    case GL_VERTEX_BINDING_OFFSET:
    case GL_VERTEX_BINDING_STRIDE:
    case GL_VERTEX_BINDING_BUFFER:
        SET_ERROR_IF(index < 0 || index > currBindings.size(), GL_INVALID_VALUE);
        break;
    default:
        break;
    }

    switch (target) {
    case GL_VERTEX_BINDING_DIVISOR:
        *params = currBindings[index].divisor;
        return;
    case GL_VERTEX_BINDING_OFFSET:
        *params = currBindings[index].offset;
        return;
    case GL_VERTEX_BINDING_STRIDE:
        *params = currBindings[index].effectiveStride;
        return;
    case GL_VERTEX_BINDING_BUFFER:
        *params = currBindings[index].buffer;
        return;
    default:
        break;
    }

    ctx->safe_glGetIntegeri_v(target, index, params);
}

void GL2Encoder::s_glGetInteger64i_v(void* self, GLenum target, GLuint index, GLint64* params) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    GLClientState* state = ctx->m_state;

    GLint limit;

    switch (target) {
    LIMIT_CASE(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS)
    LIMIT_CASE(GL_UNIFORM_BUFFER_BINDING, GL_MAX_UNIFORM_BUFFER_BINDINGS)
    LIMIT_CASE(GL_ATOMIC_COUNTER_BUFFER_BINDING, GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS)
    LIMIT_CASE(GL_SHADER_STORAGE_BUFFER_BINDING, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS)
    default:
        break;
    }

    const GLClientState::VertexAttribBindingVector& currBindings =
        state->currentVertexBufferBindings();

    switch (target) {
    case GL_VERTEX_BINDING_DIVISOR:
    case GL_VERTEX_BINDING_OFFSET:
    case GL_VERTEX_BINDING_STRIDE:
    case GL_VERTEX_BINDING_BUFFER:
        SET_ERROR_IF(index < 0 || index > currBindings.size(), GL_INVALID_VALUE);
        break;
    default:
        break;
    }

    switch (target) {
    case GL_VERTEX_BINDING_DIVISOR:
        *params = currBindings[index].divisor;
        return;
    case GL_VERTEX_BINDING_OFFSET:
        *params = currBindings[index].offset;
        return;
    case GL_VERTEX_BINDING_STRIDE:
        *params = currBindings[index].effectiveStride;
        return;
    case GL_VERTEX_BINDING_BUFFER:
        *params = currBindings[index].buffer;
        return;
    default:
        break;
    }

    ctx->safe_glGetInteger64i_v(target, index, params);
}

void GL2Encoder::s_glGetInteger64v(void* self, GLenum param, GLint64* val) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    ctx->safe_glGetInteger64v(param, val);
}

void GL2Encoder::s_glGetBooleani_v(void* self, GLenum param, GLuint index, GLboolean* val) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    ctx->safe_glGetBooleani_v(param, index, val);
}

void GL2Encoder::s_glGetShaderiv(void* self, GLuint shader, GLenum pname, GLint* params) {
    GL2Encoder *ctx = (GL2Encoder *)self;
    ctx->m_glGetShaderiv_enc(self, shader, pname, params);

    SET_ERROR_IF(!GLESv2Validation::allowedGetShader(pname), GL_INVALID_ENUM);
    VALIDATE_SHADER_NAME(shader);

    if (pname == GL_SHADER_SOURCE_LENGTH) {
        ShaderData* shaderData = ctx->m_shared->getShaderData(shader);
        if (shaderData) {
            int totalLen = 0;
            for (int i = 0; i < shaderData->sources.size(); i++) {
                totalLen += shaderData->sources[i].size();
            }
            if (totalLen != 0) {
                *params = totalLen + 1; // account for null terminator
            }
        }
    }
}

void GL2Encoder::s_glActiveShaderProgram(void* self, GLuint pipeline, GLuint program) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;

    SET_ERROR_IF(!pipeline, GL_INVALID_OPERATION);
    SET_ERROR_IF(program && !shared->isShaderOrProgramObject(program), GL_INVALID_VALUE);
    SET_ERROR_IF(program && !shared->isProgram(program), GL_INVALID_OPERATION);

    ctx->m_glActiveShaderProgram_enc(ctx, pipeline, program);
    if (!state->currentProgram()) {
        state->setCurrentShaderProgram(program);
    }
}

GLuint GL2Encoder::s_glCreateShaderProgramv(void* self, GLenum shaderType, GLsizei count, const char** strings) {

    GLint* length = NULL;
    GL2Encoder* ctx = (GL2Encoder*)self;

    int len = glUtilsCalcShaderSourceLen((char**)strings, length, count);
    char *str = new char[len + 1];
    glUtilsPackStrings(str, (char**)strings, (GLint*)length, count);

    // Do GLSharedGroup and location WorkARound-specific initialization
    // Phase 1: create a ShaderData and initialize with replaceSamplerExternalWith2D()
    uint32_t spDataId = ctx->m_shared->addNewShaderProgramData();
    ShaderProgramData* spData = ctx->m_shared->getShaderProgramDataById(spDataId);

    if (!replaceSamplerExternalWith2D(str, &spData->shaderData)) {
        delete [] str;
        ctx->setError(GL_OUT_OF_MEMORY);
        ctx->m_shared->deleteShaderProgramDataById(spDataId);
        return -1;
    }

    GLuint res = ctx->glCreateShaderProgramvAEMU(ctx, shaderType, count, str, len + 1);
    delete [] str;

    // Phase 2: do glLinkProgram-related initialization for locationWorkARound
    GLint linkStatus = 0;
    ctx->m_glGetProgramiv_enc(self, res, GL_LINK_STATUS ,&linkStatus);
    ctx->m_shared->setProgramLinkStatus(res, linkStatus);
    if (!linkStatus) {
        ctx->m_shared->deleteShaderProgramDataById(spDataId);
        return -1;
    }

    ctx->m_shared->associateGLShaderProgram(res, spDataId);

    GLint numUniforms = 0;
    GLint numAttributes = 0;
    ctx->m_glGetProgramiv_enc(self, res, GL_ACTIVE_UNIFORMS, &numUniforms);
    ctx->m_glGetProgramiv_enc(self, res, GL_ACTIVE_ATTRIBUTES, &numAttributes);
    ctx->m_shared->initShaderProgramData(res, numUniforms, numAttributes);

    GLint maxLength=0;
    GLint maxAttribLength=0;
    ctx->m_glGetProgramiv_enc(self, res, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength);
    ctx->m_glGetProgramiv_enc(self, res, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &maxAttribLength);

    size_t bufLen = maxLength > maxAttribLength ? maxLength : maxAttribLength;
    GLint size; GLenum type; GLchar *name = new GLchar[bufLen + 1];

    for (GLint i = 0; i < numUniforms; ++i) {
        ctx->m_glGetActiveUniform_enc(self, res, i, maxLength, NULL, &size, &type, name);
        GLint location = ctx->m_glGetUniformLocation_enc(self, res, name);
        ctx->m_shared->setShaderProgramIndexInfo(res, i, location, size, type, name);
    }

    for (GLint i = 0; i < numAttributes; ++i) {
        ctx->m_glGetActiveAttrib_enc(self, res, i, maxAttribLength,  NULL, &size, &type, name);
        GLint location = ctx->m_glGetAttribLocation_enc(self, res, name);
        ctx->m_shared->setProgramAttribInfo(res, i, location, size, type, name);
    }

    GLint numBlocks;
    ctx->m_glGetProgramiv_enc(ctx, res, GL_ACTIVE_UNIFORM_BLOCKS, &numBlocks);
    ctx->m_shared->setActiveUniformBlockCountForProgram(res, numBlocks);

    GLint tfVaryingsCount;
    ctx->m_glGetProgramiv_enc(ctx, res, GL_TRANSFORM_FEEDBACK_VARYINGS, &tfVaryingsCount);
    ctx->m_shared->setTransformFeedbackVaryingsCountForProgram(res, tfVaryingsCount);

    delete [] name;

    return res;
}

void GL2Encoder::s_glProgramUniform1f(void* self, GLuint program, GLint location, GLfloat v0)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform1f_enc(self, program, location, v0);
}

void GL2Encoder::s_glProgramUniform1fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform1fv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform1i(void* self, GLuint program, GLint location, GLint v0)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform1i_enc(self, program, location, v0);

    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;
    GLenum target;

    if (shared->setSamplerUniform(program, location, v0, &target)) {
        GLenum origActiveTexture = state->getActiveTextureUnit();
        if (ctx->updateHostTexture2DBinding(GL_TEXTURE0 + v0, target)) {
            ctx->m_glActiveTexture_enc(self, origActiveTexture);
        }
        state->setActiveTextureUnit(origActiveTexture);
    }
}

void GL2Encoder::s_glProgramUniform1iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform1iv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform1ui(void* self, GLuint program, GLint location, GLuint v0)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform1ui_enc(self, program, location, v0);

    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;
    GLenum target;

    if (shared->setSamplerUniform(program, location, v0, &target)) {
        GLenum origActiveTexture = state->getActiveTextureUnit();
        if (ctx->updateHostTexture2DBinding(GL_TEXTURE0 + v0, target)) {
            ctx->m_glActiveTexture_enc(self, origActiveTexture);
        }
        state->setActiveTextureUnit(origActiveTexture);
    }
}

void GL2Encoder::s_glProgramUniform1uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform1uiv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform2f(void* self, GLuint program, GLint location, GLfloat v0, GLfloat v1)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform2f_enc(self, program, location, v0, v1);
}

void GL2Encoder::s_glProgramUniform2fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform2fv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform2i(void* self, GLuint program, GLint location, GLint v0, GLint v1)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform2i_enc(self, program, location, v0, v1);
}

void GL2Encoder::s_glProgramUniform2iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform2iv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform2ui(void* self, GLuint program, GLint location, GLint v0, GLuint v1)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform2ui_enc(self, program, location, v0, v1);
}

void GL2Encoder::s_glProgramUniform2uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform2uiv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform3f(void* self, GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform3f_enc(self, program, location, v0, v1, v2);
}

void GL2Encoder::s_glProgramUniform3fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform3fv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform3i(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLint v2)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform3i_enc(self, program, location, v0, v1, v2);
}

void GL2Encoder::s_glProgramUniform3iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform3iv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform3ui(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLuint v2)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform3ui_enc(self, program, location, v0, v1, v2);
}

void GL2Encoder::s_glProgramUniform3uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform3uiv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform4f(void* self, GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform4f_enc(self, program, location, v0, v1, v2, v3);
}

void GL2Encoder::s_glProgramUniform4fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform4fv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform4i(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform4i_enc(self, program, location, v0, v1, v2, v3);
}

void GL2Encoder::s_glProgramUniform4iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform4iv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniform4ui(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLuint v3)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform4ui_enc(self, program, location, v0, v1, v2, v3);
}

void GL2Encoder::s_glProgramUniform4uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniform4uiv_enc(self, program, location, count, value);
}

void GL2Encoder::s_glProgramUniformMatrix2fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix2fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix2x3fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix2x3fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix2x4fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix2x4fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix3fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix3fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix3x2fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix3x2fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix3x4fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix3x4fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix4fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix4fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix4x2fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix4x2fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramUniformMatrix4x3fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glProgramUniformMatrix4x3fv_enc(self, program, location, count, transpose, value);
}

void GL2Encoder::s_glProgramParameteri(void* self, GLuint program, GLenum pname, GLint value) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(pname != GL_PROGRAM_BINARY_RETRIEVABLE_HINT && pname != GL_PROGRAM_SEPARABLE, GL_INVALID_ENUM);
    SET_ERROR_IF(value != GL_FALSE && value != GL_TRUE, GL_INVALID_VALUE);
    ctx->m_glProgramParameteri_enc(self, program, pname, value);
}

void GL2Encoder::s_glUseProgramStages(void *self, GLuint pipeline, GLbitfield stages, GLuint program)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    GLSharedGroupPtr shared = ctx->m_shared;

    SET_ERROR_IF(!pipeline, GL_INVALID_OPERATION);
    SET_ERROR_IF(program && !shared->isShaderOrProgramObject(program), GL_INVALID_VALUE);
    SET_ERROR_IF(program && !shared->isProgram(program), GL_INVALID_OPERATION);

    ctx->m_glUseProgramStages_enc(self, pipeline, stages, program);
    state->associateProgramWithPipeline(program, pipeline);

    // There is an active non-separable shader program in effect; no need to update external/2D bindings.
    if (state->currentProgram()) {
        return;
    }

    // Otherwise, update host texture 2D bindings.
    ctx->updateHostTexture2DBindingsFromProgramData(program);

    if (program) {
        ctx->m_state->currentUniformValidationInfo = ctx->m_shared->getUniformValidationInfo(program);
        ctx->m_state->currentAttribValidationInfo = ctx->m_shared->getAttribValidationInfo(program);
    }
}

void GL2Encoder::s_glBindProgramPipeline(void* self, GLuint pipeline)
{
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    ctx->m_glBindProgramPipeline_enc(self, pipeline);

    // There is an active non-separable shader program in effect; no need to update external/2D bindings.
    if (!pipeline || state->currentProgram()) {
        return;
    }

    GLClientState::ProgramPipelineIterator it = state->programPipelineBegin();
    for (; it != state->programPipelineEnd(); ++it) {
        if (it->second == pipeline) {
            ctx->updateHostTexture2DBindingsFromProgramData(it->first);
        }
    }
}

void GL2Encoder::s_glGetProgramResourceiv(void* self, GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum * props, GLsizei bufSize, GLsizei * length, GLint * params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(bufSize < 0, GL_INVALID_VALUE);
    if (bufSize == 0) {
        if (length) *length = 0;
        return;
    }

    // Avoid modifying |name| if |*length| < bufSize.
    GLint* intermediate = new GLint[bufSize];
    GLsizei* myLength = length ? length : new GLsizei;
    bool needFreeLength = length == NULL;

    ctx->m_glGetProgramResourceiv_enc(self, program, programInterface, index, propCount, props, bufSize, myLength, intermediate);
    GLsizei writtenInts = *myLength;
    memcpy(params, intermediate, writtenInts * sizeof(GLint));

    delete [] intermediate;
    if (needFreeLength)
        delete myLength;
}

GLuint GL2Encoder::s_glGetProgramResourceIndex(void* self, GLuint program, GLenum programInterface, const char* name) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    return ctx->m_glGetProgramResourceIndex_enc(self, program, programInterface, name);
}

GLint GL2Encoder::s_glGetProgramResourceLocation(void* self, GLuint program, GLenum programInterface, const char* name) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    return ctx->m_glGetProgramResourceLocation_enc(self, program, programInterface, name);
}

void GL2Encoder::s_glGetProgramResourceName(void* self, GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize, GLsizei* length, char* name) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(bufSize < 0, GL_INVALID_VALUE);
    if (bufSize == 0) {
        if (length) *length = 0;
        return;
    }

    // Avoid modifying |name| if |*length| < bufSize.
    char* intermediate = new char[bufSize];
    GLsizei* myLength = length ? length : new GLsizei;
    bool needFreeLength = length == NULL;

    ctx->m_glGetProgramResourceName_enc(self, program, programInterface, index, bufSize, myLength, intermediate);
    GLsizei writtenStrLen = *myLength;
    memcpy(name, intermediate, writtenStrLen + 1);

    delete [] intermediate;
    if (needFreeLength)
        delete myLength;
}

void GL2Encoder::s_glGetProgramPipelineInfoLog(void* self, GLuint pipeline, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(bufSize < 0, GL_INVALID_VALUE);
    if (bufSize == 0) {
        if (length) *length = 0;
        return;
    }

    // Avoid modifying |infoLog| if |*length| < bufSize.
    GLchar* intermediate = new GLchar[bufSize];
    GLsizei* myLength = length ? length : new GLsizei;
    bool needFreeLength = length == NULL;

    ctx->m_glGetProgramPipelineInfoLog_enc(self, pipeline, bufSize, myLength, intermediate);
    GLsizei writtenStrLen = *myLength;
    memcpy(infoLog, intermediate, writtenStrLen + 1);

    delete [] intermediate;
    if (needFreeLength)
        delete myLength;
}

void GL2Encoder::s_glVertexAttribFormat(void* self, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    VALIDATE_VERTEX_ATTRIB_INDEX(attribindex);
    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);

    state->setVertexAttribFormat(attribindex, size, type, normalized, relativeoffset, false);
    ctx->m_glVertexAttribFormat_enc(ctx, attribindex, size, type, normalized, relativeoffset);
}

void GL2Encoder::s_glVertexAttribIFormat(void* self, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    VALIDATE_VERTEX_ATTRIB_INDEX(attribindex);
    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);

    state->setVertexAttribFormat(attribindex, size, type, GL_FALSE, relativeoffset, true);
    ctx->m_glVertexAttribIFormat_enc(ctx, attribindex, size, type, relativeoffset);
}

void GL2Encoder::s_glVertexBindingDivisor(void* self, GLuint bindingindex, GLuint divisor) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);

    state->setVertexBindingDivisor(bindingindex, divisor);
    ctx->m_glVertexBindingDivisor_enc(ctx, bindingindex, divisor);
}

void GL2Encoder::s_glVertexAttribBinding(void* self, GLuint attribindex, GLuint bindingindex) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    VALIDATE_VERTEX_ATTRIB_INDEX(attribindex);
    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);

    state->setVertexAttribBinding(attribindex, bindingindex);
    ctx->m_glVertexAttribBinding_enc(ctx, attribindex, bindingindex);
}

void GL2Encoder::s_glBindVertexBuffer(void* self, GLuint bindingindex, GLuint buffer, GLintptr offset, GLintptr stride) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(offset < 0, GL_INVALID_VALUE);

    GLint maxStride;
    ctx->glGetIntegerv(ctx, GL_MAX_VERTEX_ATTRIB_STRIDE, &maxStride);
    SET_ERROR_IF(stride < 0 || stride > maxStride, GL_INVALID_VALUE);

    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);

    state->bindIndexedBuffer(0, bindingindex, buffer, offset, 0, stride, stride);
    ctx->m_glBindVertexBuffer_enc(ctx, bindingindex, buffer, offset, stride);
}

void GL2Encoder::s_glDrawArraysIndirect(void* self, GLenum mode, const void* indirect) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    bool hasClientArrays = false;
    bool hasVBOs = false;
    ctx->getVBOUsage(&hasClientArrays, &hasVBOs);

    SET_ERROR_IF(hasClientArrays, GL_INVALID_OPERATION);
    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->boundBuffer(GL_DRAW_INDIRECT_BUFFER), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    GLuint indirectStructSize = glUtilsIndirectStructSize(INDIRECT_COMMAND_DRAWARRAYS);
    if (ctx->boundBuffer(GL_DRAW_INDIRECT_BUFFER)) {
        // BufferData* buf = ctx->getBufferData(target);
        // if (buf) {
        //     SET_ERROR_IF((GLuint)(uintptr_t)indirect + indirectStructSize > buf->m_size, GL_INVALID_VALUE);
        // }
        ctx->glDrawArraysIndirectOffsetAEMU(ctx, mode, (uintptr_t)indirect);
    } else {
        // Client command structs are technically allowed in desktop OpenGL, but not in ES.
        // This is purely for debug/dev purposes.
        ctx->glDrawArraysIndirectDataAEMU(ctx, mode, indirect, indirectStructSize);
    }
    ctx->m_state->postDraw();
}

void GL2Encoder::s_glDrawElementsIndirect(void* self, GLenum mode, GLenum type, const void* indirect) {
    GL2Encoder *ctx = (GL2Encoder*)self;

    GLClientState* state = ctx->m_state;

    bool hasClientArrays = false;
    bool hasVBOs = false;
    ctx->getVBOUsage(&hasClientArrays, &hasVBOs);

    SET_ERROR_IF(hasClientArrays, GL_INVALID_OPERATION);
    SET_ERROR_IF(!state->currentVertexArrayObject(), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->boundBuffer(GL_DRAW_INDIRECT_BUFFER), GL_INVALID_OPERATION);

    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->checkFramebufferCompleteness(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE, GL_INVALID_FRAMEBUFFER_OPERATION);

    GLuint indirectStructSize = glUtilsIndirectStructSize(INDIRECT_COMMAND_DRAWELEMENTS);
    if (ctx->boundBuffer(GL_DRAW_INDIRECT_BUFFER)) {
        // BufferData* buf = ctx->getBufferData(target);
        // if (buf) {
        //     SET_ERROR_IF((GLuint)(uintptr_t)indirect + indirectStructSize > buf->m_size, GL_INVALID_VALUE);
        // }
        ctx->glDrawElementsIndirectOffsetAEMU(ctx, mode, type, (uintptr_t)indirect);
    } else {
        // Client command structs are technically allowed in desktop OpenGL, but not in ES.
        // This is purely for debug/dev purposes.
        ctx->glDrawElementsIndirectDataAEMU(ctx, mode, type, indirect, indirectStructSize);
    }
    ctx->m_state->postDraw();
}

void GL2Encoder::s_glTexStorage2DMultisample(void* self, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;

    SET_ERROR_IF(target != GL_TEXTURE_2D_MULTISAMPLE, GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::pixelInternalFormat(internalformat), GL_INVALID_ENUM);
    SET_ERROR_IF(!state->getBoundTexture(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(width < 1 || height < 1, GL_INVALID_VALUE);
    SET_ERROR_IF(state->isBoundTextureImmutableFormat(target), GL_INVALID_OPERATION);
    GLint max_samples;
    ctx->s_glGetInternalformativ(ctx, target, internalformat, GL_SAMPLES, 1, &max_samples);
    SET_ERROR_IF(samples > max_samples, GL_INVALID_OPERATION);

    state->setBoundTextureInternalFormat(target, internalformat);
    state->setBoundTextureDims(target, target, 0, width, height, 1);
    state->setBoundTextureImmutableFormat(target);
    state->setBoundTextureSamples(target, samples);

    ctx->m_glTexStorage2DMultisample_enc(ctx, target, samples, internalformat, width, height, fixedsamplelocations);
}

GLenum GL2Encoder::s_glGetGraphicsResetStatusEXT(void* self) {
    (void)self;
    return GL_NO_ERROR;
}

void GL2Encoder::s_glReadnPixelsEXT(void* self, GLint x, GLint y, GLsizei width,
        GLsizei height, GLenum format, GLenum type, GLsizei bufSize,
        GLvoid* pixels) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(bufSize < glesv2_enc::pixelDataSize(self, width, height, format,
        type, 1), GL_INVALID_OPERATION);
    s_glReadPixels(self, x, y, width, height, format, type, pixels);
    ctx->m_state->postReadPixels();
}

void GL2Encoder::s_glGetnUniformfvEXT(void *self, GLuint program, GLint location,
        GLsizei bufSize, GLfloat* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(bufSize < glSizeof(glesv2_enc::uniformType(self, program,
        location)), GL_INVALID_OPERATION);
    s_glGetUniformfv(self, program, location, params);
}

void GL2Encoder::s_glGetnUniformivEXT(void *self, GLuint program, GLint location,
        GLsizei bufSize, GLint* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(bufSize < glSizeof(glesv2_enc::uniformType(self, program,
        location)), GL_INVALID_OPERATION);
    s_glGetUniformiv(self, program, location, params);
}

void GL2Encoder::s_glInvalidateFramebuffer(void* self, GLenum target, GLsizei numAttachments, const GLenum *attachments) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF((target != GL_FRAMEBUFFER) &&
                 (target != GL_READ_FRAMEBUFFER) &&
                 (target != GL_DRAW_FRAMEBUFFER), GL_INVALID_ENUM);
    SET_ERROR_IF(numAttachments < 0, GL_INVALID_VALUE);

    GLint maxColorAttachments;
    ctx->glGetIntegerv(ctx, GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
    for (GLsizei i = 0; i < numAttachments; ++i) {
        if (attachments[i] != GL_DEPTH_ATTACHMENT && attachments[i] != GL_STENCIL_ATTACHMENT && attachments[i] != GL_DEPTH_STENCIL_ATTACHMENT) {
            SET_ERROR_IF(attachments[i] >= GL_COLOR_ATTACHMENT0 + maxColorAttachments, GL_INVALID_OPERATION);
        }
    }

    ctx->m_glInvalidateFramebuffer_enc(ctx, target, numAttachments, attachments);
}

void GL2Encoder::s_glInvalidateSubFramebuffer(void* self, GLenum target, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(target != GL_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER, GL_INVALID_ENUM);
    SET_ERROR_IF(numAttachments < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(height < 0, GL_INVALID_VALUE);
    GLint maxColorAttachments;
    ctx->glGetIntegerv(ctx, GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
    for (GLsizei i = 0; i < numAttachments; ++i) {
        if (attachments[i] != GL_DEPTH_ATTACHMENT && attachments[i] != GL_STENCIL_ATTACHMENT && attachments[i] != GL_DEPTH_STENCIL_ATTACHMENT) {
            SET_ERROR_IF(attachments[i] >= GL_COLOR_ATTACHMENT0 + maxColorAttachments, GL_INVALID_OPERATION);
        }
    }
    ctx->m_glInvalidateSubFramebuffer_enc(ctx, target, numAttachments, attachments, x, y, width, height);
}

void GL2Encoder::s_glDispatchCompute(void* self, GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glDispatchCompute_enc(ctx, num_groups_x, num_groups_y, num_groups_z);
    ctx->m_state->postDispatchCompute();
}

void GL2Encoder::s_glDispatchComputeIndirect(void* self, GLintptr indirect) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glDispatchComputeIndirect_enc(ctx, indirect);
    ctx->m_state->postDispatchCompute();
}

void GL2Encoder::s_glGenTransformFeedbacks(void* self, GLsizei n, GLuint* ids) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glGenTransformFeedbacks_enc(ctx, n, ids);
    ctx->m_state->setExistence(GLClientState::ObjectType::TransformFeedback, true, n, ids);
}

void GL2Encoder::s_glDeleteTransformFeedbacks(void* self, GLsizei n, const GLuint* ids) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActive(), GL_INVALID_OPERATION);

    ctx->m_state->setExistence(GLClientState::ObjectType::TransformFeedback, false, n, ids);
    ctx->m_glDeleteTransformFeedbacks_enc(ctx, n, ids);
}

void GL2Encoder::s_glGenSamplers(void* self, GLsizei n, GLuint* ids) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glGenSamplers_enc(ctx, n, ids);
    ctx->m_state->setExistence(GLClientState::ObjectType::Sampler, true, n, ids);
}

void GL2Encoder::s_glGenQueries(void* self, GLsizei n, GLuint* ids) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_glGenQueries_enc(ctx, n, ids);
    ctx->m_state->setExistence(GLClientState::ObjectType::Query, true, n, ids);
}

void GL2Encoder::s_glDeleteQueries(void* self, GLsizei n, const GLuint* ids) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    ctx->m_state->setExistence(GLClientState::ObjectType::Query, false, n, ids);
    ctx->m_glDeleteQueries_enc(ctx, n, ids);
}

void GL2Encoder::s_glBindTransformFeedback(void* self, GLenum target, GLuint id) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(GL_TRANSFORM_FEEDBACK != target, GL_INVALID_ENUM);
    SET_ERROR_IF(ctx->m_state->getTransformFeedbackActiveUnpaused(), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_state->tryBind(target, id), GL_INVALID_OPERATION);
    ctx->m_glBindTransformFeedback_enc(ctx, target, id);
}

void GL2Encoder::s_glBeginQuery(void* self, GLenum target, GLuint query) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedQueryTarget(target), GL_INVALID_ENUM);

    if (target != GL_ANY_SAMPLES_PASSED_CONSERVATIVE &&
        target != GL_ANY_SAMPLES_PASSED) {
        SET_ERROR_IF(ctx->m_state->isQueryBound(target), GL_INVALID_OPERATION);
    } else {
        SET_ERROR_IF(ctx->m_state->isQueryBound(GL_ANY_SAMPLES_PASSED_CONSERVATIVE), GL_INVALID_OPERATION);
        SET_ERROR_IF(ctx->m_state->isQueryBound(GL_ANY_SAMPLES_PASSED), GL_INVALID_OPERATION);
    }

    GLenum lastTarget = ctx->m_state->getLastQueryTarget(query);

    if (lastTarget) {
        SET_ERROR_IF(target != lastTarget, GL_INVALID_OPERATION);
    }

    SET_ERROR_IF(!query, GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_state->tryBind(target, query), GL_INVALID_OPERATION);
    ctx->m_state->setLastQueryTarget(target, query);
    ctx->m_glBeginQuery_enc(ctx, target, query);
}

void GL2Encoder::s_glEndQuery(void* self, GLenum target) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedQueryTarget(target), GL_INVALID_ENUM);
    SET_ERROR_IF(!ctx->m_state->isBoundTargetValid(target), GL_INVALID_OPERATION);
    SET_ERROR_IF(!ctx->m_state->tryBind(target, 0), GL_INVALID_OPERATION);
    ctx->m_glEndQuery_enc(ctx, target);
}

void GL2Encoder::s_glClear(void* self, GLbitfield mask) {
    GL2Encoder *ctx = (GL2Encoder*)self;

    GLbitfield allowed_bits = GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    GLbitfield has_disallowed_bits = (mask & ~allowed_bits);
    SET_ERROR_IF(has_disallowed_bits, GL_INVALID_VALUE);

    ctx->m_glClear_enc(ctx, mask);
}

void GL2Encoder::s_glCopyTexSubImage2D(void *self , GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::textureTarget(ctx, target), GL_INVALID_ENUM);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    GLint max_texture_size;
    GLint max_cube_map_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_map_texture_size);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF((target == GL_TEXTURE_CUBE_MAP) &&
                 (level > ilog2(max_cube_map_texture_size)), GL_INVALID_VALUE);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(width > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(height > max_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && width > max_cube_map_texture_size, GL_INVALID_VALUE);
    SET_ERROR_IF(GLESv2Validation::isCubeMapTarget(target) && height > max_cube_map_texture_size, GL_INVALID_VALUE);
    GLuint tex = ctx->m_state->getBoundTexture(target);
    GLsizei neededWidth = xoffset + width;
    GLsizei neededHeight = yoffset + height;
    GFXSTREAM_VERBOSE("tex %u needed width height %d %d xoff %d width %d yoff %d height %d (texture width %d height %d) level %d\n",
            tex,
            neededWidth,
            neededHeight,
            xoffset,
            width,
            yoffset,
            height,
            ctx->m_state->queryTexWidth(level, tex),
            ctx->m_state->queryTexWidth(level, tex),
            level);

    SET_ERROR_IF(tex &&
                 (neededWidth > ctx->m_state->queryTexWidth(level, tex) ||
                  neededHeight > ctx->m_state->queryTexHeight(level, tex)),
                 GL_INVALID_VALUE);
    SET_ERROR_IF(ctx->glCheckFramebufferStatus(ctx, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE,
                 GL_INVALID_FRAMEBUFFER_OPERATION);

    ctx->m_glCopyTexSubImage2D_enc(ctx, target, level, xoffset, yoffset, x, y, width, height);
}

void GL2Encoder::s_glCopyTexSubImage3D(void *self , GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(target != GL_TEXTURE_3D &&
                 target != GL_TEXTURE_2D_ARRAY,
                 GL_INVALID_ENUM);
    GLint max_texture_size;
    GLint max_3d_texture_size;
    ctx->glGetIntegerv(ctx, GL_MAX_TEXTURE_SIZE, &max_texture_size);
    ctx->glGetIntegerv(ctx, GL_MAX_3D_TEXTURE_SIZE, &max_3d_texture_size);
    SET_ERROR_IF(level < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(level > ilog2(max_3d_texture_size), GL_INVALID_VALUE);
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(xoffset < 0 || yoffset < 0 || zoffset < 0, GL_INVALID_VALUE);
    GLuint tex = ctx->m_state->getBoundTexture(target);
    GLsizei neededWidth = xoffset + width;
    GLsizei neededHeight = yoffset + height;
    GLsizei neededDepth = zoffset + 1;
    SET_ERROR_IF(tex &&
                 (neededWidth > ctx->m_state->queryTexWidth(level, tex) ||
                  neededHeight > ctx->m_state->queryTexHeight(level, tex) ||
                  neededDepth > ctx->m_state->queryTexDepth(level, tex)),
                 GL_INVALID_VALUE);
    SET_ERROR_IF(ctx->glCheckFramebufferStatus(ctx, GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE,
                 GL_INVALID_FRAMEBUFFER_OPERATION);

    ctx->m_glCopyTexSubImage3D_enc(ctx, target, level, xoffset, yoffset, zoffset, x, y, width, height);
}

void GL2Encoder::s_glCompileShader(void* self, GLuint shader) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    bool isShaderOrProgramObject =
        ctx->m_shared->isShaderOrProgramObject(shader);
    bool isShader =
        ctx->m_shared->isShader(shader);

    SET_ERROR_IF(isShaderOrProgramObject && !isShader, GL_INVALID_OPERATION);
    SET_ERROR_IF(!isShaderOrProgramObject && !isShader, GL_INVALID_VALUE);

    ctx->m_glCompileShader_enc(ctx, shader);
}

void GL2Encoder::s_glValidateProgram(void* self, GLuint program ) {
    GL2Encoder *ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);

    ctx->m_glValidateProgram_enc(self, program);
}

void GL2Encoder::s_glGetSamplerParameterfv(void *self, GLuint sampler, GLenum pname, GLfloat* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;

    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::samplerParams(ctx, pname), GL_INVALID_ENUM);

    if (!params) return;

    ctx->m_glGetSamplerParameterfv_enc(ctx, sampler, pname, params);
}

void GL2Encoder::s_glGetSamplerParameteriv(void *self, GLuint sampler, GLenum pname, GLint* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::samplerParams(ctx, pname), GL_INVALID_ENUM);

    if (!params) return;

    ctx->m_glGetSamplerParameteriv_enc(ctx, sampler, pname, params);
}

void GL2Encoder::s_glSamplerParameterf(void *self , GLuint sampler, GLenum pname, GLfloat param) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::samplerParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, (GLint)param, param, (GLenum)param), GL_INVALID_ENUM);

    ctx->m_glSamplerParameterf_enc(ctx, sampler, pname, param);
}

void GL2Encoder::s_glSamplerParameteri(void *self , GLuint sampler, GLenum pname, GLint param) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::samplerParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, param, (GLfloat)param, (GLenum)param), GL_INVALID_ENUM);

    ctx->m_glSamplerParameteri_enc(ctx, sampler, pname, param);
}

void GL2Encoder::s_glSamplerParameterfv(void *self , GLuint sampler, GLenum pname, const GLfloat* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::samplerParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!params, GL_INVALID_VALUE);
    GLfloat param = *params;
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, (GLint)param, param, (GLenum)param), GL_INVALID_ENUM);

    ctx->m_glSamplerParameterfv_enc(ctx, sampler, pname, params);
}

void GL2Encoder::s_glSamplerParameteriv(void *self , GLuint sampler, GLenum pname, const GLint* params) {
    GL2Encoder *ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!ctx->m_state->samplerExists(sampler), GL_INVALID_OPERATION);
    SET_ERROR_IF(!GLESv2Validation::samplerParams(ctx, pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!params, GL_INVALID_VALUE);
    GLint param = *params;
    SET_ERROR_IF(!GLESv2Validation::textureParamValue(ctx, pname, (GLint)param, param, (GLenum)param), GL_INVALID_ENUM);

    ctx->m_glSamplerParameteriv_enc(ctx, sampler, pname, params);
}

int GL2Encoder::s_glGetAttribLocation(void *self , GLuint program, const GLchar* name) {
    GL2Encoder *ctx = (GL2Encoder*)self;

    bool isShaderOrProgramObject =
        ctx->m_shared->isShaderOrProgramObject(program);
    bool isProgram =
        ctx->m_shared->isProgram(program);

    RET_AND_SET_ERROR_IF(!isShaderOrProgramObject, GL_INVALID_VALUE, -1);
    RET_AND_SET_ERROR_IF(!isProgram, GL_INVALID_OPERATION, -1);
    RET_AND_SET_ERROR_IF(!ctx->m_shared->getProgramLinkStatus(program), GL_INVALID_OPERATION, -1);

    return ctx->m_glGetAttribLocation_enc(ctx, program, name);
}

void GL2Encoder::s_glBindAttribLocation(void *self , GLuint program, GLuint index, const GLchar* name) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);

    GLint maxVertexAttribs;
    ctx->glGetIntegerv(self, GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    SET_ERROR_IF(!(index < maxVertexAttribs), GL_INVALID_VALUE);
    SET_ERROR_IF(index > maxVertexAttribs, GL_INVALID_VALUE);
    SET_ERROR_IF(name && !strncmp("gl_", name, 3), GL_INVALID_OPERATION);

    fprintf(stderr, "%s: bind attrib %u name %s\n", __func__, index, name);
    ctx->m_glBindAttribLocation_enc(ctx, program, index, name);
}

// TODO-SLOW
void GL2Encoder::s_glUniformBlockBinding(void *self , GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(uniformBlockIndex >= ctx->m_shared->getActiveUniformBlockCount(program), GL_INVALID_VALUE);

    GLint maxUniformBufferBindings;
    ctx->glGetIntegerv(ctx, GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUniformBufferBindings);
    SET_ERROR_IF(uniformBlockBinding >= maxUniformBufferBindings, GL_INVALID_VALUE);

    ctx->m_glUniformBlockBinding_enc(ctx, program, uniformBlockIndex, uniformBlockBinding);
}

void GL2Encoder::s_glGetTransformFeedbackVarying(void *self , GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLsizei* size, GLenum* type, char* name) {
    GL2Encoder* ctx = (GL2Encoder*)self;

    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(!ctx->m_shared->getProgramLinkStatus(program), GL_INVALID_OPERATION);
    SET_ERROR_IF(index >= ctx->m_shared->getTransformFeedbackVaryingsCountForProgram(program), GL_INVALID_VALUE);

    ctx->m_glGetTransformFeedbackVarying_enc(ctx, program, index, bufSize, length, size, type, name);
}

void GL2Encoder::s_glScissor(void *self , GLint x, GLint y, GLsizei width, GLsizei height) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    ctx->m_glScissor_enc(ctx, x, y, width, height);
}

void GL2Encoder::s_glDepthFunc(void *self , GLenum func) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        (func != GL_NEVER) &&
        (func != GL_ALWAYS) &&
        (func != GL_LESS) &&
        (func != GL_LEQUAL) &&
        (func != GL_EQUAL) &&
        (func != GL_GREATER) &&
        (func != GL_GEQUAL) &&
        (func != GL_NOTEQUAL),
        GL_INVALID_ENUM);
    ctx->m_glDepthFunc_enc(ctx, func);
}

void GL2Encoder::s_glViewport(void *self , GLint x, GLint y, GLsizei width, GLsizei height) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(width < 0 || height < 0, GL_INVALID_VALUE);
    ctx->m_glViewport_enc(ctx, x, y, width, height);
}

void GL2Encoder::s_glStencilFunc(void *self , GLenum func, GLint ref, GLuint mask) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedFunc(func), GL_INVALID_ENUM);
    if (!ctx->m_state) return;
    ctx->m_state->stencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask);
    ctx->m_glStencilFunc_enc(ctx, func, ref, mask);
}

void GL2Encoder::s_glStencilFuncSeparate(void *self , GLenum face, GLenum func, GLint ref, GLuint mask) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedFace(face) || !GLESv2Validation::allowedFunc(func), GL_INVALID_ENUM);
    if (!ctx->m_state) return;
    ctx->m_state->stencilFuncSeparate(face, func, ref, mask);
    ctx->m_glStencilFuncSeparate_enc(ctx, face, func, ref, mask);
}

void GL2Encoder::s_glStencilOp(void *self , GLenum fail, GLenum zfail, GLenum zpass) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedStencilOp(fail) ||
        !GLESv2Validation::allowedStencilOp(zfail) ||
        !GLESv2Validation::allowedStencilOp(zpass),
        GL_INVALID_ENUM);
    if (!ctx->m_state) return;
    ctx->m_state->stencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass);
    ctx->m_glStencilOp_enc(ctx, fail, zfail, zpass);
}

void GL2Encoder::s_glStencilOpSeparate(void *self , GLenum face, GLenum fail, GLenum zfail, GLenum zpass) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedFace(face) ||
        !GLESv2Validation::allowedStencilOp(fail) ||
        !GLESv2Validation::allowedStencilOp(zfail) ||
        !GLESv2Validation::allowedStencilOp(zpass),
        GL_INVALID_ENUM);
    if (!ctx->m_state) return;
    ctx->m_state->stencilOpSeparate(face, fail, zfail, zpass);
    ctx->m_glStencilOpSeparate_enc(ctx, face, fail, zfail, zpass);
}

void GL2Encoder::s_glStencilMaskSeparate(void *self , GLenum face, GLuint mask) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedFace(face),
        GL_INVALID_ENUM);
    if (!ctx->m_state) return;
    ctx->m_state->stencilMaskSeparate(face, mask);
    ctx->m_glStencilMaskSeparate_enc(ctx, face, mask);
}

void GL2Encoder::s_glBlendEquation(void *self , GLenum mode) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedBlendEquation(mode),
        GL_INVALID_ENUM);
    ctx->m_glBlendEquation_enc(ctx, mode);
}

void GL2Encoder::s_glBlendEquationSeparate(void *self , GLenum modeRGB, GLenum modeAlpha) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedBlendEquation(modeRGB) ||
        !GLESv2Validation::allowedBlendEquation(modeAlpha),
        GL_INVALID_ENUM);
    ctx->m_glBlendEquationSeparate_enc(ctx, modeRGB, modeAlpha);
}

void GL2Encoder::s_glBlendFunc(void *self , GLenum sfactor, GLenum dfactor) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedBlendFunc(sfactor) ||
        !GLESv2Validation::allowedBlendFunc(dfactor),
        GL_INVALID_ENUM);
    ctx->m_glBlendFunc_enc(ctx, sfactor, dfactor);
}

void GL2Encoder::s_glBlendFuncSeparate(void *self , GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedBlendFunc(srcRGB) ||
        !GLESv2Validation::allowedBlendFunc(dstRGB) ||
        !GLESv2Validation::allowedBlendFunc(srcAlpha) ||
        !GLESv2Validation::allowedBlendFunc(dstAlpha),
        GL_INVALID_ENUM);
    ctx->m_glBlendFuncSeparate_enc(ctx, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

void GL2Encoder::s_glCullFace(void *self , GLenum mode) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedCullFace(mode),
        GL_INVALID_ENUM);
    ctx->m_glCullFace_enc(ctx, mode);
}

void GL2Encoder::s_glFrontFace(void *self , GLenum mode) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(
        !GLESv2Validation::allowedFrontFace(mode),
        GL_INVALID_ENUM);
    ctx->m_glFrontFace_enc(ctx, mode);
}

void GL2Encoder::s_glLineWidth(void *self , GLfloat width) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(width <= 0.0f, GL_INVALID_VALUE);
    ctx->m_glLineWidth_enc(ctx, width);
}

void GL2Encoder::s_glVertexAttrib1f(void *self , GLuint indx, GLfloat x) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib1f_enc(ctx, indx, x);
}

void GL2Encoder::s_glVertexAttrib2f(void *self , GLuint indx, GLfloat x, GLfloat y) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib2f_enc(ctx, indx, x, y);
}

void GL2Encoder::s_glVertexAttrib3f(void *self , GLuint indx, GLfloat x, GLfloat y, GLfloat z) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib3f_enc(ctx, indx, x, y, z);
}

void GL2Encoder::s_glVertexAttrib4f(void *self , GLuint indx, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib4f_enc(ctx, indx, x, y, z, w);
}

void GL2Encoder::s_glVertexAttrib1fv(void *self , GLuint indx, const GLfloat* values) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib1fv_enc(ctx, indx, values);
}

void GL2Encoder::s_glVertexAttrib2fv(void *self , GLuint indx, const GLfloat* values) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib2fv_enc(ctx, indx, values);
}

void GL2Encoder::s_glVertexAttrib3fv(void *self , GLuint indx, const GLfloat* values) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib3fv_enc(ctx, indx, values);
}

void GL2Encoder::s_glVertexAttrib4fv(void *self , GLuint indx, const GLfloat* values) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(indx);
    ctx->m_glVertexAttrib4fv_enc(ctx, indx, values);
}

void GL2Encoder::s_glVertexAttribI4i(void *self , GLuint index, GLint v0, GLint v1, GLint v2, GLint v3) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_glVertexAttribI4i_enc(ctx, index, v0, v1, v2, v3);
}

void GL2Encoder::s_glVertexAttribI4ui(void *self , GLuint index, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_glVertexAttribI4ui_enc(ctx, index, v0, v1, v2, v3);
}

void GL2Encoder::s_glVertexAttribI4iv(void *self , GLuint index, const GLint* v) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_glVertexAttribI4iv_enc(ctx, index, v);
}

void GL2Encoder::s_glVertexAttribI4uiv(void *self , GLuint index, const GLuint* v) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    ctx->m_glVertexAttribI4uiv_enc(ctx, index, v);
}

void GL2Encoder::s_glGetShaderPrecisionFormat(void *self , GLenum shadertype, GLenum precisiontype, GLint* range, GLint* precision) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedShaderType(shadertype), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::allowedPrecisionType(precisiontype), GL_INVALID_ENUM);
    ctx->m_glGetShaderPrecisionFormat_enc(ctx, shadertype, precisiontype, range, precision);
}

void GL2Encoder::s_glGetProgramiv(void *self , GLuint program, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedGetProgram(ctx->majorVersion(), ctx->minorVersion(), pname), GL_INVALID_ENUM);
    VALIDATE_PROGRAM_NAME(program);

    if (pname == GL_PROGRAM_BINARY_LENGTH) {
        return ctx->getProgramBinaryLength(program, params);
    }

    ctx->m_glGetProgramiv_enc(ctx, program, pname, params);
}

void GL2Encoder::s_glGetActiveUniform(void *self , GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(index >= ctx->m_shared->getActiveUniformsCountForProgram(program), GL_INVALID_VALUE);
    ctx->m_glGetActiveUniform_enc(ctx, program, index, bufsize, length, size, type, name);
}

void GL2Encoder::s_glGetActiveUniformsiv(void *self , GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(uniformCount < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(!GLESv2Validation::allowedGetActiveUniforms(pname), GL_INVALID_ENUM);
    int activeUniformsCount = ctx->m_shared->getActiveUniformsCountForProgram(program);
    for (GLsizei i = 0; i < uniformCount; ++i) {
        SET_ERROR_IF(uniformIndices[i] >= activeUniformsCount, GL_INVALID_VALUE);
    }
    ctx->m_glGetActiveUniformsiv_enc(ctx, program, uniformCount, uniformIndices, pname, params);
}

void GL2Encoder::s_glGetActiveUniformBlockName(void *self , GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformBlockName) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    SET_ERROR_IF(bufSize < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(uniformBlockIndex >= ctx->m_shared->getActiveUniformBlockCount(program), GL_INVALID_VALUE);
    ctx->m_glGetActiveUniformBlockName_enc(ctx, program, uniformBlockIndex, bufSize, length, uniformBlockName);
}

void GL2Encoder::s_glGetActiveAttrib(void *self , GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME(program);
    VALIDATE_VERTEX_ATTRIB_INDEX(index);
    SET_ERROR_IF(bufsize < 0, GL_INVALID_VALUE);
    SET_ERROR_IF(index >= ctx->m_shared->getActiveAttributesCountForProgram(program), GL_INVALID_VALUE);
    ctx->m_glGetActiveAttrib_enc(ctx, program, index, bufsize, length, size, type, name);
}

void GL2Encoder::s_glGetRenderbufferParameteriv(void *self , GLenum target, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(target != GL_RENDERBUFFER, GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::allowedGetRenderbufferParameter(pname), GL_INVALID_ENUM);
    SET_ERROR_IF(0 == ctx->m_state->boundRenderbuffer(), GL_INVALID_OPERATION);
    ctx->m_glGetRenderbufferParameteriv_enc(ctx, target, pname, params);
}

void GL2Encoder::s_glGetQueryiv(void *self , GLenum target, GLenum pname, GLint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedQueryTarget(target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::allowedQueryParam(pname), GL_INVALID_ENUM);
    ctx->m_glGetQueryiv_enc(ctx, target, pname, params);
}

void GL2Encoder::s_glGetQueryObjectuiv(void *self , GLuint query, GLenum pname, GLuint* params) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    GLClientState* state = ctx->m_state;
    SET_ERROR_IF(!GLESv2Validation::allowedQueryObjectParam(pname), GL_INVALID_ENUM);
    SET_ERROR_IF(!state->queryExistence(GLClientState::ObjectType::Query, query), GL_INVALID_OPERATION);
    SET_ERROR_IF(!state->getLastQueryTarget(query), GL_INVALID_OPERATION);
    SET_ERROR_IF(ctx->m_state->isQueryObjectActive(query), GL_INVALID_OPERATION);

    ctx->m_glGetQueryObjectuiv_enc(ctx, query, pname, params);
}

GLboolean GL2Encoder::s_glIsEnabled(void *self , GLenum cap) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    RET_AND_SET_ERROR_IF(!GLESv2Validation::allowedEnable(ctx->majorVersion(), ctx->minorVersion(), cap), GL_INVALID_ENUM, 0);
    return ctx->m_glIsEnabled_enc(ctx, cap);
}

void GL2Encoder::s_glHint(void *self , GLenum target, GLenum mode) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    SET_ERROR_IF(!GLESv2Validation::allowedHintTarget(target), GL_INVALID_ENUM);
    SET_ERROR_IF(!GLESv2Validation::allowedHintMode(mode), GL_INVALID_ENUM);
    ctx->m_glHint_enc(ctx, target, mode);
}

GLint GL2Encoder::s_glGetFragDataLocation (void *self , GLuint program, const char* name) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    VALIDATE_PROGRAM_NAME_RET(program, -1);
    RET_AND_SET_ERROR_IF(!ctx->m_shared->getProgramLinkStatus(program), GL_INVALID_OPERATION, -1);
    return ctx->m_glGetFragDataLocation_enc(ctx, program, name);
}

void GL2Encoder::s_glStencilMask(void* self, GLuint mask) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    if (!ctx->m_state) return;
    ctx->m_state->stencilMaskSeparate(GL_FRONT_AND_BACK, mask);
    ctx->m_glStencilMask_enc(ctx, mask);
}

void GL2Encoder::s_glClearStencil(void* self, int v) {
    GL2Encoder* ctx = (GL2Encoder*)self;
    if (!ctx->m_state) return;
    ctx->m_state->state_GL_STENCIL_CLEAR_VALUE = v;
    ctx->m_glClearStencil_enc(ctx, v);
}
