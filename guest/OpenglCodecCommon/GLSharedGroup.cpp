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

#include "gfxstream/guest/GLSharedGroup.h"

#include "KeyedVectorUtils.h"
#include "glUtils.h"

namespace gfxstream {
namespace guest {

/**** BufferData ****/

BufferData::BufferData() : m_size(0), m_usage(0), m_mapped(false) {};

BufferData::BufferData(GLsizeiptr size, const void* data) :
    m_size(size), m_usage(0), m_mapped(false) {

    if (size > 0) {
        m_fixedBuffer.resize(size);
    }

    if (data) {
        memcpy(m_fixedBuffer.data(), data, size);
    }
}

/**** ProgramData ****/
ProgramData::ProgramData() : m_numIndexes(0),
                             m_numAttributes(0),
                             m_initialized(false) {
    m_Indexes = NULL;
    m_attribIndexes = NULL;
    m_refcount = 1;
    m_linkStatus = 0;
    m_activeUniformBlockCount = 0;
    m_transformFeedbackVaryingsCount = 0;
}

void ProgramData::initProgramData(GLuint numIndexes, GLuint numAttributes) {
    m_initialized = true;
    m_numIndexes = numIndexes;
    m_numAttributes = numAttributes;

    delete [] m_Indexes;
    delete [] m_attribIndexes;

    m_Indexes = new IndexInfo[numIndexes];
    m_attribIndexes = new AttribInfo[m_numAttributes];
}

bool ProgramData::isInitialized() {
    return m_initialized;
}

ProgramData::~ProgramData() {

    delete [] m_Indexes;
    delete [] m_attribIndexes;

    m_Indexes = NULL;
}

void ProgramData::setIndexInfo(
    GLuint index, GLint base, GLint size, GLenum type) {

    if (index >= m_numIndexes) return;

    m_Indexes[index].base = base;
    m_Indexes[index].size = size;
    m_Indexes[index].type = type;
    m_Indexes[index].hostLocsPerElement = 1;
    m_Indexes[index].flags = 0;
    m_Indexes[index].samplerValue = 0;
}

void ProgramData::setAttribInfo(
    GLuint index, GLint attribLoc, GLint size, GLenum type) {

    if (index >= m_numAttributes) return;

    m_attribIndexes[index].attribLoc = attribLoc;
    m_attribIndexes[index].size = size;
    m_attribIndexes[index].type = type;
}

void ProgramData::setIndexFlags(GLuint index, GLuint flags) {

    if (index >= m_numIndexes) return;

    m_Indexes[index].flags |= flags;
}

GLuint ProgramData::getIndexForLocation(GLint location) {
    GLuint index = m_numIndexes;

    GLint minDist = -1;

    for (GLuint i = 0; i < m_numIndexes; ++i) {
        GLint dist = location - m_Indexes[i].base;
        if (dist >= 0 && (minDist < 0 || dist < minDist)) {
            index = i;
            minDist = dist;
        }
    }

    return index;
}

GLenum ProgramData::getTypeForLocation(GLint location) {
    GLuint index = getIndexForLocation(location);
    if (index < m_numIndexes) {
        return m_Indexes[index].type;
    }
    return 0;
}

bool ProgramData::isValidUniformLocation(GLint location) {
    for (GLuint i = 0; i < m_numIndexes; ++i) {
        if (location >= m_Indexes[i].base &&
            location < m_Indexes[i].base + m_Indexes[i].size)
            return true;
    }

    return false;
}

void ProgramData::getExternalSamplerUniformIndices(std::vector<GLuint>* outIndices) {
    for (GLuint i = 0; i < m_numIndexes; ++i) {
        if (m_Indexes[i].flags & INDEX_FLAG_SAMPLER_EXTERNAL) {
            outIndices->push_back(i);
        }
    }
}

GLint ProgramData::getNextSamplerUniform(
    GLint index, GLint* val, GLenum* target) {

    for (GLint i = index + 1; i >= 0 && i < (GLint)m_numIndexes; i++) {

        if (m_Indexes[i].type == GL_SAMPLER_2D) {

            if (val) *val = m_Indexes[i].samplerValue;

            if (target) {
                if (m_Indexes[i].flags & INDEX_FLAG_SAMPLER_EXTERNAL) {
                    *target = GL_TEXTURE_EXTERNAL_OES;
                } else {
                    *target = GL_TEXTURE_2D;
                }
            }

            return i;
        }

    }

    return -1;
}

bool ProgramData::setSamplerUniform(GLint appLoc, GLint val, GLenum* target) {

    for (GLuint i = 0; i < m_numIndexes; i++) {

        GLint elemIndex = appLoc - m_Indexes[i].base;

        if (elemIndex >= 0 && elemIndex < m_Indexes[i].size) {
            if (m_Indexes[i].type == GL_SAMPLER_2D) {
                m_Indexes[i].samplerValue = val;
                if (target) {
                    if (m_Indexes[i].flags & INDEX_FLAG_SAMPLER_EXTERNAL) {
                        *target = GL_TEXTURE_EXTERNAL_OES;
                    } else {
                        *target = GL_TEXTURE_2D;

                    }
                }
                return true;
            }
        }
    }

    return false;
}

bool ProgramData::attachShader(GLuint shader, GLenum shaderType) {
    size_t n = m_shaders.size();

    for (size_t i = 0; i < n; i++) {
        if (m_shaders[i] == shader) {
            return false;
        } else if (m_shaderTypes[i] == shaderType) {
            return false;
        }
    }
    m_shaders.push_back(shader);
    m_shaderTypes.push_back(shaderType);
    return true;
}

bool ProgramData::detachShader(GLuint shader) {
    size_t n = m_shaders.size();

    for (size_t i = 0; i < n; i++) {
        if (m_shaders[i] == shader) {
            m_shaders.erase(m_shaders.begin() + i);
            m_shaderTypes.erase(m_shaderTypes.begin() + i);
            return true;
        }
    }

    return false;
}

UniformValidationInfo ProgramData::compileValidationInfo(bool* error) const {
    UniformValidationInfo res;
    if (!m_Indexes) {
        *error = true;
        return res;
    }

    for (GLuint i = 0; i < m_numIndexes; ++i) {
        if (m_Indexes[i].base < 0) continue;

        UniformLocationInfo info = {
            .valid = true,
            .columns = getColumnsOfType(m_Indexes[i].type),
            .rows = getRowsOfType(m_Indexes[i].type),
            .isSampler = isSamplerType(m_Indexes[i].type),
            .isInt = isIntegerType(m_Indexes[i].type),
            .isArray = m_Indexes[i].size > 1,
            .isUnsigned = isUnsignedIntType(m_Indexes[i].type),
            .isBool = isBoolType(m_Indexes[i].type),
        };
        for (GLint j = 0; j < m_Indexes[i].size; ++j) {
            res.add(m_Indexes[i].base + j, info);
        }
    }

    return res;
}

AttribValidationInfo ProgramData::compileAttribValidationInfo(bool* error) const {
    AttribValidationInfo res;
    if (!m_attribIndexes) {
        *error = true;
        return res;
    }

    for (GLuint i = 0; i < m_numAttributes; ++i) {
        if (m_attribIndexes[i].attribLoc < 0) continue;

        AttribIndexInfo info = {
            .validInProgram = true,
        };

        for (GLuint j = 0; j < getAttributeCountOfType(m_attribIndexes[i].type) * m_attribIndexes[i].size ; ++j) {
            res.add(m_attribIndexes[i].attribLoc + j, info);
        }
    }

    return res;
}
/***** GLSharedGroup ****/

GLSharedGroup::GLSharedGroup() { }

GLSharedGroup::~GLSharedGroup() {
    m_buffers.clear();
    m_programs.clear();
    clearObjectMap(m_buffers);
    clearObjectMap(m_programs);
    clearObjectMap(m_shaders);
    clearObjectMap(m_shaderPrograms);
}

bool GLSharedGroup::isShaderOrProgramObject(GLuint obj) {

    AutoLock<Lock> _lock(m_lock);

    return (findObjectOrDefault(m_shaders, obj) ||
            findObjectOrDefault(m_programs, obj) ||
            findObjectOrDefault(m_shaderPrograms, m_shaderProgramIdMap[obj]));
}

BufferData* GLSharedGroup::getBufferData(GLuint bufferId) {

    AutoLock<Lock> _lock(m_lock);

    return findObjectOrDefault(m_buffers, bufferId);
}

SharedTextureDataMap* GLSharedGroup::getTextureData() {
    return &m_textureRecs;
}

RenderbufferInfo* GLSharedGroup::getRenderbufferInfo() {
    return &m_renderbufferInfo;
}

SamplerInfo* GLSharedGroup::getSamplerInfo() {
    return &m_samplerInfo;
}

void GLSharedGroup::addBufferData(GLuint bufferId, GLsizeiptr size, const void* data) {

    AutoLock<Lock> _lock(m_lock);

    m_buffers[bufferId] = new BufferData(size, data);
}

void GLSharedGroup::updateBufferData(GLuint bufferId, GLsizeiptr size, const void* data) {

    AutoLock<Lock> _lock(m_lock);

    BufferData* currentBuffer = findObjectOrDefault(m_buffers, bufferId);

    if (currentBuffer) delete currentBuffer;

    m_buffers[bufferId] = new BufferData(size, data);
}

void GLSharedGroup::setBufferUsage(GLuint bufferId, GLenum usage) {

    AutoLock<Lock> _lock(m_lock);

    BufferData* data = findObjectOrDefault(m_buffers, bufferId);

    if (data) data->m_usage = usage;
}

void GLSharedGroup::setBufferMapped(GLuint bufferId, bool mapped) {
    BufferData* buf = findObjectOrDefault(m_buffers, bufferId);

    if (!buf) return;

    buf->m_mapped = mapped;
}

GLenum GLSharedGroup::getBufferUsage(GLuint bufferId) {
    BufferData* buf = findObjectOrDefault(m_buffers, bufferId);

    if (!buf) return 0;

    return buf->m_usage;
}

bool GLSharedGroup::isBufferMapped(GLuint bufferId) {
    BufferData* buf = findObjectOrDefault(m_buffers, bufferId);

    if (!buf) return false;

    return buf->m_mapped;
}

GLenum GLSharedGroup::subUpdateBufferData(GLuint bufferId, GLintptr offset, GLsizeiptr size, const void* data) {

    AutoLock<Lock> _lock(m_lock);

    BufferData* buf = findObjectOrDefault(m_buffers, bufferId);

    if ((!buf) || (buf->m_size < offset+size) || (offset < 0) || (size<0)) {
        return GL_INVALID_VALUE;
    }

    memcpy(&buf->m_fixedBuffer[offset], data, size);

    buf->m_indexRangeCache.invalidateRange((size_t)offset, (size_t)size);
    return GL_NO_ERROR;
}

void GLSharedGroup::deleteBufferData(GLuint bufferId) {

    AutoLock<Lock> _lock(m_lock);

    BufferData* buf = findObjectOrDefault(m_buffers, bufferId);
    if (buf) {
        delete buf;
        m_buffers.erase(bufferId);
    }
}

void GLSharedGroup::addProgramData(GLuint program) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);
    if (pData) {
        delete pData;
    }

    m_programs[program] = new ProgramData();
}

void GLSharedGroup::initProgramData(GLuint program, GLuint numIndexes, GLuint numAttributes) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);
    if (pData) {
        pData->initProgramData(numIndexes, numAttributes);
    }
}

void GLSharedGroup::refProgramData(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData = findObjectOrDefault(m_programs, program);
    if (!pData) return;
    pData->incRef();
}

void GLSharedGroup::onUseProgram(GLuint previous, GLuint next) {
    if (previous == next) return;

    AutoLock<Lock> _lock(m_lock);

    if (previous) {
        deleteProgramDataLocked(previous);
    }

    ProgramData* pData = findObjectOrDefault(m_programs, next);
    if (!pData) return;
    pData->incRef();
}

bool GLSharedGroup::isProgramInitialized(GLuint program) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (pData) {
        return pData->isInitialized();
    }

    if (m_shaderProgramIdMap.find(program) == m_shaderProgramIdMap.end()) {
        return false;
    }

    ShaderProgramData* shaderProgramData =
        findObjectOrDefault(m_shaderPrograms, m_shaderProgramIdMap[program]);

    if (shaderProgramData) {
        return shaderProgramData->programData.isInitialized();
    }

    return false;
}

void GLSharedGroup::deleteProgramData(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    deleteProgramDataLocked(program);
}

void GLSharedGroup::deleteProgramDataLocked(GLuint program) {

    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (pData && pData->decRef()) {
        size_t numShaders = pData->getNumShaders();
        for (size_t i = 0; i < numShaders; ++i) {
            // changes the first one
            detachShaderLocked(program, pData->getShader(0));
        }
        delete pData;
        m_programs.erase(program);
    }

    if (m_shaderProgramIdMap.find(program) ==
        m_shaderProgramIdMap.end()) return;

    ShaderProgramData* spData =
        findObjectOrDefault(
            m_shaderPrograms, m_shaderProgramIdMap[program]);

    if (spData) delete spData;

    m_shaderPrograms.erase(m_shaderProgramIdMap[program]);
    m_shaderProgramIdMap.erase(program);
}

// No such thing for separable shader programs.
bool GLSharedGroup::attachShader(GLuint program, GLuint shader) {
    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);
    ShaderData* sData = findObjectOrDefault(m_shaders, shader);

    bool res = false;

    if (pData && sData) {
        res = pData->attachShader(shader, sData->shaderType);
        if (res) {
            refShaderDataLocked(shader);
        }
    }

    return res;
}

bool GLSharedGroup::detachShader(GLuint program, GLuint shader) {
    AutoLock<Lock> _lock(m_lock);
    return detachShaderLocked(program, shader);
}

bool GLSharedGroup::detachShaderLocked(GLuint program, GLuint shader) {
    ProgramData* pData = findObjectOrDefault(m_programs, program);
    ShaderData* sData = findObjectOrDefault(m_shaders, shader);

    bool res = false;

    if (pData && sData) {
        res = pData->detachShader(shader);
        if (res) {
            unrefShaderDataLocked(shader);
        }
    }

    return res;
}

// Not needed/used for separate shader programs.
void GLSharedGroup::setProgramIndexInfo(
    GLuint program, GLuint index, GLint base,
    GLint size, GLenum type, const char* name) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (pData) {
        pData->setIndexInfo(index,base,size,type);
        if (type == GL_SAMPLER_2D) {
            size_t n = pData->getNumShaders();
            for (size_t i = 0; i < n; i++) {
                GLuint shaderId = pData->getShader(i);
                ShaderData* shader = findObjectOrDefault(m_shaders, shaderId);
                if (!shader) continue;
                ShaderData::StringList::iterator nameIter =
                    shader->samplerExternalNames.begin();
                ShaderData::StringList::iterator nameEnd =
                    shader->samplerExternalNames.end();
                while (nameIter != nameEnd) {
                    if (*nameIter == name || *nameIter + "[0]" == name) {
                        pData->setIndexFlags(
                            index,
                            ProgramData::INDEX_FLAG_SAMPLER_EXTERNAL);
                        break;
                    }
                    ++nameIter;
                }
            }
        }
    }
}

void GLSharedGroup::setProgramIndexFlag(GLuint program, GLuint index, GLuint flags) {
    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);
    if (pData) {
        pData->setIndexFlags(index, flags);
    }
}

void GLSharedGroup::setProgramAttribInfo(
    GLuint program, GLuint index, GLint attribLoc,
    GLint size, GLenum type, __attribute__((unused)) const char* name) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = getProgramDataLocked(program);

    if (pData) {
        pData->setAttribInfo(index,attribLoc,size,type);
    }
}

GLenum GLSharedGroup::getProgramUniformType(GLuint program, GLint location) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);
    GLenum type = 0;

    if (pData) {
        type = pData->getTypeForLocation(location);
    }

    if (m_shaderProgramIdMap.find(program) ==
        m_shaderProgramIdMap.end()) return type;

    ShaderProgramData* spData =
        findObjectOrDefault(
            m_shaderPrograms, m_shaderProgramIdMap[program]);

    if (spData) {
        type = spData->programData.getTypeForLocation(location);
    }

    return type;
}

bool GLSharedGroup::isProgram(GLuint program) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (pData) return true;

    if (m_shaderProgramIdMap.find(program) ==
        m_shaderProgramIdMap.end()) return false;

    ShaderProgramData* spData =
        findObjectOrDefault(m_shaderPrograms, m_shaderProgramIdMap[program]);

    if (spData) return true;

    return false;
}

GLint GLSharedGroup::getNextSamplerUniform(
    GLuint program, GLint index, GLint* val, GLenum* target) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (pData) return pData->getNextSamplerUniform(index, val, target);

    if (m_shaderProgramIdMap.find(program) ==
        m_shaderProgramIdMap.end()) return -1;

    ShaderProgramData* spData =
        findObjectOrDefault(
            m_shaderPrograms,
            findObjectOrDefault(m_shaderProgramIdMap, program));

    if (spData) return spData->programData.getNextSamplerUniform(index, val, target);

    return -1;
}

bool GLSharedGroup::setSamplerUniform(
    GLuint program, GLint appLoc, GLint val, GLenum* target) {

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData =
        findObjectOrDefault(m_programs, program);

    if (pData) return pData->setSamplerUniform(appLoc, val, target);

    if (m_shaderProgramIdMap.find(program) ==
        m_shaderProgramIdMap.end()) return false;

    ShaderProgramData* spData =
        findObjectOrDefault(m_shaderPrograms, m_shaderProgramIdMap[program]);

    if (spData) return spData->programData.setSamplerUniform(appLoc, val, target);

    return false;
}

bool GLSharedGroup::isProgramUniformLocationValid(GLuint program, GLint location) {
    if (location < 0) return false;

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData =
        findObjectOrDefault(m_programs, program);

    if (!pData) return false;

    return pData->isValidUniformLocation(location);
}

bool GLSharedGroup::getExternalSamplerUniformIndices(GLuint program,
                                                     std::vector<GLuint>* outIndices) {
    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (!pData) return false;

    pData->getExternalSamplerUniformIndices(outIndices);

    return true;
}

bool GLSharedGroup::isShader(GLuint shader) {

    AutoLock<Lock> _lock(m_lock);

    ShaderData* pData = findObjectOrDefault(m_shaders, shader);

    return pData != NULL;
}

bool GLSharedGroup::addShaderData(GLuint shader, GLenum shaderType) {

    AutoLock<Lock> _lock(m_lock);

    ShaderData* data = new ShaderData;

    if (data) {
        m_shaders[shader] = data;
        data->refcount = 1;
        data->shaderType = shaderType;
    }

    return data != NULL;
}

ShaderData* GLSharedGroup::getShaderData(GLuint shader) {

    AutoLock<Lock> _lock(m_lock);

    return findObjectOrDefault(m_shaders, shader);
}

void GLSharedGroup::unrefShaderData(GLuint shader) {

    AutoLock<Lock> _lock(m_lock);

    unrefShaderDataLocked(shader);
}

void GLSharedGroup::refShaderDataLocked(GLuint shaderId) {
    ShaderData* data = findObjectOrDefault(m_shaders, shaderId);
    data->refcount++;
}

void GLSharedGroup::unrefShaderDataLocked(GLuint shaderId) {
    ShaderData* data = findObjectOrDefault(m_shaders, shaderId);

    if (data && --data->refcount == 0) {

        delete data;

        m_shaders.erase(shaderId);
    }
}

ProgramData* GLSharedGroup::getProgramDataLocked(GLuint program) {
    // Check the space of normal programs, then separable ones
    ProgramData* pData = findObjectOrDefault(m_programs, program);

    if (pData) return pData;

    std::map<GLuint, uint32_t>::const_iterator it =
        m_shaderProgramIdMap.find(program);
    if (it == m_shaderProgramIdMap.end()) return NULL;

    ShaderProgramData* spData = findObjectOrDefault(m_shaderPrograms, it->second);
    if (!spData) return NULL;
    return &spData->programData;
}

uint32_t GLSharedGroup::addNewShaderProgramData() {

    AutoLock<Lock> _lock(m_lock);

    ShaderProgramData* data = new ShaderProgramData;
    uint32_t currId = m_shaderProgramId;

    m_shaderPrograms[currId] = data;
    m_shaderProgramId++;
    return currId;
}

void GLSharedGroup::associateGLShaderProgram(
    GLuint shaderProgramName, uint32_t shaderProgramId) {

    AutoLock<Lock> _lock(m_lock);

    m_shaderProgramIdMap[shaderProgramName] = shaderProgramId;
}

ShaderProgramData* GLSharedGroup::getShaderProgramDataById(uint32_t id) {

    AutoLock<Lock> _lock(m_lock);

    ShaderProgramData* res = findObjectOrDefault(m_shaderPrograms, id);

    return res;
}

ShaderProgramData* GLSharedGroup::getShaderProgramData(
    GLuint shaderProgramName) {

    AutoLock<Lock> _lock(m_lock);

    return findObjectOrDefault(m_shaderPrograms,
                               m_shaderProgramIdMap[shaderProgramName]);
}

void GLSharedGroup::deleteShaderProgramDataById(uint32_t id) {

    AutoLock<Lock> _lock(m_lock);

    ShaderProgramData* data =
        findObjectOrDefault(m_shaderPrograms, id);

    delete data;

    m_shaderPrograms.erase(id);
}


void GLSharedGroup::deleteShaderProgramData(GLuint shaderProgramName) {

    AutoLock<Lock> _lock(m_lock);

    uint32_t id = m_shaderProgramIdMap[shaderProgramName];
    ShaderProgramData* data = findObjectOrDefault(m_shaderPrograms, id);

    delete data;

    m_shaderPrograms.erase(id);
    m_shaderProgramIdMap.erase(shaderProgramName);
}

void GLSharedGroup::initShaderProgramData(GLuint shaderProgram, GLuint numIndices, GLuint numAttributes) {
    ShaderProgramData* spData = getShaderProgramData(shaderProgram);
    spData->programData.initProgramData(numIndices, numAttributes);
}

void GLSharedGroup::setShaderProgramIndexInfo(
    GLuint shaderProgram, GLuint index, GLint base,
    GLint size, GLenum type, const char* name) {

    ShaderProgramData* spData = getShaderProgramData(shaderProgram);
    ProgramData& pData = spData->programData;
    ShaderData& sData = spData->shaderData;

    pData.setIndexInfo(index, base, size, type);

    if (type == GL_SAMPLER_2D) {

        ShaderData::StringList::iterator nameIter =
            sData.samplerExternalNames.begin();
        ShaderData::StringList::iterator nameEnd =
            sData.samplerExternalNames.end();

        while (nameIter != nameEnd) {
            if (*nameIter == name) {
                pData.setIndexFlags(
                    index, ProgramData::INDEX_FLAG_SAMPLER_EXTERNAL);
                break;
            }
            ++nameIter;
        }
    }
}

UniformValidationInfo GLSharedGroup::getUniformValidationInfo(GLuint program) {
    UniformValidationInfo res;

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData =
        getProgramDataLocked(program);

    if (!pData) return res;

    bool error; (void)error;
    return pData->compileValidationInfo(&error);
}

AttribValidationInfo GLSharedGroup::getAttribValidationInfo(GLuint program) {
    AttribValidationInfo res;

    AutoLock<Lock> _lock(m_lock);

    ProgramData* pData =
        getProgramDataLocked(program);

    if (!pData) return res;

    bool error; (void)error;
    return pData->compileAttribValidationInfo(&error);
}

void GLSharedGroup::setProgramLinkStatus(GLuint program, GLint linkStatus) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData =
        getProgramDataLocked(program);
    if (!pData) return;
    pData->setLinkStatus(linkStatus);
}

GLint GLSharedGroup::getProgramLinkStatus(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData = getProgramDataLocked(program);
    if (!pData) return 0;
    return pData->getLinkStatus();
}

void GLSharedGroup::setActiveUniformBlockCountForProgram(GLuint program, GLint count) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData =
        getProgramDataLocked(program);

    if (!pData) return;

    pData->setActiveUniformBlockCount(count);
}

GLint GLSharedGroup::getActiveUniformBlockCount(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData =
        getProgramDataLocked(program);

    if (!pData) return 0;

    return pData->getActiveUniformBlockCount();
}

void GLSharedGroup::setTransformFeedbackVaryingsCountForProgram(GLuint program, GLint count) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData = getProgramDataLocked(program);
    if (!pData) return;
    pData->setTransformFeedbackVaryingsCount(count);
}

GLint GLSharedGroup::getTransformFeedbackVaryingsCountForProgram(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData = getProgramDataLocked(program);
    if (!pData) return 0;
    return pData->getTransformFeedbackVaryingsCount();
}

int GLSharedGroup::getActiveUniformsCountForProgram(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData =
        getProgramDataLocked(program);

    if (!pData) return 0;

    return pData->getActiveUniformsCount();
}

int GLSharedGroup::getActiveAttributesCountForProgram(GLuint program) {
    AutoLock<Lock> _lock(m_lock);
    ProgramData* pData =
        getProgramDataLocked(program);

    if (!pData) return 0;

    return pData->getActiveAttributesCount();
}

}  // namespace guest
}  // namespace gfxstream
