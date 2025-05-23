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
#ifndef _GL_SHARED_GROUP_H_
#define _GL_SHARED_GROUP_H_

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "IndexRangeCache.h"
#include "StateTrackingSupport.h"
#include "TextureSharedData.h"

using gfxstream::guest::AutoLock;
using gfxstream::guest::Lock;

namespace gfxstream {
namespace guest {

struct BufferData {
    BufferData();
    BufferData(GLsizeiptr size, const void* data);

    // General buffer state
    GLsizeiptr m_size;
    GLenum m_usage;

    // Mapped buffer state
    bool m_mapped;
    GLbitfield m_mappedAccess;
    GLintptr m_mappedOffset;
    GLsizeiptr m_mappedLength;
    uint64_t m_guest_paddr;

    // Internal bookkeeping
    std::vector<char> m_fixedBuffer; // actual buffer is shadowed here
    IndexRangeCache m_indexRangeCache;
};

class ProgramData {
private:
    typedef struct _IndexInfo {
        GLint base;
        GLint size;
        GLenum type;
        GLint hostLocsPerElement;
        GLuint flags;
        GLint samplerValue; // only set for sampler uniforms
    } IndexInfo;

    typedef struct _AttribInfo {
        GLint attribLoc;
        GLint size;
        GLenum type;
    } AttribInfo;

    GLuint m_numIndexes;
    GLuint m_numAttributes;
    IndexInfo* m_Indexes;
    AttribInfo* m_attribIndexes;
    bool m_initialized;

    std::vector<GLuint> m_shaders;
    std::vector<GLenum> m_shaderTypes;

    uint32_t m_refcount;
    GLint m_linkStatus;

    uint32_t m_activeUniformBlockCount;
    uint32_t m_transformFeedbackVaryingsCount;;

public:
    enum {
        INDEX_FLAG_SAMPLER_EXTERNAL = 0x00000001,
    };

    ProgramData();
    void initProgramData(GLuint numIndexes, GLuint numAttributes);
    bool isInitialized();
    virtual ~ProgramData();
    void setIndexInfo(GLuint index, GLint base, GLint size, GLenum type);
    void setAttribInfo(GLuint index, GLint base, GLint size, GLenum type);
    void setIndexFlags(GLuint index, GLuint flags);
    GLuint getIndexForLocation(GLint location);
    GLenum getTypeForLocation(GLint location);
    bool isValidUniformLocation(GLint location);
    void getExternalSamplerUniformIndices(std::vector<GLuint>* outIndices);

    GLint getNextSamplerUniform(GLint index, GLint* val, GLenum* target);
    bool setSamplerUniform(GLint appLoc, GLint val, GLenum* target);

    bool attachShader(GLuint shader, GLenum shaderType);
    bool detachShader(GLuint shader);
    size_t getNumShaders() const { return m_shaders.size(); }
    GLuint getShader(size_t i) const { return m_shaders[i]; }

    void incRef() { ++m_refcount; }
    bool decRef() {
        --m_refcount;
        return 0 == m_refcount;
    }

    UniformValidationInfo compileValidationInfo(bool* error) const;
    AttribValidationInfo compileAttribValidationInfo(bool* error) const;
    void setLinkStatus(GLint status) { m_linkStatus = status; }
    GLint getLinkStatus() { return m_linkStatus; }

    void setActiveUniformBlockCount(uint32_t count) {
        m_activeUniformBlockCount = count;
    }

    uint32_t getActiveUniformBlockCount() const {
        return m_activeUniformBlockCount;
    }

    void setTransformFeedbackVaryingsCount(uint32_t count) {
        m_transformFeedbackVaryingsCount = count;
    }

    uint32_t getTransformFeedbackVaryingsCount() const {
        return m_transformFeedbackVaryingsCount;
    }

    GLuint getActiveUniformsCount() const {
        return m_numIndexes;
    }

    GLuint getActiveAttributesCount() const {
        return m_numAttributes;
    }
};

struct ShaderData {
    typedef std::vector<std::string> StringList;
    StringList samplerExternalNames;
    int refcount;
    std::vector<std::string> sources;
    GLenum shaderType;
};

class ShaderProgramData {
public:
    ShaderData shaderData;
    ProgramData programData;
};

class GLSharedGroup {
private:
    SharedTextureDataMap m_textureRecs;
    std::map<GLuint, BufferData*> m_buffers;
    std::map<GLuint, ProgramData*> m_programs;
    std::map<GLuint, ShaderData*> m_shaders;
    std::map<uint32_t, ShaderProgramData*> m_shaderPrograms;
    std::map<GLuint, uint32_t> m_shaderProgramIdMap;
    RenderbufferInfo m_renderbufferInfo;
    SamplerInfo m_samplerInfo;

    Lock m_lock;

    void refShaderDataLocked(GLuint shader);
    void unrefShaderDataLocked(GLuint shader);

    uint32_t m_shaderProgramId;

    ProgramData* getProgramDataLocked(GLuint program);
public:
    GLSharedGroup();
    ~GLSharedGroup();
    bool isShaderOrProgramObject(GLuint obj);
    BufferData * getBufferData(GLuint bufferId);
    SharedTextureDataMap* getTextureData();
    RenderbufferInfo* getRenderbufferInfo();
    SamplerInfo* getSamplerInfo();
    void    addBufferData(GLuint bufferId, GLsizeiptr size, const void* data);
    void    updateBufferData(GLuint bufferId, GLsizeiptr size, const void* data);
    void    setBufferUsage(GLuint bufferId, GLenum usage);
    void    setBufferMapped(GLuint bufferId, bool mapped);
    GLenum    getBufferUsage(GLuint bufferId);
    bool    isBufferMapped(GLuint bufferId);
    GLenum  subUpdateBufferData(GLuint bufferId, GLintptr offset, GLsizeiptr size, const void* data);
    void    deleteBufferData(GLuint);

    bool    isProgram(GLuint program);
    bool    isProgramInitialized(GLuint program);
    void    addProgramData(GLuint program);
    void    initProgramData(GLuint program, GLuint numIndexes, GLuint numAttributes);
    void    refProgramData(GLuint program);
    void    onUseProgram(GLuint previous, GLuint next);
    bool    attachShader(GLuint program, GLuint shader);
    bool    detachShader(GLuint program, GLuint shader);
    bool    detachShaderLocked(GLuint program, GLuint shader);
    void    deleteProgramData(GLuint program);
    void    deleteProgramDataLocked(GLuint program);
    void    setProgramIndexInfo(GLuint program, GLuint index, GLint base, GLint size, GLenum type, const char* name);
    void    setProgramIndexFlag(GLuint program, GLuint index, GLuint flags);
    void    setProgramAttribInfo(GLuint program, GLuint index, GLint attribLoc, GLint size, GLenum type, const char* name);
    GLenum  getProgramUniformType(GLuint program, GLint location);
    GLint   getNextSamplerUniform(GLuint program, GLint index, GLint* val, GLenum* target);
    bool    setSamplerUniform(GLuint program, GLint appLoc, GLint val, GLenum* target);
    bool    isProgramUniformLocationValid(GLuint program, GLint location);
    bool    getExternalSamplerUniformIndices(GLuint program, std::vector<GLuint>* outIndices);

    bool    isShader(GLuint shader);
    bool    addShaderData(GLuint shader, GLenum shaderType);
    // caller must hold a reference to the shader as long as it holds the pointer
    ShaderData* getShaderData(GLuint shader);
    void    unrefShaderData(GLuint shader);

    // For separable shader programs.
    uint32_t addNewShaderProgramData();
    void associateGLShaderProgram(GLuint shaderProgramName, uint32_t shaderProgramId);
    ShaderProgramData* getShaderProgramDataById(uint32_t id);
    ShaderProgramData* getShaderProgramData(GLuint shaderProgramName);
    void deleteShaderProgramDataById(uint32_t id);
    void deleteShaderProgramData(GLuint shaderProgramName);
    void initShaderProgramData(GLuint shaderProgram, GLuint numIndices, GLuint numAttributes);
    void setShaderProgramIndexInfo(GLuint shaderProgram, GLuint index, GLint base, GLint size, GLenum type, const char* name);

    // Validation info
    UniformValidationInfo getUniformValidationInfo(GLuint program);
    AttribValidationInfo getAttribValidationInfo(GLuint program);

    void setProgramLinkStatus(GLuint program, GLint linkStatus);
    GLint getProgramLinkStatus(GLuint program);

    void setActiveUniformBlockCountForProgram(GLuint program, GLint numBlocks);
    GLint getActiveUniformBlockCount(GLuint program);

    void setTransformFeedbackVaryingsCountForProgram(GLuint program, GLint count);
    GLint getTransformFeedbackVaryingsCountForProgram(GLuint program);

    int getActiveUniformsCountForProgram(GLuint program);
    int getActiveAttributesCountForProgram(GLuint program);
};

typedef std::shared_ptr<GLSharedGroup> GLSharedGroupPtr;

}  // namespace guest
}  // namespace gfxstream

#endif //_GL_SHARED_GROUP_H_
