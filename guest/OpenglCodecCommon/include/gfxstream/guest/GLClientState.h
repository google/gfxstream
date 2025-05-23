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
#ifndef _GL_CLIENT_STATE_H_
#define _GL_CLIENT_STATE_H_

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "StateTrackingSupport.h"
#include "TextureSharedData.h"
#include "codec_defs.h"

namespace gfxstream {
namespace guest {

// Caps of host driver that make it easy to validate stuff
struct HostDriverCaps {
    // ES 2
    int max_vertex_attribs;
    int max_combined_texture_image_units;
    int max_color_attachments;

    int max_texture_size;
    int max_texture_size_cube_map;
    int max_renderbuffer_size;

    // ES 3.0
    int max_draw_buffers;

    int ubo_offset_alignment;
    int max_uniform_buffer_bindings;
    int max_transform_feedback_separate_attribs;

    int max_texture_size_3d;
    int max_array_texture_layers;

    // ES 3.1
    int max_atomic_counter_buffer_bindings;
    int max_shader_storage_buffer_bindings;
    int max_vertex_attrib_bindings;
    int max_vertex_attrib_stride;
    int ssbo_offset_alignment;
};

// Tracking framebuffer objects:
// which framebuffer is bound,
// and which texture names
// are currently bound to which attachment points.
struct FboProps {
    GLuint name;
    bool previouslyBound;
    bool completenessDirty;
    GLenum cachedCompleteness;
    std::vector<std::shared_ptr<TextureRec>> colorAttachmenti_textures;
    std::vector<GLint> colorAttachmenti_texture_levels;
    std::vector<GLint> colorAttachmenti_texture_layers;

    GLint depthAttachment_texture_level;
    GLint depthAttachment_texture_layer;
    GLint stencilAttachment_texture_level;
    GLint stencilAttachment_texture_layer;

    std::shared_ptr<TextureRec> depthAttachment_texture;
    std::shared_ptr<TextureRec> stencilAttachment_texture;
    std::shared_ptr<TextureRec> depthstencilAttachment_texture;

    std::vector<bool> colorAttachmenti_hasTex;
    bool depthAttachment_hasTexObj;
    bool stencilAttachment_hasTexObj;
    bool depthstencilAttachment_hasTexObj;

    std::vector<std::shared_ptr<RboProps>> colorAttachmenti_rbos;
    std::shared_ptr<RboProps> depthAttachment_rbo = 0;
    std::shared_ptr<RboProps> stencilAttachment_rbo = 0;
    std::shared_ptr<RboProps> depthstencilAttachment_rbo = 0;

    std::vector<bool> colorAttachmenti_hasRbo;
    bool depthAttachment_hasRbo = false;
    bool stencilAttachment_hasRbo = false;
    bool depthstencilAttachment_hasRbo = false;

    GLuint defaultWidth;
    GLuint defaultHeight;
};

// Enum for describing whether a framebuffer attachment
// is a texture or renderbuffer.
enum FboAttachmentType {
    FBO_ATTACHMENT_RENDERBUFFER = 0,
    FBO_ATTACHMENT_TEXTURE = 1,
    FBO_ATTACHMENT_NONE = 2
};

// Tracking FBO format
struct FboFormatInfo {
    FboAttachmentType type;
    GLenum rb_format;
    GLsizei rb_multisamples;
    bool rb_external;

    GLint tex_internalformat;
    GLenum tex_format;
    GLenum tex_type;
    GLsizei tex_multisamples;
    GLint tex_level;
    GLint tex_layer;
    bool tex_external;
};

class GLClientState {
public:
    // TODO: Unify everything in here
    typedef enum {
        Buffer,
        TransformFeedback,
        Sampler,
        Query,
    } ObjectType;

    typedef enum {
        VERTEX_LOCATION = 0,
        NORMAL_LOCATION = 1,
        COLOR_LOCATION = 2,
        POINTSIZE_LOCATION = 3,
        TEXCOORD0_LOCATION = 4,
        TEXCOORD1_LOCATION = 5,
        TEXCOORD2_LOCATION = 6,
        TEXCOORD3_LOCATION = 7,
        TEXCOORD4_LOCATION = 8,
        TEXCOORD5_LOCATION = 9,
        TEXCOORD6_LOCATION = 10,
        TEXCOORD7_LOCATION = 11,
        MATRIXINDEX_LOCATION = 12,
        WEIGHT_LOCATION = 13,
        LAST_LOCATION = 14
    } StateLocation;

    typedef struct {
        GLint enabled;
        GLint size;
        GLenum type;
        GLsizei stride;
        void *data;
        GLuint reloffset;
        GLuint bufferObject;
        GLenum glConst;
        unsigned int elementSize;
        bool enableDirty;  // true if any enable state has changed since last draw
        bool normalized;
        GLuint divisor;
        bool isInt;
        int bindingindex;
    } VertexAttribState;

    struct BufferBinding {
        GLintptr offset;
        GLintptr stride;
        GLintptr effectiveStride;
        GLsizeiptr size;
        GLuint buffer;
        GLuint divisor;
        GLint vertexAttribLoc;
    };

    typedef std::vector<VertexAttribState> VertexAttribStateVector;
    typedef std::vector<BufferBinding> VertexAttribBindingVector;

    struct VAOState {
        VAOState(GLuint ibo, int nLoc, int nBindings) :
            attribState(nLoc),
            bindingState(nBindings),
            element_array_buffer_binding(ibo),
            element_array_buffer_binding_lastEncode(ibo) { }
        VertexAttribStateVector attribState;
        VertexAttribBindingVector bindingState;
        GLuint element_array_buffer_binding;
        GLuint element_array_buffer_binding_lastEncode;
        int attributesNeedingUpdateForDraw[CODEC_MAX_VERTEX_ATTRIBUTES];
        int numAttributesNeedingUpdateForDraw;
    };

    typedef std::map<GLuint, VAOState> VAOStateMap;
    struct VAOStateRef {
        VAOStateRef() { }
        VAOStateRef(
                VAOStateMap::iterator iter) : it(iter) { }
        VAOState& vaoState() { return it->second; }
        VertexAttribState& operator[](size_t k) { return it->second.attribState[k]; }
        BufferBinding& bufferBinding(size_t k) { return it->second.bindingState[k]; }
        VertexAttribBindingVector& bufferBindings() { return it->second.bindingState; }
        const VertexAttribBindingVector& bufferBindings_const() const { return it->second.bindingState; }
        GLuint vaoId() const { return it->first; }
        GLuint& iboId() { return it->second.element_array_buffer_binding; }
        GLuint& iboIdLastEncode() { return it->second.element_array_buffer_binding_lastEncode; }
        VAOStateMap::iterator it;
    };

    typedef struct {
        int unpack_alignment;

        int unpack_row_length;
        int unpack_image_height;
        int unpack_skip_pixels;
        int unpack_skip_rows;
        int unpack_skip_images;

        int pack_alignment;

        int pack_row_length;
        int pack_skip_pixels;
        int pack_skip_rows;
    } PixelStoreState;

    enum {
        MAX_TEXTURE_UNITS = 256,
    };

public:
    GLClientState();
    GLClientState(int majorVersion, int minorVersion);
    ~GLClientState();
    int nLocations() { return CODEC_MAX_VERTEX_ATTRIBUTES; }
    const PixelStoreState *pixelStoreState() { return &m_pixelStore; }
    int setPixelStore(GLenum param, GLint value);
    GLuint currentVertexArrayObject() const { return m_currVaoState.vaoId(); }
    const VertexAttribBindingVector& currentVertexBufferBindings() const {
        return m_currVaoState.bufferBindings_const();
    }

    GLuint currentArrayVbo() { return m_arrayBuffer; }
    GLuint currentIndexVbo() { return m_currVaoState.iboId(); }
    void enable(int location, int state);
    // Vertex array objects and vertex attributes
    void addVertexArrayObjects(GLsizei n, GLuint* arrays);
    void removeVertexArrayObjects(GLsizei n, const GLuint* arrays);
    void addVertexArrayObject(GLuint name);
    void removeVertexArrayObject(GLuint name);
    void setVertexArrayObject(GLuint vao);
    bool isVertexArrayObject(GLuint vao) const;
    void setVertexAttribState(int  location, int size, GLenum type, GLboolean normalized, GLsizei stride, const void *data, bool isInt = false);
    void setVertexBindingDivisor(int bindingindex, GLuint divisor);
    const BufferBinding& getCurrAttributeBindingInfo(int attribindex);
    void setVertexAttribBinding(int attribindex, int bindingindex);
    void setVertexAttribFormat(int location, int size, GLenum type, GLboolean normalized, GLuint reloffset, bool isInt = false);
    void getVBOUsage(bool* hasClientArrays, bool* hasVBOs);
    const VertexAttribState& getState(int location);
    const VertexAttribState& getStateAndEnableDirty(int location, bool *enableChanged);
    void updateEnableDirtyArrayForDraw();
    VAOState& currentVaoState();
    int getLocation(GLenum loc);
    void setActiveTexture(int texUnit) {m_activeTexture = texUnit; };
    int getActiveTexture() const { return m_activeTexture; }

    void addBuffer(GLuint id);
    void removeBuffer(GLuint id);
    bool bufferIdExists(GLuint id) const;
    void unBindBuffer(GLuint id);

    void setBufferHostMapDirty(GLuint id, bool dirty);
    bool isBufferHostMapDirty(GLuint id) const;

    void setExistence(ObjectType type, bool exists, GLsizei count, const GLuint* ids);
    bool queryExistence(ObjectType type, GLuint id) const;
    bool samplerExists(GLuint id) const;
    bool tryBind(GLenum target, GLuint id);
    bool isBoundTargetValid(GLenum target);
    bool isQueryBound(GLenum target);
    bool isQueryObjectActive(GLuint id);
    void setLastQueryTarget(GLenum target, GLuint id);
    GLenum getLastQueryTarget(GLuint id);

    static void onFenceCreated(GLsync sync);
    static void onFenceDestroyed(GLsync sync);
    static bool fenceExists(GLsync sync);

    void setBoundPixelPackBufferDirtyForHostMap();
    void setBoundTransformFeedbackBuffersDirtyForHostMap();
    void setBoundShaderStorageBuffersDirtyForHostMap();
    void setBoundAtomicCounterBuffersDirtyForHostMap();

    int bindBuffer(GLenum target, GLuint id);
    void bindIndexedBuffer(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, GLintptr stride, GLintptr effectiveStride);
    int getMaxIndexedBufferBindings(GLenum target) const;
    bool isNonIndexedBindNoOp(GLenum target, GLuint buffer);
    bool isIndexedBindNoOp(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, GLintptr stride, GLintptr effectiveStride);

    int getMaxTextureSize() const;
    int getMaxTextureSize3D() const;
    int getMaxTextureSizeCubeMap() const;
    int getLog2MaxTextureSize() const;

    void postDraw();
    void postReadPixels();
    void postDispatchCompute();

    bool shouldSkipHostMapBuffer(GLenum target);
    void onHostMappedBuffer(GLenum target);

    int getBuffer(GLenum target);
    GLuint getLastEncodedBufferBind(GLenum target);
    void setLastEncodedBufferBind(GLenum target, GLuint id);

    size_t pixelDataSize(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, int pack) const;
    size_t pboNeededDataSize(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, int pack, int ignoreTrailing = 0) const;
    size_t clearBufferNumElts(GLenum buffer) const;
    void getPackingOffsets2D(GLsizei width, GLsizei height, GLenum format, GLenum type, int* bpp, int* startOffset, int* pixelRowSize, int* totalRowSize, int* skipRows) const;
    void getUnpackingOffsets2D(GLsizei width, GLsizei height, GLenum format, GLenum type, int* bpp, int* startOffset, int* pixelRowSize, int* totalRowSize, int* skipRows) const;
    void getUnpackingOffsets3D(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, int* bpp, int* startOffset, int* pixelRowSize, int* totalRowSize, int* pixelImageSize, int* totalImageSize, int* skipRows, int* skipImages) const;

    void setCurrentProgram(GLint program) { m_currentProgram = program; }
    void setCurrentShaderProgram(GLint program) { m_currentShaderProgram = program; }
    GLint currentProgram() const { return m_currentProgram; }
    GLint currentShaderProgram() const { return m_currentShaderProgram; }

    struct UniformBlockInfoKey {
        GLuint program;
        GLuint uniformBlockIndex;
    };
    struct UniformBlockInfoKeyCompare {
        bool operator() (const UniformBlockInfoKey& a,
                         const UniformBlockInfoKey& b) const {
            if (a.program != b.program) return a.program < b.program;
            if (a.uniformBlockIndex != b.uniformBlockIndex) return a.uniformBlockIndex < b.uniformBlockIndex;
            return false;
        }
    };
    struct UniformBlockUniformInfo {
        size_t numActiveUniforms;
    };

    typedef std::map<UniformBlockInfoKey, UniformBlockUniformInfo, UniformBlockInfoKeyCompare> UniformBlockInfoMap;
    UniformBlockInfoMap m_uniformBlockInfoMap;

    void setNumActiveUniformsInUniformBlock(GLuint program, GLuint uniformBlockIndex, GLint numActiveUniforms);
    size_t numActiveUniformsInUniformBlock(GLuint program, GLuint uniformBlockIndex) const;

    typedef std::map<GLuint, GLuint> ProgramPipelineMap;
    typedef ProgramPipelineMap::iterator ProgramPipelineIterator;
    void associateProgramWithPipeline(GLuint program, GLuint pipeline);
    ProgramPipelineIterator programPipelineBegin();
    ProgramPipelineIterator programPipelineEnd();

    /* OES_EGL_image_external
     *
     * These functions manipulate GL state which interacts with the
     * OES_EGL_image_external extension, to support client-side emulation on
     * top of host implementations that don't have it.
     *
     * Most of these calls should only be used with TEXTURE_2D or
     * TEXTURE_EXTERNAL_OES texture targets; TEXTURE_CUBE_MAP or other extension
     * targets should bypass this. An exception is bindTexture(), which should
     * see all glBindTexture() calls for any target.
     */

    // glActiveTexture(GL_TEXTURE0 + i)
    // Sets the active texture unit. Up to MAX_TEXTURE_UNITS are supported.
    GLenum setActiveTextureUnit(GLenum texture);
    GLenum getActiveTextureUnit() const;

    // glEnable(GL_TEXTURE_(2D|EXTERNAL_OES))
    void enableTextureTarget(GLenum target);

    // glDisable(GL_TEXTURE_(2D|EXTERNAL_OES))
    void disableTextureTarget(GLenum target);

    bool bindSampler(GLuint unit, GLuint sampler);
    bool isSamplerBindNoOp(GLuint unit, GLuint sampler);
    void onDeleteSamplers(GLsizei n, const GLuint* samplers);

    // Implements the target priority logic:
    // * Return GL_TEXTURE_EXTERNAL_OES if enabled, else
    // * Return GL_TEXTURE_2D if enabled, else
    // * Return the allDisabled value.
    // For some cases passing GL_TEXTURE_2D for allDisabled makes callee code
    // simpler; for other cases passing a recognizable enum like GL_ZERO or
    // GL_INVALID_ENUM is appropriate.
    GLenum getPriorityEnabledTarget(GLenum allDisabled) const;

    // glBindTexture(GL_TEXTURE_*, ...)
    // Set the target binding of the active texture unit to texture. Returns
    // GL_NO_ERROR on success or GL_INVALID_OPERATION if the texture has
    // previously been bound to a different target. If firstUse is not NULL,
    // it is set to indicate whether this is the first use of the texture.
    // For accurate error detection, bindTexture should be called for *all*
    // targets, not just 2D and EXTERNAL_OES.
    GLenum bindTexture(GLenum target, GLuint texture, GLboolean* firstUse);
    void setBoundEGLImage(GLenum target, GLeglImageOES image, int width, int height);

    // Return the texture currently bound to GL_TEXTURE_(2D|EXTERNAL_OES).
    GLuint getBoundTexture(GLenum target) const;
    // Return bound framebuffer for target
    GLuint getBoundFramebuffer(GLenum target) const;

    // Check framebuffer completeness
    GLenum checkFramebufferCompleteness(GLenum target);
    // |currentSamples|: threads through the current sample count of attachments so far,
    // for validating consistent number of samples across attachments
    GLenum checkFramebufferAttachmentCompleteness(GLenum target, GLenum attachment, int* currentSamples) const;

    // Other publicly-visible texture queries
    GLenum queryTexLastBoundTarget(GLuint name) const;
    GLenum queryTexFormat(GLuint name) const;
    GLint queryTexInternalFormat(GLuint name) const;
    GLsizei queryTexWidth(GLsizei level, GLuint name) const;
    GLsizei queryTexHeight(GLsizei level, GLuint name) const;
    GLsizei queryTexDepth(GLsizei level, GLuint name) const;
    bool queryTexEGLImageBacked(GLuint name) const;

    // For AMD GPUs, it is easy for the emulator to segfault
    // (esp. in dEQP) when a cube map is defined using glCopyTexImage2D
    // and uses GL_LUMINANCE as internal format.
    // In particular, the segfault happens when negative components of
    // cube maps are defined before positive ones,
    // This procedure checks internal state to see if we have defined
    // the positive component of a cube map already. If not, it returns
    // which positive component needs to be defined first.
    // If there is no need for the extra definition, 0 is returned.
    GLenum copyTexImageLuminanceCubeMapAMDWorkaround(GLenum target, GLint level,
                                                     GLenum internalformat);

    // Tracks the format of the currently bound texture.
    // This is to pass dEQP tests for fbo completeness.
    void setBoundTextureInternalFormat(GLenum target, GLint format);
    void setBoundTextureFormat(GLenum target, GLenum format);
    void setBoundTextureType(GLenum target, GLenum type);
    void setBoundTextureDims(GLenum target, GLenum cubetarget, GLsizei level, GLsizei width, GLsizei height, GLsizei depth);
    void setBoundTextureSamples(GLenum target, GLsizei samples);
    void addTextureCubeMapImage(GLenum stateTarget, GLenum cubeTarget);

    // glTexStorage2D disallows any change in texture format after it is set for a particular texture.
    void setBoundTextureImmutableFormat(GLenum target);
    bool isBoundTextureImmutableFormat(GLenum target) const;
    bool isBoundTextureComplete(GLenum target) const;

    // glDeleteTextures(...)
    // Remove references to the to-be-deleted textures.
    void deleteTextures(GLsizei n, const GLuint* textures);

    // Render buffer objects
    void addRenderbuffers(GLsizei n, GLuint* renderbuffers);
    void removeRenderbuffers(GLsizei n, const GLuint* renderbuffers);
    bool usedRenderbufferName(GLuint name) const;
    void bindRenderbuffer(GLenum target, GLuint name);
    GLuint boundRenderbuffer() const;
    void setBoundRenderbufferFormat(GLenum format);
    void setBoundRenderbufferSamples(GLsizei samples);
    void setBoundRenderbufferDimensions(GLsizei width, GLsizei height);
    void setBoundRenderbufferEGLImageBacked();

    // Frame buffer objects
    void addFramebuffers(GLsizei n, GLuint* framebuffers);
    void removeFramebuffers(GLsizei n, const GLuint* framebuffers);
    bool usedFramebufferName(GLuint name) const;
    void bindFramebuffer(GLenum target, GLuint name);
    void setCheckFramebufferStatus(GLenum target, GLenum status);
    void setFramebufferParameter(GLenum target, GLenum pname, GLint param);
    GLenum getCheckFramebufferStatus(GLenum target) const;
    GLuint boundFramebuffer(GLenum target) const;

    // Texture object -> FBO
    void attachTextureObject(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
    std::shared_ptr<TextureRec> getFboAttachmentTexture(GLenum target, GLenum attachment) const;

    // RBO -> FBO
    void detachRbo(GLuint renderbuffer);
    void detachRboFromFbo(GLenum target, GLenum attachment, GLuint renderbuffer);
    void attachRbo(GLenum target, GLenum attachment, GLuint renderbuffer);
    std::shared_ptr<RboProps> getFboAttachmentRbo(GLenum target, GLenum attachment) const;

    // FBO attachments in general
    bool attachmentHasObject(GLenum target, GLenum attachment) const;
    bool depthStencilHasSameObject(GLenum target) const;

    // Dirty FBO completeness
    void setFboCompletenessDirtyForTexture(GLuint texture);
    void setFboCompletenessDirtyForRbo(std::shared_ptr<RboProps> rbo);

    // Transform feedback state
    void setTransformFeedbackActive(bool active);
    void setTransformFeedbackUnpaused(bool unpaused);
    void setTransformFeedbackVaryingsCountForLinking(uint32_t count);
    bool getTransformFeedbackActive() const;
    bool getTransformFeedbackUnpaused() const;
    bool getTransformFeedbackActiveUnpaused() const;
    uint32_t getTransformFeedbackVaryingsCountForLinking() const;

    // Stencil state
    void stencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
    void stencilMaskSeparate(GLenum face, GLuint mask);
    void stencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass);

    void setTextureData(SharedTextureDataMap* sharedTexData);
    void setRenderbufferInfo(RenderbufferInfo* rbInfo);
    void setSamplerInfo(SamplerInfo* samplerInfo);

    bool compressedTexImageSizeCompatible(GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLsizei imageSize);
    // set eglsurface property on default framebuffer
    // if coming from eglMakeCurrent
    void fromMakeCurrent();
    // set indexed buffer state.
    // We need to query the underlying OpenGL to get
    // accurate values for indexed buffers
    // and # render targets.
    void initFromCaps(
        const HostDriverCaps& caps);
    bool needsInitFromCaps() const;
    void setExtensions(const std::string& extensions);
    bool hasExtension(const char* ext) const;

    // Queries the format backing the current framebuffer.
    // Type differs depending on whether the attachment
    // is a texture or renderbuffer.
    void getBoundFramebufferFormat(
            GLenum target,
            GLenum attachment,
            FboFormatInfo* res_info) const;
    FboAttachmentType getBoundFramebufferAttachmentType(
            GLenum target,
            GLenum attachment) const;
    int getMaxColorAttachments() const;
    int getMaxDrawBuffers() const;

    // Uniform/attribute validation info
    UniformValidationInfo currentUniformValidationInfo;
    AttribValidationInfo currentAttribValidationInfo;;

    // Uniform validation api
    void validateUniform(bool isFloat, bool isUnsigned, GLint columns, GLint rows, GLint location, GLsizei count, GLenum* err);
    // Attrib validation
    bool isAttribIndexUsedByProgram(int attribIndex);

    // Fast access to some enables and stencil related glGet's
    bool state_GL_STENCIL_TEST;
    GLenum state_GL_STENCIL_FUNC;
    unsigned int state_GL_STENCIL_VALUE_MASK;
    int state_GL_STENCIL_REF;
    GLenum state_GL_STENCIL_FAIL;
    GLenum state_GL_STENCIL_PASS_DEPTH_FAIL;
    GLenum state_GL_STENCIL_PASS_DEPTH_PASS;
    GLenum state_GL_STENCIL_BACK_FUNC;
    unsigned int state_GL_STENCIL_BACK_VALUE_MASK;
    int state_GL_STENCIL_BACK_REF;
    GLenum state_GL_STENCIL_BACK_FAIL;
    GLenum state_GL_STENCIL_BACK_PASS_DEPTH_FAIL;
    GLenum state_GL_STENCIL_BACK_PASS_DEPTH_PASS;
    unsigned int state_GL_STENCIL_WRITEMASK;
    unsigned int state_GL_STENCIL_BACK_WRITEMASK;
    int state_GL_STENCIL_CLEAR_VALUE;
private:
    void init();
    bool m_initialized;
    PixelStoreState m_pixelStore;

    using DirtyMap = PredicateMap<uint32_t, true>;

    ExistenceMap mBufferIds;
    ExistenceMap mTransformFeedbackIds;
    SamplerInfo* mSamplerInfo;
    ExistenceMap mQueryIds;
    LastQueryTargetInfo mLastQueryTargets;

    // Bound query target validity and tracking
    struct BoundTargetInfo {
        GLuint id;
        bool valid;
    };
   
    // Transform feedback
    BoundTargetInfo mBoundTransformFeedbackValidity;

    // Queries
    // GL_ANY_SAMPLES_PASSED
    BoundTargetInfo mBoundQueryValidity_AnySamplesPassed;
    // GL_ANY_SAMPLES_PASSED_CONSERVATIVE
    BoundTargetInfo mBoundQueryValidity_AnySamplesPassedConservative;
    // GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
    BoundTargetInfo mBoundQueryValidity_TransformFeedbackPrimitivesWritten;

    // Dirty maps
    DirtyMap mHostMappedBufferDirty;

    // GL_ARRAY_BUFFER_BINDING is separate from VAO state
    GLuint m_arrayBuffer;
    GLuint m_arrayBuffer_lastEncode;
    VAOStateMap m_vaoMap;
    VAOStateRef m_currVaoState;

    uint16_t m_attribEnableCache;
    uint16_t m_vaoAttribBindingCacheInvalid;
    uint16_t m_vaoAttribBindingHasClientArrayCache;
    uint16_t m_vaoAttribBindingHasVboCache;
    uint8_t m_noClientArraysCache;

    // Other buffer id's, other targets
    GLuint m_copyReadBuffer;
    GLuint m_copyWriteBuffer;

    GLuint m_pixelPackBuffer;
    GLuint m_pixelUnpackBuffer;

    GLuint m_transformFeedbackBuffer;
    GLuint m_uniformBuffer;

    GLuint m_atomicCounterBuffer;
    GLuint m_dispatchIndirectBuffer;
    GLuint m_drawIndirectBuffer;
    GLuint m_shaderStorageBuffer;
    GLuint m_textureBuffer;

    bool m_transformFeedbackActive;
    bool m_transformFeedbackUnpaused;
    uint32_t m_transformFeedbackVaryingsCountForLinking;

    HostDriverCaps m_hostDriverCaps;
    bool m_extensions_set;
    std::string m_extensions;
    bool m_has_color_buffer_float_extension;
    bool m_has_color_buffer_half_float_extension;
    std::vector<BufferBinding> m_indexedTransformFeedbackBuffers;
    std::vector<BufferBinding> m_indexedUniformBuffers;
    std::vector<BufferBinding> m_indexedAtomicCounterBuffers;
    std::vector<BufferBinding> m_indexedShaderStorageBuffers;
    int m_log2MaxTextureSize;

    int m_glesMajorVersion;
    int m_glesMinorVersion;
    int m_activeTexture;
    GLint m_currentProgram;
    GLint m_currentShaderProgram;
    ProgramPipelineMap m_programPipelines;

    enum TextureTarget {
        TEXTURE_2D = 0,
        TEXTURE_EXTERNAL = 1,
        TEXTURE_CUBE_MAP = 2,
        TEXTURE_2D_ARRAY = 3,
        TEXTURE_3D = 4,
        TEXTURE_2D_MULTISAMPLE = 5,
        TEXTURE_BUFFER = 6,
        TEXTURE_TARGET_COUNT
    };
    struct TextureUnit {
        unsigned int enables;
        GLuint texture[TEXTURE_TARGET_COUNT];
        GLuint boundSampler;
    };
    struct TextureState {
        TextureUnit unit[MAX_TEXTURE_UNITS];
        TextureUnit* activeUnit;
        // Initialized from shared group.
        SharedTextureDataMap* textureRecs;
    };
    TextureState m_tex;

    // State tracking of cube map definitions.
    // Currently used only for driver workarounds
    // when using GL_LUMINANCE and defining cube maps with
    // glCopyTexImage2D.
    struct CubeMapDef {
        GLuint id;
        GLenum target;
        GLint level;
        GLenum internalformat;
    };
    struct CubeMapDefCompare {
        bool operator() (const CubeMapDef& a,
                         const CubeMapDef& b) const {
            if (a.id != b.id) return a.id < b.id;
            if (a.target != b.target) return a.target < b.target;
            if (a.level != b.level) return a.level < b.level;
            if (a.internalformat != b.internalformat)
                return a.internalformat < b.internalformat;
            return false;
        }
    };
    std::set<CubeMapDef, CubeMapDefCompare> m_cubeMapDefs;
    void writeCopyTexImageState(GLenum target, GLint level,
                                GLenum internalformat);
    GLenum copyTexImageNeededTarget(GLenum target, GLint level,
                                    GLenum internalformat);

    struct RboState {
        std::shared_ptr<RboProps> boundRenderbuffer;
        // Connects to share group.
        // Expected that share group lifetime outlives this context.
        RenderbufferInfo* rboData;
    };
    RboState mRboState;
    void addFreshRenderbuffer(GLuint name);

    struct FboState {
        GLuint boundDrawFramebuffer;
        GLuint boundReadFramebuffer;
        size_t boundFramebufferIndex;
        std::map<GLuint, FboProps> fboData;
        GLenum drawFboCheckStatus;
        GLenum readFboCheckStatus;
    };
    FboState mFboState;
    void addFreshFramebuffer(GLuint name);
    FboProps& boundFboProps(GLenum target);
    const FboProps& boundFboProps_const(GLenum target) const;

    // Querying framebuffer format
    GLenum queryTexType(GLuint name) const;
    GLsizei queryTexSamples(GLuint name) const;

    static int compareTexId(const void* pid, const void* prec);
    TextureRec* addTextureRec(GLuint id, GLenum target);
    std::shared_ptr<TextureRec> getTextureRec(GLuint id) const;
    TextureRec* getTextureRecPtr(GLuint id) const;
    TextureRec* getTextureRecPtrLocked(GLuint id) const;

public:
    bool isTexture(GLuint name) const;
    bool isTextureWithStorage(GLuint name) const;
    bool isTextureWithTarget(GLuint name) const;
    bool isTextureCubeMap(GLuint name) const;
    bool isRenderbuffer(GLuint name) const;
    bool isRenderbufferThatWasBound(GLuint name) const;

    void getClientStatePointer(GLenum pname, GLvoid** params);

    template <class T>
    int getVertexAttribParameter(GLuint index, GLenum param, T *ptr)
    {
        bool handled = true;
        const VertexAttribState& vertexAttrib = getState(index);
        const BufferBinding& vertexAttribBufferBinding =
            m_currVaoState.bufferBindings_const()[vertexAttrib.bindingindex];

        switch(param) {
#define GL_VERTEX_ATTRIB_BINDING 0x82D4
        case GL_VERTEX_ATTRIB_BINDING:
            *ptr = (T)vertexAttrib.bindingindex;
            break;
#define GL_VERTEX_ATTRIB_RELATIVE_OFFSET 0x82D5
        case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:
            *ptr = (T)vertexAttrib.reloffset;
            break;
        case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            *ptr = (T)(vertexAttribBufferBinding.buffer);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
            *ptr = (T)(vertexAttrib.enabled);
            break;
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88FD
        case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
            *ptr = (T)(vertexAttrib.isInt);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_SIZE:
            *ptr = (T)(vertexAttrib.size);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
            *ptr = (T)(vertexAttribBufferBinding.stride);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_TYPE:
            *ptr = (T)(vertexAttrib.type);
            break;
        case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            *ptr = (T)(vertexAttrib.normalized);
            break;
        case GL_CURRENT_VERTEX_ATTRIB:
            handled = false;
            break;
        default:
            handled = false;
        }
        return handled;
    }

    template <class T>
    bool getClientStateParameter(GLenum param, T* out)
    {
        bool isClientStateParam = false;
        switch (param) {
        case GL_CLIENT_ACTIVE_TEXTURE: {
            GLint tex = getActiveTexture() + GL_TEXTURE0;
            *out = tex;
            isClientStateParam = true;
            break;
            }
        case GL_VERTEX_ARRAY_SIZE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::VERTEX_LOCATION);
            *out = state.size;
            isClientStateParam = true;
            break;
            }
        case GL_VERTEX_ARRAY_TYPE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::VERTEX_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_VERTEX_ARRAY_STRIDE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::VERTEX_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_COLOR_ARRAY_SIZE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::COLOR_LOCATION);
            *out = state.size;
            isClientStateParam = true;
            break;
            }
        case GL_COLOR_ARRAY_TYPE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::COLOR_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_COLOR_ARRAY_STRIDE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::COLOR_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_NORMAL_ARRAY_TYPE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::NORMAL_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_NORMAL_ARRAY_STRIDE: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::NORMAL_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_TEXTURE_COORD_ARRAY_SIZE: {
            const GLClientState::VertexAttribState& state = getState(getActiveTexture() + GLClientState::TEXCOORD0_LOCATION);
            *out = state.size;
            isClientStateParam = true;
            break;
            }
        case GL_TEXTURE_COORD_ARRAY_TYPE: {
            const GLClientState::VertexAttribState& state = getState(getActiveTexture() + GLClientState::TEXCOORD0_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_TEXTURE_COORD_ARRAY_STRIDE: {
            const GLClientState::VertexAttribState& state = getState(getActiveTexture() + GLClientState::TEXCOORD0_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_POINT_SIZE_ARRAY_TYPE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::POINTSIZE_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_POINT_SIZE_ARRAY_STRIDE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::POINTSIZE_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_MATRIX_INDEX_ARRAY_SIZE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::MATRIXINDEX_LOCATION);
            *out = state.size;
            isClientStateParam = true;
            break;
            }
        case GL_MATRIX_INDEX_ARRAY_TYPE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::MATRIXINDEX_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_MATRIX_INDEX_ARRAY_STRIDE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::MATRIXINDEX_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_WEIGHT_ARRAY_SIZE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::WEIGHT_LOCATION);
            *out = state.size;
            isClientStateParam = true;
            break;
            }
        case GL_WEIGHT_ARRAY_TYPE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::WEIGHT_LOCATION);
            *out = state.type;
            isClientStateParam = true;
            break;
            }
        case GL_WEIGHT_ARRAY_STRIDE_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::WEIGHT_LOCATION);
            *out = state.stride;
            isClientStateParam = true;
            break;
            }
        case GL_VERTEX_ARRAY_BUFFER_BINDING: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::VERTEX_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_NORMAL_ARRAY_BUFFER_BINDING: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::NORMAL_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_COLOR_ARRAY_BUFFER_BINDING: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::COLOR_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING: {
            const GLClientState::VertexAttribState& state = getState(getActiveTexture()+GLClientState::TEXCOORD0_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::POINTSIZE_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_MATRIX_INDEX_ARRAY_BUFFER_BINDING_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::MATRIXINDEX_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_WEIGHT_ARRAY_BUFFER_BINDING_OES: {
            const GLClientState::VertexAttribState& state = getState(GLClientState::WEIGHT_LOCATION);
            *out = state.bufferObject;
            isClientStateParam = true;
            break;
            }
        case GL_ARRAY_BUFFER_BINDING: {
            int buffer = getBuffer(GL_ARRAY_BUFFER);
            *out = buffer;
            isClientStateParam = true;
            break;
            }
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: {
            int buffer = getBuffer(GL_ELEMENT_ARRAY_BUFFER);
            *out = buffer;
            isClientStateParam = true;
            break;
            }
        case GL_MAX_VERTEX_ATTRIBS: {
            *out = CODEC_MAX_VERTEX_ATTRIBUTES;
            isClientStateParam = true;
            break;
        }
        case GL_FRAMEBUFFER_BINDING:
        // also case GL_DRAW_FRAMEBUFFER_BINDING:
            *out = (T)mFboState.boundDrawFramebuffer;
            isClientStateParam = true;
            break;
        case 0x8CAA: // GL_READ_FRAMEBUFFER_BINDING
            *out = (T)mFboState.boundReadFramebuffer;
            isClientStateParam = true;
            break;
        case GL_STENCIL_TEST:
            *out = (T)state_GL_STENCIL_TEST;
            isClientStateParam = true;
            break;
        case GL_STENCIL_FUNC:
            *out = (T)state_GL_STENCIL_FUNC;
            isClientStateParam = true;
            break;
        case GL_STENCIL_VALUE_MASK:
            *out = (T)state_GL_STENCIL_VALUE_MASK;
            isClientStateParam = true;
            break;
        case GL_STENCIL_REF:
            *out = (T)state_GL_STENCIL_REF;
            isClientStateParam = true;
            break;
        case GL_STENCIL_FAIL:
            *out = (T)state_GL_STENCIL_FAIL;
            isClientStateParam = true;
            break;
        case GL_STENCIL_PASS_DEPTH_FAIL:
            *out = (T)state_GL_STENCIL_PASS_DEPTH_FAIL;
            isClientStateParam = true;
            break;
        case GL_STENCIL_PASS_DEPTH_PASS:
            *out = (T)state_GL_STENCIL_PASS_DEPTH_PASS;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_FUNC:
            *out = (T)state_GL_STENCIL_BACK_FUNC;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_VALUE_MASK:
            *out = (T)state_GL_STENCIL_BACK_VALUE_MASK;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_REF:
            *out = (T)state_GL_STENCIL_BACK_REF;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_FAIL:
            *out = (T)state_GL_STENCIL_BACK_FAIL;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
            *out = (T)state_GL_STENCIL_BACK_PASS_DEPTH_FAIL;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_PASS_DEPTH_PASS:
            *out = (T)state_GL_STENCIL_BACK_PASS_DEPTH_PASS;
            isClientStateParam = true;
            break;
        case GL_STENCIL_WRITEMASK:
            *out = (T)state_GL_STENCIL_WRITEMASK;
            isClientStateParam = true;
            break;
        case GL_STENCIL_BACK_WRITEMASK:
            *out = (T)state_GL_STENCIL_BACK_WRITEMASK;
            isClientStateParam = true;
            break;
        case GL_STENCIL_CLEAR_VALUE:
            *out = (T)state_GL_STENCIL_CLEAR_VALUE;
            isClientStateParam = true;
            break;
        }
        return isClientStateParam;
    }

};

}  // namespace guest
}  // namespace gfxstream

#endif
