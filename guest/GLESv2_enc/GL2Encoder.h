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
#ifndef _GL2_ENCODER_H_
#define _GL2_ENCODER_H_

#include <optional>
#include <string>
#include <vector>

#include "ProgramBinary.pb.h"
#include "gfxstream/guest/GLClientState.h"
#include "gfxstream/guest/GLSharedGroup.h"
#include "gl2_enc.h"

struct Extensions
{
    Extensions() = default;
    Extensions(const Extensions &other) = default;

    Extensions &operator=(const Extensions &other) = default;

    bool textureBufferAny() const { return textureBufferEXT || textureBufferOES; }
    // GL_EXT_texture_buffer
    bool textureBufferEXT = false;

    // GL_OES_texture_buffer
    bool textureBufferOES = false;

    // GL_EXT_draw_buffers_indexed
    bool drawBuffersIndexedEXT = false;
};

class GL2Encoder : public gl2_encoder_context_t {
public:
 GL2Encoder(gfxstream::guest::IOStream* stream, gfxstream::guest::ChecksumCalculator* protocol);
 virtual ~GL2Encoder();
 const Extensions& getExtensions() const { return m_extensions; }
 void setDrawCallFlushInterval(uint32_t interval) { m_drawCallFlushInterval = interval; }
 void setHasAsyncUnmapBuffer(int version) { m_hasAsyncUnmapBuffer = version; }
 void setHasSyncBufferData(bool value) { m_hasSyncBufferData = value; }
 void setNoHostError(bool noHostError) { m_noHostError = noHostError; }
 void setClientState(gfxstream::guest::GLClientState* state) { m_state = state; }
 void setVersion(int major, int minor, int deviceMajor, int deviceMinor) {
     m_currMajorVersion = major;
     m_currMinorVersion = minor;
     m_deviceMajorVersion = deviceMajor;
     m_deviceMinorVersion = deviceMinor;
 }
    void setClientStateMakeCurrent(gfxstream::guest::GLClientState *state,
                                   int majorVersion,
                                   int minorVersion,
                                   int deviceMajorVersion,
                                   int deviceMinorVersion) {
        m_state = state;
        m_state->fromMakeCurrent();
        m_currMajorVersion = majorVersion;
        m_currMinorVersion = minorVersion;
        m_deviceMajorVersion = deviceMajorVersion;
        m_deviceMinorVersion = deviceMinorVersion;
    }
    void setSharedGroup(gfxstream::guest::GLSharedGroupPtr shared) {
        m_shared = shared;
        if (m_state && m_shared) {
            m_state->setTextureData(m_shared->getTextureData());
            m_state->setRenderbufferInfo(m_shared->getRenderbufferInfo());
            m_state->setSamplerInfo(m_shared->getSamplerInfo());
        }
    }
    bool es32Plus() const { return m_currMajorVersion > 3 || (m_currMajorVersion == 3 && m_currMinorVersion >= 2); }
    int majorVersion() const { return m_currMajorVersion; }
    int minorVersion() const { return m_currMinorVersion; }
    void setExtensions(const char* exts,
                       const std::vector<std::string>& extArray) {
        m_currExtensions = std::string(exts);
        m_currExtensionsArray = extArray;
        m_state->setExtensions(m_currExtensions);

        m_extensions.textureBufferEXT = hasExtension("GL_EXT_texture_buffer");
        m_extensions.textureBufferOES = hasExtension("GL_OES_texture_buffer");
        m_extensions.drawBuffersIndexedEXT = hasExtension("GL_EXT_draw_buffers_indexed");
    }
    bool hasExtension(const char* ext) const {
        return m_currExtensions.find(ext) != std::string::npos;
    }
    const gfxstream::guest::GLClientState *state() { return m_state; }
    const gfxstream::guest::GLSharedGroupPtr shared() { return m_shared; }
    void flush() { m_stream->flush(); }

    void setInitialized(){ m_initialized = true; };
    bool isInitialized(){ return m_initialized; };

    virtual void setError(GLenum error){ m_error = error; };
    virtual GLenum getError() { return m_error; };

    __attribute__((always_inline)) GLenum* getErrorPtr() { return &m_error; }
    __attribute__((always_inline)) bool hasError() const { return m_error != GL_NO_ERROR; }

    void override2DTextureTarget(GLenum target);
    void restore2DTextureTarget(GLenum target);
    void associateEGLImage(GLenum target, GLeglImageOES eglImage, int width, int height);

    // Convenience functions for buffers
    GLuint boundBuffer(GLenum target) const;
    gfxstream::guest::BufferData* getBufferData(GLenum target) const;
    gfxstream::guest::BufferData* getBufferDataById(GLuint buffer) const;
    bool isBufferMapped(GLuint buffer) const;
    bool isBufferTargetMapped(GLenum target) const;

    std::optional<gfxstream::guest::gles2::ProgramBinaryInfo> getProgramBinary(GLuint program);
    void getProgramBinaryLength(GLuint program, GLint* outLength);

   private:

    int m_currMajorVersion;
    int m_currMinorVersion;
    int m_deviceMajorVersion;
    int m_deviceMinorVersion;
    std::string m_currExtensions;
    std::vector<std::string> m_currExtensionsArray;

    Extensions m_extensions;

    bool    m_hasAsyncUnmapBuffer;
    bool    m_hasSyncBufferData;
    bool    m_initialized;
    bool    m_noHostError;
    gfxstream::guest::GLClientState *m_state;
    gfxstream::guest::GLSharedGroupPtr m_shared;
    GLenum  m_error;

    GLint *m_compressedTextureFormats;
    GLint m_num_compressedTextureFormats;
    GLint *getCompressedTextureFormats();

    GLint m_max_combinedTextureImageUnits;
    GLint m_max_vertexTextureImageUnits;
    GLint m_max_array_texture_layers;;
    GLint m_max_textureImageUnits;
    GLint m_max_cubeMapTextureSize;
    GLint m_max_renderBufferSize;
    GLint m_max_textureSize;
    GLint m_max_3d_textureSize;
    GLint m_max_vertexAttribStride;

    GLint m_max_transformFeedbackSeparateAttribs;
    GLint m_max_uniformBufferBindings;
    GLint m_max_colorAttachments;
    GLint m_max_drawBuffers;

    GLint m_max_atomicCounterBufferBindings;
    GLint m_max_shaderStorageBufferBindings;
    GLint m_max_vertexAttribBindings;
    GLint m_textureBufferOffsetAlign;

    GLuint m_ssbo_offset_align;
    GLuint m_ubo_offset_align;

    GLint m_log2MaxTextureSize;

    std::vector<char> m_fixedBuffer;

    uint32_t m_drawCallFlushInterval;
    uint32_t m_drawCallFlushCount;

    bool m_primitiveRestartEnabled;
    GLuint m_primitiveRestartIndex;

    void calcIndexRange(const void* indices,
                        GLenum type, GLsizei count,
                        int* minIndex, int* maxIndex);
    void* recenterIndices(const void* src,
                          GLenum type, GLsizei count,
                          int minIndex);
    void getBufferIndexRange(gfxstream::guest::BufferData* buf, const void* dataWithOffset,
                             GLenum type, size_t count, size_t offset,
                             int* minIndex_out, int* maxIndex_out);
    void getVBOUsage(bool* hasClientArrays, bool* hasVBOs) const;
    void sendVertexAttributes(GLint first, GLsizei count, bool hasClientArrays, GLsizei primcount = 0);
    void flushDrawCall();

    bool updateHostTexture2DBinding(GLenum texUnit, GLenum newTarget);
    void updateHostTexture2DBindingsFromProgramData(GLuint program);
    bool texture2DNeedsOverride(GLenum target) const;

    void updateProgramInfoAfterLink(GLuint program);

    // Utility classes for safe queries that
    // need access to private class members
    class ErrorUpdater;
    template<class T> class ScopedQueryUpdate;

    // General queries
    void safe_glGetBooleanv(GLenum param, GLboolean *val);
    void safe_glGetFloatv(GLenum param, GLfloat *val);
    void safe_glGetIntegerv(GLenum param, GLint *val);
    void safe_glGetInteger64v(GLenum param, GLint64 *val);
    void safe_glGetIntegeri_v(GLenum param, GLuint index, GLint *val);
    void safe_glGetInteger64i_v(GLenum param, GLuint index, GLint64 *val);
    void safe_glGetBooleani_v(GLenum param, GLuint index, GLboolean *val);

    // API implementation
    glGetError_client_proc_t    m_glGetError_enc;
    static GLenum s_glGetError(void * self);

    glFlush_client_proc_t m_glFlush_enc;
    static void s_glFlush(void * self);

    glPixelStorei_client_proc_t m_glPixelStorei_enc;
    static void s_glPixelStorei(void *self, GLenum param, GLint value);

    glGetString_client_proc_t m_glGetString_enc;
    static const GLubyte * s_glGetString(void *self, GLenum name);

    glBindBuffer_client_proc_t m_glBindBuffer_enc;
    static void s_glBindBuffer(void *self, GLenum target, GLuint id);
    void doBindBufferEncodeCached(GLenum taret, GLuint id);

    glBufferData_client_proc_t m_glBufferData_enc;
    static void s_glBufferData(void *self, GLenum target, GLsizeiptr size, const GLvoid * data, GLenum usage);
    glBufferSubData_client_proc_t m_glBufferSubData_enc;
    static void s_glBufferSubData(void *self, GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid * data);

    glGenBuffers_client_proc_t m_glGenBuffers_enc;
    static void s_glGenBuffers(void *self, GLsizei n, GLuint * buffers);

    glDeleteBuffers_client_proc_t m_glDeleteBuffers_enc;
    static void s_glDeleteBuffers(void *self, GLsizei n, const GLuint * buffers);

    glDrawArrays_client_proc_t m_glDrawArrays_enc;
    static void s_glDrawArrays(void *self, GLenum mode, GLint first, GLsizei count);

    glDrawElements_client_proc_t m_glDrawElements_enc;
    static void s_glDrawElements(void *self, GLenum mode, GLsizei count, GLenum type, const void *indices);

    glDrawArraysNullAEMU_client_proc_t m_glDrawArraysNullAEMU_enc;
    static void s_glDrawArraysNullAEMU(void *self, GLenum mode, GLint first, GLsizei count);

    glDrawElementsNullAEMU_client_proc_t m_glDrawElementsNullAEMU_enc;
    static void s_glDrawElementsNullAEMU(void *self, GLenum mode, GLsizei count, GLenum type, const void *indices);

    glGetIntegerv_client_proc_t m_glGetIntegerv_enc;
    static void s_glGetIntegerv(void *self, GLenum pname, GLint *ptr);

    glGetFloatv_client_proc_t m_glGetFloatv_enc;
    static void s_glGetFloatv(void *self, GLenum pname, GLfloat *ptr);

    glGetBooleanv_client_proc_t m_glGetBooleanv_enc;
    static void s_glGetBooleanv(void *self, GLenum pname, GLboolean *ptr);

    glGetInteger64v_client_proc_t m_glGetInteger64v_enc;
    static void s_glGetInteger64v(void* self, GLenum param, GLint64* val);

    glGetBooleani_v_client_proc_t m_glGetBooleani_v_enc;
    static void s_glGetBooleani_v(void* self, GLenum param, GLuint index, GLboolean* val);

    glVertexAttribPointer_client_proc_t m_glVertexAttribPointer_enc;
    static void s_glVertexAttribPointer(void *self, GLuint indx, GLint size, GLenum type,
                                        GLboolean normalized, GLsizei stride, const GLvoid * ptr);

    glEnableVertexAttribArray_client_proc_t m_glEnableVertexAttribArray_enc;
    static void s_glEnableVertexAttribArray(void *self, GLuint index);

    glDisableVertexAttribArray_client_proc_t m_glDisableVertexAttribArray_enc;
    static void s_glDisableVertexAttribArray(void *self, GLuint index);

    glGetVertexAttribiv_client_proc_t m_glGetVertexAttribiv_enc;
    static void s_glGetVertexAttribiv(void *self, GLuint index, GLenum pname, GLint *params);

    glGetVertexAttribfv_client_proc_t m_glGetVertexAttribfv_enc;
    static void s_glGetVertexAttribfv(void *self, GLuint index, GLenum pname, GLfloat *params);

    glGetVertexAttribPointerv_client_proc_t m_glGetVertexAttribPointerv_enc;
    static void s_glGetVertexAttribPointerv(void *self, GLuint index, GLenum pname, GLvoid **pointer);

    static void s_glShaderBinary(void *self, GLsizei n, const GLuint *shaders, GLenum binaryformat, const void* binary, GLsizei length);

    static void s_glShaderSource(void *self, GLuint shader, GLsizei count, const GLchar * const *string, const GLint *length);

    static void s_glFinish(void *self);

    glLinkProgram_client_proc_t m_glLinkProgram_enc;
    static void s_glLinkProgram(void *self, GLuint program);

    glDeleteProgram_client_proc_t m_glDeleteProgram_enc;
    static void s_glDeleteProgram(void * self, GLuint program);

    glGetUniformiv_client_proc_t m_glGetUniformiv_enc;
    static void s_glGetUniformiv(void *self, GLuint program, GLint location , GLint *params);

    glGetUniformfv_client_proc_t m_glGetUniformfv_enc;
    static void s_glGetUniformfv(void *self, GLuint program, GLint location , GLfloat *params);

    glCreateProgram_client_proc_t m_glCreateProgram_enc;
    static GLuint s_glCreateProgram(void *self);

    glCreateShader_client_proc_t m_glCreateShader_enc;
    static GLuint s_glCreateShader(void *self, GLenum shaderType);

    glDeleteShader_client_proc_t m_glDeleteShader_enc;
    static void s_glDeleteShader(void *self, GLuint shader);

    glAttachShader_client_proc_t m_glAttachShader_enc;
    static void s_glAttachShader(void *self, GLuint program, GLuint shader);

    glDetachShader_client_proc_t m_glDetachShader_enc;
    static void s_glDetachShader(void *self, GLuint program, GLuint shader);

    glGetAttachedShaders_client_proc_t m_glGetAttachedShaders_enc;
    static void s_glGetAttachedShaders(void *self, GLuint program, GLsizei maxCount,
            GLsizei* count, GLuint* shaders);

    glGetShaderSource_client_proc_t m_glGetShaderSource_enc;
    static void s_glGetShaderSource(void *self, GLuint shader, GLsizei bufsize,
            GLsizei* length, GLchar* source);

    glGetShaderInfoLog_client_proc_t m_glGetShaderInfoLog_enc;
    static void s_glGetShaderInfoLog(void *self,GLuint shader,
            GLsizei bufsize, GLsizei* length, GLchar* infolog);

    glGetProgramInfoLog_client_proc_t m_glGetProgramInfoLog_enc;
    static void s_glGetProgramInfoLog(void *self,GLuint program,
            GLsizei bufsize, GLsizei* length, GLchar* infolog);

    glGetUniformLocation_client_proc_t m_glGetUniformLocation_enc;
    static int s_glGetUniformLocation(void *self, GLuint program, const GLchar *name);
    glUseProgram_client_proc_t m_glUseProgram_enc;

    glUniform1f_client_proc_t m_glUniform1f_enc;
    glUniform1fv_client_proc_t m_glUniform1fv_enc;
    glUniform1i_client_proc_t m_glUniform1i_enc;
    glUniform1iv_client_proc_t m_glUniform1iv_enc;
    glUniform2f_client_proc_t m_glUniform2f_enc;
    glUniform2fv_client_proc_t m_glUniform2fv_enc;
    glUniform2i_client_proc_t m_glUniform2i_enc;
    glUniform2iv_client_proc_t m_glUniform2iv_enc;
    glUniform3f_client_proc_t m_glUniform3f_enc;
    glUniform3fv_client_proc_t m_glUniform3fv_enc;
    glUniform3i_client_proc_t m_glUniform3i_enc;
    glUniform3iv_client_proc_t m_glUniform3iv_enc;
    glUniform4f_client_proc_t m_glUniform4f_enc;
    glUniform4fv_client_proc_t m_glUniform4fv_enc;
    glUniform4i_client_proc_t m_glUniform4i_enc;
    glUniform4iv_client_proc_t m_glUniform4iv_enc;
    glUniformMatrix2fv_client_proc_t m_glUniformMatrix2fv_enc;
    glUniformMatrix3fv_client_proc_t m_glUniformMatrix3fv_enc;
    glUniformMatrix4fv_client_proc_t m_glUniformMatrix4fv_enc;

    static void s_glUseProgram(void *self, GLuint program);
	static void s_glUniform1f(void *self , GLint location, GLfloat x);
	static void s_glUniform1fv(void *self , GLint location, GLsizei count, const GLfloat* v);
	static void s_glUniform1i(void *self , GLint location, GLint x);
	static void s_glUniform1iv(void *self , GLint location, GLsizei count, const GLint* v);
	static void s_glUniform2f(void *self , GLint location, GLfloat x, GLfloat y);
	static void s_glUniform2fv(void *self , GLint location, GLsizei count, const GLfloat* v);
	static void s_glUniform2i(void *self , GLint location, GLint x, GLint y);
	static void s_glUniform2iv(void *self , GLint location, GLsizei count, const GLint* v);
	static void s_glUniform3f(void *self , GLint location, GLfloat x, GLfloat y, GLfloat z);
	static void s_glUniform3fv(void *self , GLint location, GLsizei count, const GLfloat* v);
	static void s_glUniform3i(void *self , GLint location, GLint x, GLint y, GLint z);
	static void s_glUniform3iv(void *self , GLint location, GLsizei count, const GLint* v);
	static void s_glUniform4f(void *self , GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
	static void s_glUniform4fv(void *self , GLint location, GLsizei count, const GLfloat* v);
	static void s_glUniform4i(void *self , GLint location, GLint x, GLint y, GLint z, GLint w);
	static void s_glUniform4iv(void *self , GLint location, GLsizei count, const GLint* v);
	static void s_glUniformMatrix2fv(void *self , GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
	static void s_glUniformMatrix3fv(void *self , GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
	static void s_glUniformMatrix4fv(void *self , GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

    glActiveTexture_client_proc_t m_glActiveTexture_enc;
    glBindTexture_client_proc_t m_glBindTexture_enc;
    glDeleteTextures_client_proc_t m_glDeleteTextures_enc;
    glGetTexParameterfv_client_proc_t m_glGetTexParameterfv_enc;
    glGetTexParameteriv_client_proc_t m_glGetTexParameteriv_enc;
    glTexParameterf_client_proc_t m_glTexParameterf_enc;
    glTexParameterfv_client_proc_t m_glTexParameterfv_enc;
    glTexParameteri_client_proc_t m_glTexParameteri_enc;
    glTexParameteriv_client_proc_t m_glTexParameteriv_enc;
    glTexImage2D_client_proc_t m_glTexImage2D_enc;
    glTexSubImage2D_client_proc_t m_glTexSubImage2D_enc;
    glCopyTexImage2D_client_proc_t m_glCopyTexImage2D_enc;
    glTexBufferOES_client_proc_t m_glTexBufferOES_enc;
    glTexBufferRangeOES_client_proc_t m_glTexBufferRangeOES_enc;
    glTexBufferEXT_client_proc_t m_glTexBufferEXT_enc;
    glTexBufferRangeEXT_client_proc_t m_glTexBufferRangeEXT_enc;

    glEnableiEXT_client_proc_t m_glEnableiEXT_enc;
    glDisableiEXT_client_proc_t m_glDisableiEXT_enc;
    glBlendEquationiEXT_client_proc_t m_glBlendEquationiEXT_enc;
    glBlendEquationSeparateiEXT_client_proc_t m_glBlendEquationSeparateiEXT_enc;
    glBlendFunciEXT_client_proc_t m_glBlendFunciEXT_enc;
    glBlendFuncSeparateiEXT_client_proc_t m_glBlendFuncSeparateiEXT_enc;
    glColorMaskiEXT_client_proc_t m_glColorMaskiEXT_enc;
    glIsEnablediEXT_client_proc_t m_glIsEnablediEXT_enc;


    static void s_glActiveTexture(void* self, GLenum texture);
    static void s_glBindTexture(void* self, GLenum target, GLuint texture);
    static void s_glDeleteTextures(void* self, GLsizei n, const GLuint* textures);
    static void s_glGetTexParameterfv(void* self, GLenum target, GLenum pname, GLfloat* params);
    static void s_glGetTexParameteriv(void* self, GLenum target, GLenum pname, GLint* params);
    static void s_glTexParameterf(void* self, GLenum target, GLenum pname, GLfloat param);
    static void s_glTexParameterfv(void* self, GLenum target, GLenum pname, const GLfloat* params);
    static void s_glTexParameteri(void* self, GLenum target, GLenum pname, GLint param);
    static void s_glTexParameteriv(void* self, GLenum target, GLenum pname, const GLint* params);
    static void s_glTexImage2D(void* self, GLenum target, GLint level, GLint internalformat,
            GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type,
            const GLvoid* pixels);
    static void s_glTexSubImage2D(void* self, GLenum target, GLint level, GLint xoffset,
            GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type,
            const GLvoid* pixels);
    static void s_glCopyTexImage2D(void* self, GLenum target, GLint level, GLenum internalformat,
            GLint x, GLint y, GLsizei width, GLsizei height, GLint border);

    static bool validateTexBuffer(void* self, GLenum target, GLenum internalFormat, GLuint buffer);
    static bool validateTexBufferRange(void* self, GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size);

    static void s_glTexBufferOES(void* self, GLenum target, GLenum internalFormat, GLuint buffer);
    static void s_glTexBufferRangeOES(void* self, GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size);

    static void s_glTexBufferEXT(void* self, GLenum target, GLenum internalFormat, GLuint buffer);
    static void s_glTexBufferRangeEXT(void* self, GLenum target, GLenum internalFormat, GLuint buffer, GLintptr offset, GLsizeiptr size);


    static bool validateAllowedEnablei(void* self, GLenum cap, GLuint index);
    static void s_glEnableiEXT(void* self, GLenum cap, GLuint index);
    static void s_glDisableiEXT(void* self, GLenum cap, GLuint index);
    static void s_glBlendEquationiEXT(void* self, GLuint buf, GLenum mode);
    static void s_glBlendEquationSeparateiEXT(void* self, GLuint buf, GLenum modeRGB, GLenum modeAlpha);
    static void s_glBlendFunciEXT(void* self, GLuint buf, GLenum sfactor, GLenum dfactor);
    static void s_glBlendFuncSeparateiEXT(void* self, GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
    static void s_glColorMaskiEXT(void* self, GLuint buf, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
    static GLboolean s_glIsEnablediEXT(void* self, GLenum cap, GLuint index);

    glGenRenderbuffers_client_proc_t m_glGenRenderbuffers_enc;
    static void s_glGenRenderbuffers(void* self, GLsizei n, GLuint* renderbuffers);
    glDeleteRenderbuffers_client_proc_t m_glDeleteRenderbuffers_enc;
    static void s_glDeleteRenderbuffers(void* self, GLsizei n, const GLuint* renderbuffers);

    glBindRenderbuffer_client_proc_t m_glBindRenderbuffer_enc;
    static void s_glBindRenderbuffer(void* self, GLenum target, GLuint renderbuffer);

    glRenderbufferStorage_client_proc_t m_glRenderbufferStorage_enc;
    static void s_glRenderbufferStorage(void* self, GLenum target, GLenum internalformat, GLsizei width, GLsizei height);

    glFramebufferRenderbuffer_client_proc_t m_glFramebufferRenderbuffer_enc;
    static void s_glFramebufferRenderbuffer(void* self, GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);

    glGenFramebuffers_client_proc_t m_glGenFramebuffers_enc;
    static void s_glGenFramebuffers(void* self, GLsizei n, GLuint* framebuffers);
    glDeleteFramebuffers_client_proc_t m_glDeleteFramebuffers_enc;
    static void s_glDeleteFramebuffers(void* self, GLsizei n, const GLuint* framebuffers);

    glBindFramebuffer_client_proc_t m_glBindFramebuffer_enc;
    static void s_glBindFramebuffer(void* self, GLenum target, GLuint framebuffer);

    glFramebufferParameteri_client_proc_t m_glFramebufferParameteri_enc;
    static void s_glFramebufferParameteri(void *self, GLenum target, GLenum pname, GLint param);

    glFramebufferTexture2D_client_proc_t m_glFramebufferTexture2D_enc;
    static void s_glFramebufferTexture2D(void* self, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);

    glFramebufferTexture3DOES_client_proc_t m_glFramebufferTexture3DOES_enc;
    static void s_glFramebufferTexture3DOES(void*self, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);

    glGetFramebufferAttachmentParameteriv_client_proc_t m_glGetFramebufferAttachmentParameteriv_enc;
    static void s_glGetFramebufferAttachmentParameteriv(void* self, GLenum target, GLenum attachment, GLenum pname, GLint* params);

    glCheckFramebufferStatus_client_proc_t m_glCheckFramebufferStatus_enc;
    static GLenum s_glCheckFramebufferStatus(void* self,
            GLenum target);

    // GLES 3.0-specific custom encoders

    // VAO (+ ES 2 extension)
    glGenVertexArrays_client_proc_t m_glGenVertexArrays_enc;
    glDeleteVertexArrays_client_proc_t m_glDeleteVertexArrays_enc;
    glBindVertexArray_client_proc_t m_glBindVertexArray_enc;
    glGenVertexArraysOES_client_proc_t m_glGenVertexArraysOES_enc;
    glDeleteVertexArraysOES_client_proc_t m_glDeleteVertexArraysOES_enc;
    glBindVertexArrayOES_client_proc_t m_glBindVertexArrayOES_enc;
    static void s_glGenVertexArrays(void *self, GLsizei n, GLuint* arrays);
    static void s_glDeleteVertexArrays(void *self , GLsizei n, const GLuint* arrays);
    static void s_glBindVertexArray(void *self , GLuint array);

    // Mapped buffers
    static void* s_glMapBufferOES(void* self, GLenum target, GLenum access);
    static GLboolean s_glUnmapBufferOES(void* self, GLenum target);
    static void* s_glMapBufferRange(void* self, GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
    static void* s_glMapBufferRangeAEMUImpl(GL2Encoder* ctx, GLenum target,
                                            GLintptr offset, GLsizeiptr length,
                                            GLbitfield access, gfxstream::guest::BufferData* buf);
    static GLboolean s_glUnmapBuffer(void* self, GLenum target);
    static void s_glFlushMappedBufferRange(void* self, GLenum target, GLintptr offset, GLsizeiptr length);

    // Custom encodes for 2D compressed textures b/c we need to account for
    // nonzero GL_PIXEL_UNPACK_BUFFER
    glCompressedTexImage2D_client_proc_t m_glCompressedTexImage2D_enc;
    static void s_glCompressedTexImage2D(void* self, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data);
    glCompressedTexSubImage2D_client_proc_t m_glCompressedTexSubImage2D_enc;
    static void s_glCompressedTexSubImage2D(void* self, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid* data);

    // Indexed, range bind
    glBindBufferRange_client_proc_t m_glBindBufferRange_enc;
    static void s_glBindBufferRange(void *self , GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);

    glBindBufferBase_client_proc_t m_glBindBufferBase_enc;
    static void s_glBindBufferBase(void *self , GLenum target, GLuint index, GLuint buffer);

    // TODO:
    // VertexAttrib(I)Pointer
    // VertexAttribBinding
    // BindVertexBuffer
    // depend on other state as well
    enum IndexedBufferBindOp {
        BindBufferBase,
        BindBufferRange,
        // TODO
        // VertexAttribPointer,
        // VertexAttribIPointer,
        // VertexAttribBinding,
        // VertexAttribFormat,
        // BindVertexBuffer,
    };
    void doIndexedBufferBindEncodeCached(IndexedBufferBindOp op, GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, GLintptr stride, GLintptr effectiveStride);

    glCopyBufferSubData_client_proc_t m_glCopyBufferSubData_enc;
    static void s_glCopyBufferSubData(void *self , GLenum readtarget, GLenum writetarget, GLintptr readoffset, GLintptr writeoffset, GLsizeiptr size);

    glGetBufferParameteriv_client_proc_t m_glGetBufferParameteriv_enc;
    static void s_glGetBufferParameteriv(void* self, GLenum target, GLenum pname, GLint* params);

    glGetBufferParameteri64v_client_proc_t m_glGetBufferParameteri64v_enc;
    static void s_glGetBufferParameteri64v(void* self, GLenum target, GLenum pname, GLint64* params);

    glGetBufferPointerv_client_proc_t m_glGetBufferPointerv_enc;
    static void s_glGetBufferPointerv(void* self, GLenum target, GLenum pname, GLvoid** params);

    glGetUniformIndices_client_proc_t m_glGetUniformIndices_enc;
    static void s_glGetUniformIndices(void* self, GLuint program, GLsizei uniformCount, const GLchar ** uniformNames, GLuint* uniformIndices);

    glUniform1ui_client_proc_t m_glUniform1ui_enc;
    glUniform1uiv_client_proc_t m_glUniform1uiv_enc;
    glUniform2ui_client_proc_t m_glUniform2ui_enc;
    glUniform2uiv_client_proc_t m_glUniform2uiv_enc;
    glUniform3ui_client_proc_t m_glUniform3ui_enc;
    glUniform3uiv_client_proc_t m_glUniform3uiv_enc;
    glUniform4ui_client_proc_t m_glUniform4ui_enc;
    glUniform4uiv_client_proc_t m_glUniform4uiv_enc;
    glUniformMatrix2x3fv_client_proc_t m_glUniformMatrix2x3fv_enc;
    glUniformMatrix2x4fv_client_proc_t m_glUniformMatrix2x4fv_enc;
    glUniformMatrix3x2fv_client_proc_t m_glUniformMatrix3x2fv_enc;
    glUniformMatrix3x4fv_client_proc_t m_glUniformMatrix3x4fv_enc;
    glUniformMatrix4x2fv_client_proc_t m_glUniformMatrix4x2fv_enc;
    glUniformMatrix4x3fv_client_proc_t m_glUniformMatrix4x3fv_enc;

    static void s_glUniform1ui(void* self, GLint location, GLuint v0);
    static void s_glUniform2ui(void* self, GLint location, GLuint v0, GLuint v1);
    static void s_glUniform3ui(void* self, GLint location, GLuint v0, GLuint v1, GLuint v2);
    static void s_glUniform4ui(void* self, GLint location, GLint v0, GLuint v1, GLuint v2, GLuint v3);
    static void s_glUniform1uiv(void* self, GLint location, GLsizei count, const GLuint *value);
    static void s_glUniform2uiv(void* self, GLint location, GLsizei count, const GLuint *value);
    static void s_glUniform3uiv(void* self, GLint location, GLsizei count, const GLuint *value);
    static void s_glUniform4uiv(void* self, GLint location, GLsizei count, const GLuint *value);
    static void s_glUniformMatrix2x3fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glUniformMatrix3x2fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glUniformMatrix2x4fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glUniformMatrix4x2fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glUniformMatrix3x4fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glUniformMatrix4x3fv(void* self, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

    glGetUniformuiv_client_proc_t m_glGetUniformuiv_enc;
    static void s_glGetUniformuiv(void *self, GLuint program, GLint location, GLuint* params);

    glGetActiveUniformBlockiv_client_proc_t m_glGetActiveUniformBlockiv_enc;
    static void s_glGetActiveUniformBlockiv(void* self, GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint* params);

    glGetVertexAttribIiv_client_proc_t m_glGetVertexAttribIiv_enc;
    static void s_glGetVertexAttribIiv(void* self, GLuint index, GLenum pname, GLint* params);

    glGetVertexAttribIuiv_client_proc_t m_glGetVertexAttribIuiv_enc;
    static void s_glGetVertexAttribIuiv(void* self, GLuint index, GLenum pname, GLuint* params);

    static void s_glVertexAttribIPointer(void* self, GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid* pointer);

    glVertexAttribDivisor_client_proc_t m_glVertexAttribDivisor_enc;
    static void s_glVertexAttribDivisor(void* self, GLuint index, GLuint divisor);

    glRenderbufferStorageMultisample_client_proc_t m_glRenderbufferStorageMultisample_enc;
    static void s_glRenderbufferStorageMultisample(void* self, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);

    glDrawBuffers_client_proc_t m_glDrawBuffers_enc;
    static void s_glDrawBuffers(void* self, GLsizei n, const GLenum* bufs);

    glReadBuffer_client_proc_t m_glReadBuffer_enc;
    static void s_glReadBuffer(void* self, GLenum src);

    glFramebufferTextureLayer_client_proc_t m_glFramebufferTextureLayer_enc;
    static void s_glFramebufferTextureLayer(void* self, GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);

    glTexStorage2D_client_proc_t m_glTexStorage2D_enc;
    static void s_glTexStorage2D(void* self, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);

    static void s_glTransformFeedbackVaryings(void* self, GLuint program, GLsizei count, const char** varyings, GLenum bufferMode);

    glBeginTransformFeedback_client_proc_t m_glBeginTransformFeedback_enc;
    static void s_glBeginTransformFeedback(void* self, GLenum primitiveMode);

    glEndTransformFeedback_client_proc_t m_glEndTransformFeedback_enc;
    static void s_glEndTransformFeedback(void* self);

    glPauseTransformFeedback_client_proc_t m_glPauseTransformFeedback_enc;
    static void s_glPauseTransformFeedback(void* self);

    glResumeTransformFeedback_client_proc_t m_glResumeTransformFeedback_enc;
    static void s_glResumeTransformFeedback(void* self);

    glTexImage3D_client_proc_t m_glTexImage3D_enc;
    static void s_glTexImage3D(void* self, GLenum target, GLint level, GLint internalFormat,
                               GLsizei width, GLsizei height, GLsizei depth,
                               GLint border, GLenum format, GLenum type, const GLvoid* data);

    glTexSubImage3D_client_proc_t m_glTexSubImage3D_enc;
    static void s_glTexSubImage3D(void* self, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* data);

    glCompressedTexImage3D_client_proc_t m_glCompressedTexImage3D_enc;
    static void s_glCompressedTexImage3D(void* self, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid* data);
    glCompressedTexSubImage3D_client_proc_t m_glCompressedTexSubImage3D_enc;
    static void s_glCompressedTexSubImage3D(void* self, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid* data);


    glTexStorage3D_client_proc_t m_glTexStorage3D_enc;
    static void s_glTexStorage3D(void* self, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);

    glDrawArraysInstanced_client_proc_t m_glDrawArraysInstanced_enc;
    static void s_glDrawArraysInstanced(void* self, GLenum mode, GLint first, GLsizei count, GLsizei primcount);

    static void s_glDrawElementsInstanced(void* self, GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei primcount);

    glDrawRangeElements_client_proc_t m_glDrawRangeElements_enc;
    static void s_glDrawRangeElements(void* self, GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void* indices);

    static const GLubyte* s_glGetStringi(void* self, GLenum name, GLuint index);

    glGetProgramBinary_client_proc_t m_glGetProgramBinary_enc;
    static void s_glGetProgramBinary(void* self, GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary);

    glReadPixels_client_proc_t m_glReadPixels_enc;
    static void s_glReadPixels(void* self, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* pixels);

    glEnable_client_proc_t m_glEnable_enc;
    static void s_glEnable(void* self, GLenum what);
    glDisable_client_proc_t m_glDisable_enc;
    static void s_glDisable(void* self, GLenum what);

    glClearBufferiv_client_proc_t m_glClearBufferiv_enc;
    static void s_glClearBufferiv(void* self, GLenum buffer, GLint drawBuffer, const GLint* value);

    glClearBufferuiv_client_proc_t m_glClearBufferuiv_enc;
    static void s_glClearBufferuiv(void* self, GLenum buffer, GLint drawBuffer, const GLuint* value);

    glClearBufferfv_client_proc_t m_glClearBufferfv_enc;
    static void s_glClearBufferfv(void* self, GLenum buffer, GLint drawBuffer, const GLfloat* value);

    glBlitFramebuffer_client_proc_t m_glBlitFramebuffer_enc;
    static void s_glBlitFramebuffer(void* self, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

    static void s_glGetInternalformativ(void* self, GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint *params);

    glGenerateMipmap_client_proc_t m_glGenerateMipmap_enc;
    static void s_glGenerateMipmap(void* self, GLenum target);

    glBindSampler_client_proc_t m_glBindSampler_enc;
    static void s_glBindSampler(void* self, GLuint unit, GLuint sampler);

    glDeleteSamplers_client_proc_t m_glDeleteSamplers_enc;
    static void s_glDeleteSamplers(void* self, GLsizei n, const GLuint* samplers);

    static GLsync s_glFenceSync(void* self, GLenum condition, GLbitfield flags);
    static GLenum s_glClientWaitSync(void* self, GLsync wait_on, GLbitfield flags, GLuint64 timeout);
    static void s_glWaitSync(void* self, GLsync wait_on, GLbitfield flags, GLuint64 timeout);
    static void s_glDeleteSync(void* self, GLsync to_delete);
    static GLboolean s_glIsSync(void* self, GLsync sync);
    static void s_glGetSynciv(void* self, GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length, GLint *values);

    glGetIntegeri_v_client_proc_t m_glGetIntegeri_v_enc;
    static void s_glGetIntegeri_v(void* self, GLenum target, GLuint index, GLint* params);

    glGetInteger64i_v_client_proc_t m_glGetInteger64i_v_enc;
    static void s_glGetInteger64i_v(void* self, GLenum target, GLuint index, GLint64* params);

    glGetShaderiv_client_proc_t m_glGetShaderiv_enc;
    static void s_glGetShaderiv(void* self, GLuint shader, GLenum pname, GLint* params);

    // 3.1
    glActiveShaderProgram_client_proc_t m_glActiveShaderProgram_enc;
    static void s_glActiveShaderProgram(void* self, GLuint pipeline, GLuint program);
    static GLuint s_glCreateShaderProgramv(void* self, GLenum type, GLsizei count, const char** strings);

    glProgramUniform1f_client_proc_t m_glProgramUniform1f_enc;
    glProgramUniform1fv_client_proc_t m_glProgramUniform1fv_enc;
    glProgramUniform1i_client_proc_t m_glProgramUniform1i_enc;
    glProgramUniform1iv_client_proc_t m_glProgramUniform1iv_enc;
    glProgramUniform1ui_client_proc_t m_glProgramUniform1ui_enc;
    glProgramUniform1uiv_client_proc_t m_glProgramUniform1uiv_enc;
    glProgramUniform2f_client_proc_t m_glProgramUniform2f_enc;
    glProgramUniform2fv_client_proc_t m_glProgramUniform2fv_enc;
    glProgramUniform2i_client_proc_t m_glProgramUniform2i_enc;
    glProgramUniform2iv_client_proc_t m_glProgramUniform2iv_enc;
    glProgramUniform2ui_client_proc_t m_glProgramUniform2ui_enc;
    glProgramUniform2uiv_client_proc_t m_glProgramUniform2uiv_enc;
    glProgramUniform3f_client_proc_t m_glProgramUniform3f_enc;
    glProgramUniform3fv_client_proc_t m_glProgramUniform3fv_enc;
    glProgramUniform3i_client_proc_t m_glProgramUniform3i_enc;
    glProgramUniform3iv_client_proc_t m_glProgramUniform3iv_enc;
    glProgramUniform3ui_client_proc_t m_glProgramUniform3ui_enc;
    glProgramUniform3uiv_client_proc_t m_glProgramUniform3uiv_enc;
    glProgramUniform4f_client_proc_t m_glProgramUniform4f_enc;
    glProgramUniform4fv_client_proc_t m_glProgramUniform4fv_enc;
    glProgramUniform4i_client_proc_t m_glProgramUniform4i_enc;
    glProgramUniform4iv_client_proc_t m_glProgramUniform4iv_enc;
    glProgramUniform4ui_client_proc_t m_glProgramUniform4ui_enc;
    glProgramUniform4uiv_client_proc_t m_glProgramUniform4uiv_enc;
    glProgramUniformMatrix2fv_client_proc_t m_glProgramUniformMatrix2fv_enc;
    glProgramUniformMatrix2x3fv_client_proc_t m_glProgramUniformMatrix2x3fv_enc;
    glProgramUniformMatrix2x4fv_client_proc_t m_glProgramUniformMatrix2x4fv_enc;
    glProgramUniformMatrix3fv_client_proc_t m_glProgramUniformMatrix3fv_enc;
    glProgramUniformMatrix3x2fv_client_proc_t m_glProgramUniformMatrix3x2fv_enc;
    glProgramUniformMatrix3x4fv_client_proc_t m_glProgramUniformMatrix3x4fv_enc;
    glProgramUniformMatrix4fv_client_proc_t m_glProgramUniformMatrix4fv_enc;
    glProgramUniformMatrix4x2fv_client_proc_t m_glProgramUniformMatrix4x2fv_enc;
    glProgramUniformMatrix4x3fv_client_proc_t m_glProgramUniformMatrix4x3fv_enc;

    static void s_glProgramUniform1f(void* self, GLuint program, GLint location, GLfloat v0);
    static void s_glProgramUniform2f(void* self, GLuint program, GLint location, GLfloat v0, GLfloat v1);
    static void s_glProgramUniform3f(void* self, GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
    static void s_glProgramUniform4f(void* self, GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    static void s_glProgramUniform1i(void* self, GLuint program, GLint location, GLint v0);
    static void s_glProgramUniform2i(void* self, GLuint program, GLint location, GLint v0, GLint v1);
    static void s_glProgramUniform3i(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLint v2);
    static void s_glProgramUniform4i(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
    static void s_glProgramUniform1ui(void* self, GLuint program, GLint location, GLuint v0);
    static void s_glProgramUniform2ui(void* self, GLuint program, GLint location, GLint v0, GLuint v1);
    static void s_glProgramUniform3ui(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLuint v2);
    static void s_glProgramUniform4ui(void* self, GLuint program, GLint location, GLint v0, GLint v1, GLint v2, GLuint v3);
    static void s_glProgramUniform1fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value);
    static void s_glProgramUniform2fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value);
    static void s_glProgramUniform3fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value);
    static void s_glProgramUniform4fv(void* self, GLuint program, GLint location, GLsizei count, const GLfloat *value);
    static void s_glProgramUniform1iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value);
    static void s_glProgramUniform2iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value);
    static void s_glProgramUniform3iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value);
    static void s_glProgramUniform4iv(void* self, GLuint program, GLint location, GLsizei count, const GLint *value);
    static void s_glProgramUniform1uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value);
    static void s_glProgramUniform2uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value);
    static void s_glProgramUniform3uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value);
    static void s_glProgramUniform4uiv(void* self, GLuint program, GLint location, GLsizei count, const GLuint *value);
    static void s_glProgramUniformMatrix2fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix3fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix4fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix2x3fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix3x2fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix2x4fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix4x2fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix3x4fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    static void s_glProgramUniformMatrix4x3fv(void* self, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

    glProgramParameteri_client_proc_t m_glProgramParameteri_enc;
    glUseProgramStages_client_proc_t m_glUseProgramStages_enc;
    glBindProgramPipeline_client_proc_t m_glBindProgramPipeline_enc;

    static void s_glProgramParameteri(void* self, GLuint program, GLenum pname, GLint value);
    static void s_glUseProgramStages(void *self, GLuint pipeline, GLbitfield stages, GLuint program);
    static void s_glBindProgramPipeline(void *self, GLuint pipeline);

    glGetProgramResourceiv_client_proc_t m_glGetProgramResourceiv_enc;
    glGetProgramResourceIndex_client_proc_t m_glGetProgramResourceIndex_enc;
    glGetProgramResourceLocation_client_proc_t m_glGetProgramResourceLocation_enc;
    glGetProgramResourceName_client_proc_t m_glGetProgramResourceName_enc;
    glGetProgramPipelineInfoLog_client_proc_t m_glGetProgramPipelineInfoLog_enc;

    static void s_glGetProgramResourceiv(void* self, GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum * props, GLsizei bufSize, GLsizei * length, GLint * params);
    static GLuint s_glGetProgramResourceIndex(void* self, GLuint program, GLenum programInterface, const char* name);
    static GLint s_glGetProgramResourceLocation(void* self, GLuint program, GLenum programInterface, const char* name);
    static void s_glGetProgramResourceName(void* self, GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize, GLsizei* length, char* name);

    static void s_glGetProgramPipelineInfoLog(void* self, GLuint pipeline, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

    // TODO: Compute shaders:
    // make sure it's OK to put memory barriers and compute dispatch
    // on the default encoding path

    glVertexAttribFormat_client_proc_t m_glVertexAttribFormat_enc;
    glVertexAttribIFormat_client_proc_t m_glVertexAttribIFormat_enc;
    glVertexBindingDivisor_client_proc_t m_glVertexBindingDivisor_enc;
    glVertexAttribBinding_client_proc_t m_glVertexAttribBinding_enc;
    glBindVertexBuffer_client_proc_t m_glBindVertexBuffer_enc;

    static void s_glVertexAttribFormat(void* self, GLuint attribindex, GLint size, GLenum type, GLboolean normalized, GLuint relativeoffset);
    static void s_glVertexAttribIFormat(void* self, GLuint attribindex, GLint size, GLenum type, GLuint relativeoffset);
    static void s_glVertexBindingDivisor(void* self, GLuint bindingindex, GLuint divisor);
    static void s_glVertexAttribBinding(void* self, GLuint attribindex, GLuint bindingindex);
    static void s_glBindVertexBuffer(void* self, GLuint bindingindex, GLuint buffer, GLintptr offset, GLintptr stride);

    // Indirect draws
    static void s_glDrawArraysIndirect(void* self, GLenum mode, const void* indirect);
    static void s_glDrawElementsIndirect(void* self, GLenum mode, GLenum type, const void* indirect);

    // Multisampled textures
    glTexStorage2DMultisample_client_proc_t m_glTexStorage2DMultisample_enc;
    static void s_glTexStorage2DMultisample(void* self, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);

    static GLenum s_glGetGraphicsResetStatusEXT(void* self);
    static void s_glReadnPixelsEXT(void* self, GLint x, GLint y, GLsizei width,
            GLsizei height, GLenum format, GLenum type, GLsizei bufSize,
            GLvoid* pixels);
    static void s_glGetnUniformfvEXT(void *self, GLuint program, GLint location,
            GLsizei bufSize, GLfloat* params);
    static void s_glGetnUniformivEXT(void *self, GLuint program, GLint location,
            GLsizei bufSize, GLint* params);

    // Invalidate framebuffer
    static void s_glInvalidateFramebuffer(void* self, GLenum target, GLsizei numAttachments, const GLenum *attachments);
    static void s_glInvalidateSubFramebuffer(void* self, GLenum target, GLsizei numAttachments, const GLenum *attachments, GLint x, GLint y, GLsizei width, GLsizei height);

    glInvalidateFramebuffer_client_proc_t m_glInvalidateFramebuffer_enc;
    glInvalidateSubFramebuffer_client_proc_t m_glInvalidateSubFramebuffer_enc;;

    // Dispatch compute
    static void s_glDispatchCompute(void* self, GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
    static void s_glDispatchComputeIndirect(void* self, GLintptr indirect);

    glDispatchCompute_client_proc_t m_glDispatchCompute_enc;
    glDispatchComputeIndirect_client_proc_t m_glDispatchComputeIndirect_enc;

    // State tracking for transform feedbacks, samplers, and query objects
    static void s_glGenTransformFeedbacks(void* self, GLsizei n, GLuint* ids);
    static void s_glDeleteTransformFeedbacks(void* self, GLsizei n, const GLuint* ids);
    static void s_glGenSamplers(void* self, GLsizei n, GLuint* ids);
    static void s_glGenQueries(void* self, GLsizei n, GLuint* ids);
    static void s_glDeleteQueries(void* self, GLsizei n, const GLuint* ids);

    glGenTransformFeedbacks_client_proc_t m_glGenTransformFeedbacks_enc;
    glDeleteTransformFeedbacks_client_proc_t m_glDeleteTransformFeedbacks_enc;
    glGenSamplers_client_proc_t m_glGenSamplers_enc;
    glGenQueries_client_proc_t m_glGenQueries_enc;
    glDeleteQueries_client_proc_t m_glDeleteQueries_enc;

    static void s_glBindTransformFeedback(void* self, GLenum target, GLuint id);
    static void s_glBeginQuery(void* self, GLenum target, GLuint query);
    static void s_glEndQuery(void* self, GLenum target);

    glBindTransformFeedback_client_proc_t m_glBindTransformFeedback_enc;
    glBeginQuery_client_proc_t m_glBeginQuery_enc;
    glEndQuery_client_proc_t m_glEndQuery_enc;

    static void s_glClear(void* self, GLbitfield mask);
    glClear_client_proc_t m_glClear_enc;

    static void s_glClearBufferfi(void* self, GLenum buffer, GLint drawBuffer, float depth, int stencil);
    glClearBufferfi_client_proc_t m_glClearBufferfi_enc;

    static void s_glCopyTexSubImage2D(void *self , GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
    glCopyTexSubImage2D_client_proc_t m_glCopyTexSubImage2D_enc;

    static void s_glCopyTexSubImage3D(void *self , GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height);
    glCopyTexSubImage3D_client_proc_t m_glCopyTexSubImage3D_enc;

    static void s_glCompileShader(void* self, GLuint shader);
    glCompileShader_client_proc_t m_glCompileShader_enc;

    static void s_glValidateProgram(void* self, GLuint program);
    glValidateProgram_client_proc_t m_glValidateProgram_enc;

    static void s_glProgramBinary(void *self , GLuint program, GLenum binaryFormat, const void* binary, GLsizei length);
    glProgramBinary_client_proc_t m_glProgramBinary_enc;

    static void s_glGetSamplerParameterfv(void *self, GLuint sampler, GLenum pname, GLfloat* params);
    static void s_glGetSamplerParameteriv(void *self, GLuint sampler, GLenum pname, GLint* params);
    glGetSamplerParameterfv_client_proc_t m_glGetSamplerParameterfv_enc;
    glGetSamplerParameteriv_client_proc_t m_glGetSamplerParameteriv_enc;

    static void s_glSamplerParameterf(void *self , GLuint sampler, GLenum pname, GLfloat param);
    static void s_glSamplerParameteri(void *self , GLuint sampler, GLenum pname, GLint param);
    static void s_glSamplerParameterfv(void *self , GLuint sampler, GLenum pname, const GLfloat* params);
    static void s_glSamplerParameteriv(void *self , GLuint sampler, GLenum pname, const GLint* params);

    glSamplerParameterf_client_proc_t m_glSamplerParameterf_enc;
    glSamplerParameteri_client_proc_t m_glSamplerParameteri_enc;
    glSamplerParameterfv_client_proc_t m_glSamplerParameterfv_enc;
    glSamplerParameteriv_client_proc_t m_glSamplerParameteriv_enc;

    static int s_glGetAttribLocation(void *self , GLuint program, const GLchar* name);
    glGetAttribLocation_client_proc_t m_glGetAttribLocation_enc;

    static void s_glBindAttribLocation(void *self , GLuint program, GLuint index, const GLchar* name);
    static void s_glUniformBlockBinding(void *self , GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
    static void s_glGetTransformFeedbackVarying(void *self , GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLsizei* size, GLenum* type, char* name);
    static void s_glScissor(void *self , GLint x, GLint y, GLsizei width, GLsizei height);
    static void s_glDepthFunc(void *self , GLenum func);
    static void s_glViewport(void *self , GLint x, GLint y, GLsizei width, GLsizei height);
    static void s_glStencilFunc(void *self , GLenum func, GLint ref, GLuint mask);
    static void s_glStencilFuncSeparate(void *self , GLenum face, GLenum func, GLint ref, GLuint mask);
    static void s_glStencilOp(void *self , GLenum fail, GLenum zfail, GLenum zpass);
    static void s_glStencilOpSeparate(void *self , GLenum face, GLenum fail, GLenum zfail, GLenum zpass);
    static void s_glStencilMaskSeparate(void *self , GLenum face, GLuint mask);
    static void s_glBlendEquation(void *self , GLenum mode);
    static void s_glBlendEquationSeparate(void *self , GLenum modeRGB, GLenum modeAlpha);
    static void s_glBlendFunc(void *self , GLenum sfactor, GLenum dfactor);
    static void s_glBlendFuncSeparate(void *self , GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
    static void s_glCullFace(void *self , GLenum mode);
    static void s_glFrontFace(void *self , GLenum mode);
    static void s_glLineWidth(void *self , GLfloat width);
    static void s_glVertexAttrib1f(void *self , GLuint indx, GLfloat x);
    static void s_glVertexAttrib2f(void *self , GLuint indx, GLfloat x, GLfloat y);
    static void s_glVertexAttrib3f(void *self , GLuint indx, GLfloat x, GLfloat y, GLfloat z);
    static void s_glVertexAttrib4f(void *self , GLuint indx, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    static void s_glVertexAttrib1fv(void *self , GLuint indx, const GLfloat* values);
    static void s_glVertexAttrib2fv(void *self , GLuint indx, const GLfloat* values);
    static void s_glVertexAttrib3fv(void *self , GLuint indx, const GLfloat* values);
    static void s_glVertexAttrib4fv(void *self , GLuint indx, const GLfloat* values);
    static void s_glVertexAttribI4i(void *self , GLuint index, GLint v0, GLint v1, GLint v2, GLint v3);
    static void s_glVertexAttribI4ui(void *self , GLuint index, GLuint v0, GLuint v1, GLuint v2, GLuint v3);
    static void s_glVertexAttribI4iv(void *self , GLuint index, const GLint* v);
    static void s_glVertexAttribI4uiv(void *self , GLuint index, const GLuint* v);

    static void s_glGetShaderPrecisionFormat(void *self , GLenum shadertype, GLenum precisiontype, GLint* range, GLint* precision);
    static void s_glGetProgramiv(void *self , GLuint program, GLenum pname, GLint* params);
    static void s_glGetActiveUniform(void *self , GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name);
    static void s_glGetActiveUniformsiv(void *self , GLuint program, GLsizei uniformCount, const GLuint* uniformIndices, GLenum pname, GLint* params);
    static void s_glGetActiveUniformBlockName(void *self , GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei* length, GLchar* uniformBlockName);
    static void s_glGetActiveAttrib(void *self , GLuint program, GLuint index, GLsizei bufsize, GLsizei* length, GLint* size, GLenum* type, GLchar* name);
    static void s_glGetRenderbufferParameteriv(void *self , GLenum target, GLenum pname, GLint* params);
    static void s_glGetQueryiv(void *self , GLenum target, GLenum pname, GLint* params);
    static void s_glGetQueryObjectuiv(void *self , GLuint query, GLenum pname, GLuint* params);
    static GLboolean s_glIsEnabled(void *self , GLenum cap);
    static void s_glHint(void *self , GLenum target, GLenum mode);
    static GLint s_glGetFragDataLocation (void *self , GLuint program, const char* name);

    static void s_glStencilMask(void* self, GLuint mask);
    static void s_glClearStencil(void* self, int v);

#define LIST_REMAINING_FUNCTIONS_FOR_VALIDATION(f) \
    f(glBindAttribLocation) \
    f(glUniformBlockBinding) \
    f(glGetTransformFeedbackVarying) \
    f(glScissor) \
    f(glDepthFunc) \
    f(glViewport) \
    f(glStencilFunc) \
    f(glStencilFuncSeparate) \
    f(glStencilOp) \
    f(glStencilOpSeparate) \
    f(glStencilMaskSeparate) \
    f(glBlendEquation) \
    f(glBlendEquationSeparate) \
    f(glBlendFunc) \
    f(glBlendFuncSeparate) \
    f(glCullFace) \
    f(glFrontFace) \
    f(glLineWidth) \
    f(glVertexAttrib1f) \
    f(glVertexAttrib2f) \
    f(glVertexAttrib3f) \
    f(glVertexAttrib4f) \
    f(glVertexAttrib1fv) \
    f(glVertexAttrib2fv) \
    f(glVertexAttrib3fv) \
    f(glVertexAttrib4fv) \
    f(glVertexAttribI4i) \
    f(glVertexAttribI4ui) \
    f(glVertexAttribI4iv) \
    f(glVertexAttribI4uiv) \
    f(glGetShaderPrecisionFormat) \
    f(glGetProgramiv) \
    f(glGetActiveUniform) \
    f(glGetActiveUniformsiv) \
    f(glGetActiveUniformBlockName) \
    f(glGetActiveAttrib) \
    f(glGetRenderbufferParameteriv) \
    f(glGetQueryiv) \
    f(glGetQueryObjectuiv) \
    f(glIsEnabled) \
    f(glHint) \
    f(glGetFragDataLocation) \
    f(glStencilMask) \
    f(glClearStencil) \

#define DECLARE_CLIENT_ENCODER_PROC(n) \
    n##_client_proc_t m_##n##_enc;

    LIST_REMAINING_FUNCTIONS_FOR_VALIDATION(DECLARE_CLIENT_ENCODER_PROC)


public:
    glEGLImageTargetTexture2DOES_client_proc_t m_glEGLImageTargetTexture2DOES_enc;

};
#endif
