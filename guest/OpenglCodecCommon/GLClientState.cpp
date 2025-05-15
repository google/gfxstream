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
#include "gfxstream/guest/GLClientState.h"

#include <optional>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GLESTextureUtils.h"
#include "gfxstream/common/logging.h"
#include "glUtils.h"

#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

// Don't include these in the .h file, or we get weird compile errors.
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl31.h>

using gfxstream::guest::AutoReadLock;
using gfxstream::guest::AutoWriteLock;

namespace gfxstream {
namespace guest {

void GLClientState::init() {
    m_initialized = false;

    state_GL_STENCIL_TEST = false;
    state_GL_STENCIL_FUNC = GL_ALWAYS;
    state_GL_STENCIL_VALUE_MASK = ~(0);
    state_GL_STENCIL_REF = 0;
    state_GL_STENCIL_FAIL = GL_KEEP;
    state_GL_STENCIL_PASS_DEPTH_FAIL = GL_KEEP;
    state_GL_STENCIL_PASS_DEPTH_PASS = GL_KEEP;
    state_GL_STENCIL_BACK_FUNC = GL_ALWAYS;
    state_GL_STENCIL_BACK_VALUE_MASK = ~(0);
    state_GL_STENCIL_BACK_REF = 0;
    state_GL_STENCIL_BACK_FAIL = GL_KEEP;
    state_GL_STENCIL_BACK_PASS_DEPTH_FAIL = GL_KEEP;
    state_GL_STENCIL_BACK_PASS_DEPTH_PASS = GL_KEEP;
    state_GL_STENCIL_WRITEMASK = ~(0);
    state_GL_STENCIL_BACK_WRITEMASK = ~(0);
    state_GL_STENCIL_CLEAR_VALUE = 0;


    m_arrayBuffer = 0;
    m_arrayBuffer_lastEncode = 0;

    m_attribEnableCache = 0;
    m_vaoAttribBindingCacheInvalid = 0xffff;
    m_vaoAttribBindingHasClientArrayCache = 0;
    m_vaoAttribBindingHasVboCache = 0;
    m_noClientArraysCache = 0;

    addVertexArrayObject(0);
    setVertexArrayObject(0);
    // init gl constans;
    m_currVaoState[VERTEX_LOCATION].glConst = GL_VERTEX_ARRAY;
    m_currVaoState[NORMAL_LOCATION].glConst = GL_NORMAL_ARRAY;
    m_currVaoState[COLOR_LOCATION].glConst = GL_COLOR_ARRAY;
    m_currVaoState[POINTSIZE_LOCATION].glConst = GL_POINT_SIZE_ARRAY_OES;
    m_currVaoState[TEXCOORD0_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD1_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD2_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD3_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD4_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD5_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD6_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[TEXCOORD7_LOCATION].glConst = GL_TEXTURE_COORD_ARRAY;
    m_currVaoState[MATRIXINDEX_LOCATION].glConst = GL_MATRIX_INDEX_ARRAY_OES;
    m_currVaoState[WEIGHT_LOCATION].glConst = GL_WEIGHT_ARRAY_OES;

    m_copyReadBuffer = 0;
    m_copyWriteBuffer = 0;
    m_pixelPackBuffer = 0;
    m_pixelUnpackBuffer = 0;
    m_transformFeedbackBuffer = 0;
    m_uniformBuffer = 0;
    m_atomicCounterBuffer = 0;
    m_dispatchIndirectBuffer = 0;
    m_drawIndirectBuffer = 0;
    m_shaderStorageBuffer = 0;
    m_textureBuffer = 0;

    m_transformFeedbackActive = false;
    m_transformFeedbackUnpaused = false;
    m_transformFeedbackVaryingsCountForLinking = 0;

    m_activeTexture = 0;
    m_currentProgram = 0;
    m_currentShaderProgram = 0;

    m_pixelStore.unpack_alignment = 4;
    m_pixelStore.pack_alignment = 4;

    m_pixelStore.unpack_row_length = 0;
    m_pixelStore.unpack_image_height = 0;
    m_pixelStore.unpack_skip_pixels = 0;
    m_pixelStore.unpack_skip_rows = 0;
    m_pixelStore.unpack_skip_images = 0;

    m_pixelStore.pack_row_length = 0;
    m_pixelStore.pack_skip_pixels = 0;
    m_pixelStore.pack_skip_rows = 0;

    memset(m_tex.unit, 0, sizeof(m_tex.unit));
    m_tex.activeUnit = &m_tex.unit[0];
    m_tex.textureRecs = NULL;

    mRboState.boundRenderbuffer = nullptr;

    mFboState.boundDrawFramebuffer = 0;
    mFboState.boundReadFramebuffer = 0;
    mFboState.drawFboCheckStatus = GL_NONE;
    mFboState.readFboCheckStatus = GL_NONE;

    m_extensions_set = false;

    // The default transform feedback buffer object
    // The default sampler object
    GLuint defaultId = 0;
    setExistence(ObjectType::TransformFeedback, true, 1, &defaultId);

    mBoundTransformFeedbackValidity.id = 0;
    mBoundTransformFeedbackValidity.valid = true;

    // query must take id that was created via glGenQueries
    mBoundQueryValidity_AnySamplesPassed.valid = false;
    mBoundQueryValidity_AnySamplesPassedConservative.valid = false;
    mBoundQueryValidity_TransformFeedbackPrimitivesWritten.valid = false;
}

GLClientState::GLClientState()
{
    init();
}

GLClientState::GLClientState(int majorVersion, int minorVersion) :
    m_glesMajorVersion(majorVersion),
    m_glesMinorVersion(minorVersion) {
    init();
}

GLClientState::~GLClientState()
{
}

void GLClientState::enable(int location, int state)
{
    m_currVaoState[location].enableDirty |= (state != m_currVaoState[location].enabled);
    m_currVaoState[location].enabled = state;
    if (state) {
        m_attribEnableCache |= (1 << location);
        m_noClientArraysCache = 0;
    } else {
        m_attribEnableCache &= ~(1 << location);
    }
}

void GLClientState::setVertexAttribState(int location, int size, GLenum type, GLboolean normalized, GLsizei stride, const void *data, bool isInt)
{
    m_currVaoState[location].size = size;
    m_currVaoState[location].type = type;
    m_currVaoState[location].stride = stride;
    m_currVaoState[location].data = (void*)data;
    m_currVaoState[location].bufferObject = m_arrayBuffer;
    m_currVaoState[location].elementSize = size ? (glSizeof(type) * size) : 0;
    switch (type) {
        case GL_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            m_currVaoState[location].elementSize =
                m_currVaoState[location].elementSize / 4;
            break;
        default:
            break;
    }
    m_currVaoState[location].normalized = normalized;
    m_currVaoState[location].isInt = isInt;
}

void GLClientState::setVertexBindingDivisor(int bindingindex, GLuint divisor) {
    m_currVaoState.bufferBinding(bindingindex).divisor = divisor;
}

const GLClientState::BufferBinding& GLClientState::getCurrAttributeBindingInfo(int attribindex) {
    return m_currVaoState.bufferBindings_const()[m_currVaoState[attribindex].bindingindex];
}

void GLClientState::setVertexAttribBinding(int attribindex, int bindingindex) {
    m_currVaoState[attribindex].bindingindex = bindingindex;
    m_currVaoState.bufferBinding(bindingindex).vertexAttribLoc = attribindex;
    m_vaoAttribBindingCacheInvalid |= (1 << attribindex);
    m_noClientArraysCache = 0;
}

void GLClientState::setVertexAttribFormat(int location, int size, GLenum type, GLboolean normalized, GLuint reloffset, bool isInt) {
    m_currVaoState[location].size = size;
    m_currVaoState[location].type = type;
    m_currVaoState[location].normalized = normalized;
    m_currVaoState[location].reloffset = reloffset;
    m_currVaoState[location].elementSize = size ? (glSizeof(type) * size) : 0;
    switch (type) {
        case GL_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            m_currVaoState[location].elementSize =
                m_currVaoState[location].elementSize / 4;
            break;
        default:
            break;
    }
    m_currVaoState[location].isInt = isInt;
}

void GLClientState::addVertexArrayObjects(GLsizei n, GLuint* arrays) {
    for (GLsizei i = 0; i < n; i++) {
        addVertexArrayObject(arrays[i]);
    }
}

void GLClientState::removeVertexArrayObjects(GLsizei n, const GLuint* arrays) {
    for (GLsizei i = 0; i < n; i++) {
        if (arrays[i] && m_currVaoState.vaoId() == arrays[i]) {
            setVertexArrayObject(0);
        }
        removeVertexArrayObject(arrays[i]);
    }
}

void GLClientState::addVertexArrayObject(GLuint name) {
    if (m_vaoMap.find(name) !=
        m_vaoMap.end()) {
        GFXSTREAM_ERROR("%u already part of current VAO state!", name);
        return;
    }

    m_vaoMap.insert(
            VAOStateMap::value_type(
                name,
                VAOState(0, CODEC_MAX_VERTEX_ATTRIBUTES, CODEC_MAX_VERTEX_ATTRIBUTES)));
    VertexAttribStateVector& attribState =
        m_vaoMap.find(name)->second.attribState;
    for (int i = 0; i < CODEC_MAX_VERTEX_ATTRIBUTES; i++) {
        attribState[i].enabled = 0;
        attribState[i].enableDirty = false;
        attribState[i].data = 0;
        attribState[i].reloffset = 0;
        attribState[i].bindingindex = i;
        attribState[i].divisor = 0;
        attribState[i].size = 4; // 4 is the default size
        attribState[i].type = GL_FLOAT; // GL_FLOAT is the default type
    }

    VertexAttribBindingVector& bindingState =
        m_vaoMap.find(name)->second.bindingState;
    for (size_t i = 0; i < bindingState.size(); i++) {
        bindingState[i].effectiveStride = 16;
    }
}

void GLClientState::removeVertexArrayObject(GLuint name) {
    if (name == 0) {
        GFXSTREAM_ERROR("Cannot delete VAO 0!");
        return;
    }
    if (m_vaoMap.find(name) ==
        m_vaoMap.end()) {
        GFXSTREAM_ERROR("%u not found in VAO state!", name);
        return;
    }
    m_vaoMap.erase(name);
}

void GLClientState::setVertexArrayObject(GLuint name) {
    if (m_vaoMap.find(name) ==
        m_vaoMap.end()) {
        GFXSTREAM_ERROR("%u not found in VAO state!", name);
        return;
    }

    if (name && m_currVaoState.vaoId() == name) {
        GFXSTREAM_ERROR("set vao to self, no-op (%u)", name);
        return;
    }

    m_currVaoState =
        VAOStateRef(m_vaoMap.find(name));
}

bool GLClientState::isVertexArrayObject(GLuint vao) const {
    return m_vaoMap.find(vao) != m_vaoMap.end();
}

void GLClientState::getVBOUsage(bool* hasClientArrays, bool* hasVBOs) {
    uint8_t todo_count = 0;
    uint8_t todo[CODEC_MAX_VERTEX_ATTRIBUTES];

    if (m_noClientArraysCache) {
        *hasClientArrays = false;
        *hasVBOs = true;
        return;
    }

    for (int i = 0; i < CODEC_MAX_VERTEX_ATTRIBUTES; i++) {
        if ((1 << i) & (m_attribEnableCache)) {
            if (!((1 << i) & m_vaoAttribBindingCacheInvalid)) {
                if ((1 << i) & m_vaoAttribBindingHasClientArrayCache) {
                    *hasClientArrays = true;
                }
                if ((1 << i) & m_vaoAttribBindingHasVboCache) {
                    *hasVBOs = true;
                }
                if (*hasClientArrays && *hasVBOs) return;
            } else {
                todo[todo_count] = i;
                ++todo_count;
            }
        }
    }

    if (todo_count == 0 &&
        !(*hasClientArrays) &&
        *hasVBOs) {
        m_noClientArraysCache = 1;
    }

    for (int k = 0; k < todo_count; ++k) {
        int i = todo[k];
        const GLClientState::BufferBinding& curr_binding =
            m_currVaoState.bufferBindings_const()[
                m_currVaoState[i].bindingindex];
        GLuint bufferObject = curr_binding.buffer;
        if (bufferObject == 0 && curr_binding.offset && hasClientArrays) {
            *hasClientArrays = true;
            m_vaoAttribBindingHasClientArrayCache |= (1 << i);
        } else {
            m_vaoAttribBindingHasClientArrayCache &= ~(1 << i);
        }
        if (bufferObject != 0 && hasVBOs) {
            *hasVBOs = true;
            m_vaoAttribBindingHasVboCache |= (1 << i);
        } else {
            m_vaoAttribBindingHasVboCache &= ~(1 << i);
        }
        m_vaoAttribBindingCacheInvalid &= ~(1 << i);
        if (*hasClientArrays && *hasVBOs) return;
    }

    if (!(*hasClientArrays) &&
        *hasVBOs) {
        m_noClientArraysCache = 1;
    }
}

const GLClientState::VertexAttribState& GLClientState::getState(int location) {
    return m_currVaoState[location];
}

const GLClientState::VertexAttribState& GLClientState::getStateAndEnableDirty(int location, bool *enableChanged)
{
    if (enableChanged) {
        *enableChanged = m_currVaoState[location].enableDirty;
    }

    m_currVaoState[location].enableDirty = false;
    return m_currVaoState[location];
}

void GLClientState::updateEnableDirtyArrayForDraw() {
    bool enableChanged;
    VAOState& vaoState = m_currVaoState.vaoState();

    int k = 0;
    for (int i = 0; i < CODEC_MAX_VERTEX_ATTRIBUTES; ++i) {
        const VertexAttribState &state = getStateAndEnableDirty(i, &enableChanged);
        if (enableChanged || state.enabled) {
            vaoState.attributesNeedingUpdateForDraw[k] = i;
            ++k;
        }
    }
    vaoState.numAttributesNeedingUpdateForDraw = k;
}

GLClientState::VAOState& GLClientState::currentVaoState() {
    return m_currVaoState.vaoState();
}

int GLClientState::getLocation(GLenum loc)
{
    int retval;

    switch(loc) {
    case GL_VERTEX_ARRAY:
        retval = int(VERTEX_LOCATION);
        break;
    case GL_NORMAL_ARRAY:
        retval = int(NORMAL_LOCATION);
        break;
    case GL_COLOR_ARRAY:
        retval = int(COLOR_LOCATION);
        break;
    case GL_POINT_SIZE_ARRAY_OES:
        retval = int(POINTSIZE_LOCATION);
        break;
    case GL_TEXTURE_COORD_ARRAY:
        retval = int (TEXCOORD0_LOCATION + m_activeTexture);
        break;
    case GL_MATRIX_INDEX_ARRAY_OES:
        retval = int (MATRIXINDEX_LOCATION);
        break;
    case GL_WEIGHT_ARRAY_OES:
        retval = int (WEIGHT_LOCATION);
        break;
    default:
        retval = loc;
    }
    return retval;
}

static void sClearIndexedBufferBinding(GLuint id, std::vector<GLClientState::BufferBinding>& bindings) {
    for (size_t i = 0; i < bindings.size(); i++) {
        if (bindings[i].buffer == id) {
            bindings[i].offset = 0;
            bindings[i].stride = 0;
            bindings[i].effectiveStride = 16;
            bindings[i].size = 0;
            bindings[i].buffer = 0;
        }
    }
}

void GLClientState::addBuffer(GLuint id) {
    mBufferIds.add(id);
    mBufferIds.set(id, true);
    mHostMappedBufferDirty.add(id);
}

void GLClientState::removeBuffer(GLuint id) {
    mHostMappedBufferDirty.remove(id);
    mBufferIds.remove(id);
}

bool GLClientState::bufferIdExists(GLuint id) const {
    return mBufferIds.get(id);
}

void GLClientState::setBufferHostMapDirty(GLuint id, bool dirty) {
    mHostMappedBufferDirty.set(id, dirty);
}

bool GLClientState::isBufferHostMapDirty(GLuint id) const {
    return mHostMappedBufferDirty.get(id);
}

void GLClientState::setExistence(ObjectType type, bool exists, GLsizei count, const GLuint* ids) {
    if (type == ObjectType::Sampler) {
        SamplerInfo::ScopedView view(mSamplerInfo);
        if (exists) {
            for (GLsizei i = 0; i < count; ++i) {
                view.addFresh(ids[i]);
            }
        } else {
            for (GLsizei i = 0; i < count; ++i) {
                view.unref(ids[i]);
            }
        }
    } else {
        ExistenceMap* existenceMap = &mBufferIds;

        switch (type) {
            case ObjectType::Buffer:
                existenceMap = &mBufferIds;
                break;
            case ObjectType::TransformFeedback:
                existenceMap = &mTransformFeedbackIds;
                break;
            case ObjectType::Query:
                existenceMap = &mQueryIds;
                for (GLsizei i = 0; i < count; ++i) {
                    // reset the last query target
                    mLastQueryTargets.add(ids[i], 0);
                }
                break;
            case ObjectType::Sampler:
            default:
                GFXSTREAM_FATAL("Unreachable code.");
        }

        if (exists) {
            for (GLsizei i = 0; i < count; ++i) {
                existenceMap->add(ids[i]);
                existenceMap->set(ids[i], true);
            }
        } else {
            for (GLsizei i = 0; i < count; ++i) {
                existenceMap->remove(ids[i]);
            }
        }
    }
}

bool GLClientState::queryExistence(ObjectType type, GLuint id) const {
    switch (type) {
        case ObjectType::Buffer:
            return mBufferIds.get(id);
        case ObjectType::TransformFeedback:
            return mTransformFeedbackIds.get(id);
        case ObjectType::Sampler:
            return samplerExists(id);
        case ObjectType::Query:
            return mQueryIds.get(id);
        default:
            GFXSTREAM_FATAL("Unknown object type: 0x%x", type);
            abort();
    }
}

bool GLClientState::samplerExists(GLuint id) const {
    if (!id) return true;
    SamplerInfo::ScopedView view(mSamplerInfo);
    return view.samplerExists(id);
}

bool GLClientState::tryBind(GLenum target, GLuint id) {
    if (0 == id) { // unbind operation
        switch (target) {
            case GL_TRANSFORM_FEEDBACK:
                mBoundTransformFeedbackValidity.id = 0;
                mBoundTransformFeedbackValidity.valid = true;
                break;
            case GL_ANY_SAMPLES_PASSED:
                mBoundQueryValidity_AnySamplesPassed.id = 0;
                mBoundQueryValidity_AnySamplesPassed.valid = false;
                break;
            case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
                mBoundQueryValidity_AnySamplesPassedConservative.id = 0;
                mBoundQueryValidity_AnySamplesPassedConservative.valid = false;
                break;
            case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
                mBoundQueryValidity_TransformFeedbackPrimitivesWritten.id = 0;
                mBoundQueryValidity_TransformFeedbackPrimitivesWritten.valid = false;
                break;
            default:
                GFXSTREAM_FATAL("Target 0x%x not yet supported in new state tracking model", target);
        }
        return true;
    }

    switch (target) {
        case GL_TRANSFORM_FEEDBACK:
            if (!queryExistence(ObjectType::TransformFeedback, id)) return false;
            break;
        case GL_ANY_SAMPLES_PASSED:
        case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
            if (!queryExistence(ObjectType::Query, id)) {
                return false;
            }
            break;
        default:
            GFXSTREAM_FATAL("Target 0x%x not yet supported in new state tracking model", target);
            abort();
    }

    // valid bind
    switch (target) {
    case GL_TRANSFORM_FEEDBACK:
        mBoundTransformFeedbackValidity.id = id;
        mBoundTransformFeedbackValidity.valid = true;
        break;
    case GL_ANY_SAMPLES_PASSED:
        mBoundQueryValidity_AnySamplesPassed.id = id;
        mBoundQueryValidity_AnySamplesPassed.valid = true;
        break;
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        mBoundQueryValidity_AnySamplesPassedConservative.id = id;
        mBoundQueryValidity_AnySamplesPassedConservative.valid = true;
        break;
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
        mBoundQueryValidity_TransformFeedbackPrimitivesWritten.id = id;
        mBoundQueryValidity_TransformFeedbackPrimitivesWritten.valid = true;
        break;
    default:
        GFXSTREAM_ERROR("Target 0x%x not yet supported in new state tracking model", target);
        abort();
    }
    return true;
}

bool GLClientState::isBoundTargetValid(GLenum target) {
    switch (target) {
    case GL_TRANSFORM_FEEDBACK:
        return mBoundTransformFeedbackValidity.valid;
    case GL_ANY_SAMPLES_PASSED:
        return mBoundQueryValidity_AnySamplesPassed.valid;
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        return mBoundQueryValidity_AnySamplesPassedConservative.valid;
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
        return mBoundQueryValidity_TransformFeedbackPrimitivesWritten.valid;
    default:
        GFXSTREAM_FATAL("Target 0x%x not yet supported in new state tracking model", target);
        abort();
    }
}

bool GLClientState::isQueryBound(GLenum target) {
    switch (target) {
    case GL_ANY_SAMPLES_PASSED:
        return mBoundQueryValidity_AnySamplesPassed.valid;
    case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
        return mBoundQueryValidity_AnySamplesPassedConservative.valid;
    case GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN:
        return mBoundQueryValidity_TransformFeedbackPrimitivesWritten.valid;
    default:
        return false;
    }
}

bool GLClientState::isQueryObjectActive(GLuint id) {
    if (mBoundQueryValidity_AnySamplesPassed.valid &&
        (id == mBoundQueryValidity_AnySamplesPassed.id))
        return true;
    if (mBoundQueryValidity_AnySamplesPassedConservative.valid &&
        (id == mBoundQueryValidity_AnySamplesPassedConservative.id))
        return true;
    if (mBoundQueryValidity_TransformFeedbackPrimitivesWritten.valid &&
        (id == mBoundQueryValidity_TransformFeedbackPrimitivesWritten.id))
        return true;
    return false;
}

void GLClientState::setLastQueryTarget(GLenum target, GLuint id) {
    mLastQueryTargets.add(id, target);
}

GLenum GLClientState::getLastQueryTarget(GLuint id) {
    auto targetPtr = mLastQueryTargets.get_const(id);
    if (!targetPtr) return 0;
    return *targetPtr;
}

void GLClientState::setBoundPixelPackBufferDirtyForHostMap() {
    if (m_pixelPackBuffer)
        setBufferHostMapDirty(m_pixelPackBuffer, true /* dirty */);
}

void GLClientState::setBoundTransformFeedbackBuffersDirtyForHostMap() {
    if (m_transformFeedbackBuffer)
        setBufferHostMapDirty(
            m_transformFeedbackBuffer,
            true /* dirty */);

    for (size_t i = 0; i < m_indexedTransformFeedbackBuffers.size(); ++i)
        if (m_indexedTransformFeedbackBuffers[i].buffer)
            setBufferHostMapDirty(
                m_indexedTransformFeedbackBuffers[i].buffer,
                true /* dirty */);
}

void GLClientState::setBoundShaderStorageBuffersDirtyForHostMap() {
    if (m_glesMajorVersion == 3 && m_glesMinorVersion == 0) return;

    if (m_shaderStorageBuffer)
        setBufferHostMapDirty(
            m_shaderStorageBuffer,
            true /* dirty */);

    for (size_t i = 0; i < m_indexedShaderStorageBuffers.size(); ++i)
        if (m_indexedShaderStorageBuffers[i].buffer)
            setBufferHostMapDirty(
                m_indexedShaderStorageBuffers[i].buffer,
                true /* dirty */);
}

void GLClientState::setBoundAtomicCounterBuffersDirtyForHostMap() {
    if (m_glesMajorVersion == 3 && m_glesMinorVersion == 0) return;

    if (m_atomicCounterBuffer)
        setBufferHostMapDirty(
            m_atomicCounterBuffer,
            true /* dirty */);

    for (size_t i = 0; i < m_indexedAtomicCounterBuffers.size(); ++i)
        if (m_indexedAtomicCounterBuffers[i].buffer)
            setBufferHostMapDirty(
                m_indexedAtomicCounterBuffers[i].buffer,
                true /* dirty */);
}

void GLClientState::unBindBuffer(GLuint id) {
    if (m_arrayBuffer == id) {
        m_arrayBuffer = 0;
        m_arrayBuffer_lastEncode = 0;
    }

    if (m_currVaoState.iboId() == id) {
        m_currVaoState.iboId() = 0;
        m_currVaoState.iboIdLastEncode() = 0;
    }

    if (m_copyReadBuffer == id)
        m_copyReadBuffer = 0;
    if (m_copyWriteBuffer == id)
        m_copyWriteBuffer = 0;
    if (m_pixelPackBuffer == id)
        m_pixelPackBuffer = 0;
    if (m_pixelUnpackBuffer == id)
        m_pixelUnpackBuffer = 0;
    if (m_transformFeedbackBuffer == id)
        m_transformFeedbackBuffer = 0;
    if (m_uniformBuffer == id)
        m_uniformBuffer = 0;
    if (m_atomicCounterBuffer == id)
        m_atomicCounterBuffer = 0;
    if (m_dispatchIndirectBuffer == id)
        m_dispatchIndirectBuffer = 0;
    if (m_drawIndirectBuffer == id)
        m_drawIndirectBuffer = 0;
    if (m_shaderStorageBuffer == id)
        m_shaderStorageBuffer = 0;
    if (m_textureBuffer == id)
        m_textureBuffer = 0;

    sClearIndexedBufferBinding(id, m_indexedTransformFeedbackBuffers);
    sClearIndexedBufferBinding(id, m_indexedUniformBuffers);
    sClearIndexedBufferBinding(id, m_indexedAtomicCounterBuffers);
    sClearIndexedBufferBinding(id, m_indexedShaderStorageBuffers);
    sClearIndexedBufferBinding(id, m_currVaoState.bufferBindings());
    m_vaoAttribBindingCacheInvalid = 0xffff;
    m_noClientArraysCache = 0;
}

int GLClientState::bindBuffer(GLenum target, GLuint id)
{
    int err = 0;
    switch(target) {
    case GL_ARRAY_BUFFER:
        m_arrayBuffer = id;
        break;
    case GL_ELEMENT_ARRAY_BUFFER:
        m_currVaoState.iboId() = id;
        break;
    case GL_COPY_READ_BUFFER:
        m_copyReadBuffer = id;
        break;
    case GL_COPY_WRITE_BUFFER:
        m_copyWriteBuffer = id;
        break;
    case GL_PIXEL_PACK_BUFFER:
        m_pixelPackBuffer = id;
        break;
    case GL_PIXEL_UNPACK_BUFFER:
        m_pixelUnpackBuffer = id;
        break;
    case GL_TRANSFORM_FEEDBACK_BUFFER:
        m_transformFeedbackBuffer = id;
        break;
    case GL_UNIFORM_BUFFER:
        m_uniformBuffer = id;
        break;
    case GL_ATOMIC_COUNTER_BUFFER:
        m_atomicCounterBuffer = id;
        break;
    case GL_DISPATCH_INDIRECT_BUFFER:
        m_dispatchIndirectBuffer = id;
        break;
    case GL_DRAW_INDIRECT_BUFFER:
        m_drawIndirectBuffer = id;
        break;
    case GL_SHADER_STORAGE_BUFFER:
        m_shaderStorageBuffer = id;
        break;
    case GL_TEXTURE_BUFFER_OES:
        m_textureBuffer = id;
        break;
    default:
        err = -1;
    }
    return err;
}

void GLClientState::bindIndexedBuffer(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, GLintptr stride, GLintptr effectiveStride) {
    switch (target) {
    case GL_TRANSFORM_FEEDBACK_BUFFER:
        m_indexedTransformFeedbackBuffers[index].buffer = buffer;
        m_indexedTransformFeedbackBuffers[index].offset = offset;
        m_indexedTransformFeedbackBuffers[index].size = size;
        m_indexedTransformFeedbackBuffers[index].stride = stride;
        break;
    case GL_UNIFORM_BUFFER:
        m_indexedUniformBuffers[index].buffer = buffer;
        m_indexedUniformBuffers[index].offset = offset;
        m_indexedUniformBuffers[index].size = size;
        m_indexedUniformBuffers[index].stride = stride;
        break;
    case GL_ATOMIC_COUNTER_BUFFER:
        m_indexedAtomicCounterBuffers[index].buffer = buffer;
        m_indexedAtomicCounterBuffers[index].offset = offset;
        m_indexedAtomicCounterBuffers[index].size = size;
        m_indexedAtomicCounterBuffers[index].stride = stride;
        break;
    case GL_SHADER_STORAGE_BUFFER:
        m_indexedShaderStorageBuffers[index].buffer = buffer;
        m_indexedShaderStorageBuffers[index].offset = offset;
        m_indexedShaderStorageBuffers[index].size = size;
        m_indexedShaderStorageBuffers[index].stride = stride;
        break;
    default:
        m_currVaoState.bufferBinding(index).buffer = buffer;
        m_currVaoState.bufferBinding(index).offset = offset;
        m_currVaoState.bufferBinding(index).size = size;
        m_currVaoState.bufferBinding(index).stride = stride;
        m_currVaoState.bufferBinding(index).effectiveStride = effectiveStride;
        m_vaoAttribBindingCacheInvalid |= (1 << m_currVaoState.bufferBinding(index).vertexAttribLoc);
        return;
    }
}

int GLClientState::getMaxIndexedBufferBindings(GLenum target) const {
    switch (target) {
    case GL_TRANSFORM_FEEDBACK_BUFFER:
        return m_indexedTransformFeedbackBuffers.size();
    case GL_UNIFORM_BUFFER:
        return m_indexedUniformBuffers.size();
    case GL_ATOMIC_COUNTER_BUFFER:
        return m_indexedAtomicCounterBuffers.size();
    case GL_SHADER_STORAGE_BUFFER:
        return m_indexedShaderStorageBuffers.size();
    default:
        return m_currVaoState.bufferBindings_const().size();
    }
}

bool GLClientState::isNonIndexedBindNoOp(GLenum target, GLuint buffer) {
    if (buffer != getLastEncodedBufferBind(target)) return false;

    int idOrError = getBuffer(target);
    if (idOrError < 0) {
        return false;
    } else {
        return buffer == (GLuint)idOrError;
    }
}

bool GLClientState::isIndexedBindNoOp(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, GLintptr stride, GLintptr effectiveStride) {

    if (target == GL_TRANSFORM_FEEDBACK_BUFFER) return false;

    if (buffer != getLastEncodedBufferBind(target)) return false;

    switch (target) {
    case GL_TRANSFORM_FEEDBACK_BUFFER:
        return m_indexedTransformFeedbackBuffers[index].buffer == buffer &&
               m_indexedTransformFeedbackBuffers[index].offset == offset &&
               m_indexedTransformFeedbackBuffers[index].size == size &&
               m_indexedTransformFeedbackBuffers[index].stride == stride;
    case GL_UNIFORM_BUFFER:
        return m_indexedUniformBuffers[index].buffer == buffer &&
               m_indexedUniformBuffers[index].offset == offset &&
               m_indexedUniformBuffers[index].size == size &&
               m_indexedUniformBuffers[index].stride == stride;
    case GL_ATOMIC_COUNTER_BUFFER:
        return m_indexedAtomicCounterBuffers[index].buffer == buffer &&
               m_indexedAtomicCounterBuffers[index].offset == offset &&
               m_indexedAtomicCounterBuffers[index].size == size &&
               m_indexedAtomicCounterBuffers[index].stride == stride;
    case GL_SHADER_STORAGE_BUFFER:
        return m_indexedShaderStorageBuffers[index].buffer == buffer &&
               m_indexedShaderStorageBuffers[index].offset == offset &&
               m_indexedShaderStorageBuffers[index].size == size &&
               m_indexedShaderStorageBuffers[index].stride == stride;
    default:
        return m_currVaoState.bufferBinding(index).buffer == buffer &&
               m_currVaoState.bufferBinding(index).offset == offset &&
               m_currVaoState.bufferBinding(index).size == size &&
               m_currVaoState.bufferBinding(index).stride == stride &&
               m_currVaoState.bufferBinding(index).effectiveStride == effectiveStride;
    }
}

int GLClientState::getMaxTextureSize() const {
    return m_hostDriverCaps.max_texture_size;
}

int GLClientState::getMaxTextureSize3D() const {
    return m_hostDriverCaps.max_texture_size_3d;
}

int GLClientState::getMaxTextureSizeCubeMap() const {
    return m_hostDriverCaps.max_texture_size_cube_map;
}

int GLClientState::getLog2MaxTextureSize() const {
    return m_log2MaxTextureSize;
}

void GLClientState::postDraw() {
    setBoundTransformFeedbackBuffersDirtyForHostMap();
    setBoundShaderStorageBuffersDirtyForHostMap();
    setBoundAtomicCounterBuffersDirtyForHostMap();
}

void GLClientState::postReadPixels() {
    setBoundPixelPackBufferDirtyForHostMap();
}

void GLClientState::postDispatchCompute() {
    setBoundShaderStorageBuffersDirtyForHostMap();
    setBoundAtomicCounterBuffersDirtyForHostMap();
}

bool GLClientState::shouldSkipHostMapBuffer(GLenum target) {
    GLuint id = getBuffer(target);
    return !isBufferHostMapDirty(id);
}

void GLClientState::onHostMappedBuffer(GLenum target) {
    GLuint id = getBuffer(target);
    setBufferHostMapDirty(id, false /* not dirty */);
}

int GLClientState::getBuffer(GLenum target) {
    int ret=0;
    switch (target) {
        case GL_ARRAY_BUFFER:
            ret = m_arrayBuffer;
            break;
        case GL_ELEMENT_ARRAY_BUFFER:
            ret = m_currVaoState.iboId();
            break;
        case GL_COPY_READ_BUFFER:
            ret = m_copyReadBuffer;
            break;
        case GL_COPY_WRITE_BUFFER:
            ret = m_copyWriteBuffer;
            break;
        case GL_PIXEL_PACK_BUFFER:
            ret = m_pixelPackBuffer;
            break;
        case GL_PIXEL_UNPACK_BUFFER:
            ret = m_pixelUnpackBuffer;
            break;
        case GL_TRANSFORM_FEEDBACK_BUFFER:
            ret = m_transformFeedbackBuffer;
            break;
        case GL_UNIFORM_BUFFER:
            ret = m_uniformBuffer;
            break;
        case GL_ATOMIC_COUNTER_BUFFER:
            ret = m_atomicCounterBuffer;
            break;
        case GL_DISPATCH_INDIRECT_BUFFER:
            ret = m_dispatchIndirectBuffer;
            break;
        case GL_DRAW_INDIRECT_BUFFER:
            ret = m_drawIndirectBuffer;
            break;
        case GL_SHADER_STORAGE_BUFFER:
            ret = m_shaderStorageBuffer;
            break;
        case GL_TEXTURE_BUFFER_OES:
            ret = m_textureBuffer;
            break;
        default:
            ret = -1;
    }
    return ret;
}

GLuint GLClientState::getLastEncodedBufferBind(GLenum target) {
    GLuint ret;
    switch (target)
    {
    case GL_ARRAY_BUFFER:
        ret = m_arrayBuffer_lastEncode;
        break;
    case GL_ELEMENT_ARRAY_BUFFER:
        ret = m_currVaoState.iboIdLastEncode();
        break;
    default:
    {
        int idOrError = getBuffer(target);
        ret = (idOrError < 0) ? 0 : (GLuint)idOrError;
    }
    }

    return ret;
}

void GLClientState::setLastEncodedBufferBind(GLenum target, GLuint id)
{
    switch (target)
    {
    case GL_ARRAY_BUFFER:
        m_arrayBuffer_lastEncode = id;
        break;
    case GL_ELEMENT_ARRAY_BUFFER:
        m_currVaoState.iboIdLastEncode() = id;
        break;
    default:
        break;
    }
}

bool GLClientState::isTexture(GLuint tex_name) const {
    return getTextureRec(tex_name) != nullptr;
}

bool GLClientState::isTextureWithStorage(GLuint tex_name) const {
    TextureRec* rec = getTextureRecPtr(tex_name);
    if (!rec) return false;
    return rec->hasStorage;
}

bool GLClientState::isTextureCubeMap(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return false;
    switch (texrec->target) {
        case GL_TEXTURE_CUBE_MAP:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            return true;
        default:
            return false;
    }
}

bool GLClientState::isRenderbuffer(GLuint name) const {
    if (!name) return false;

    RenderbufferInfo::ScopedView view(mRboState.rboData);
    return view.hasRbo(name);
}

bool GLClientState::isRenderbufferThatWasBound(GLuint name) const {
    if (!name) return true;

    RenderbufferInfo::ScopedView view(mRboState.rboData);
    if (!view.hasRbo(name)) return false;

    const RboProps* props = view.get_const(name);
    return props->previouslyBound;
}

void GLClientState::getClientStatePointer(GLenum pname, GLvoid** params)
{
    std::optional<GLenum> which_state;
    switch (pname) {
    case GL_VERTEX_ARRAY_POINTER: {
        which_state = GLClientState::VERTEX_LOCATION;
        break;
        }
    case GL_NORMAL_ARRAY_POINTER: {
        which_state = GLClientState::NORMAL_LOCATION;
        break;
        }
    case GL_COLOR_ARRAY_POINTER: {
        which_state = GLClientState::COLOR_LOCATION;
        break;
        }
    case GL_TEXTURE_COORD_ARRAY_POINTER: {
        which_state = getActiveTexture() + GLClientState::TEXCOORD0_LOCATION;
        break;
        }
    case GL_POINT_SIZE_ARRAY_POINTER_OES: {
        which_state = GLClientState::POINTSIZE_LOCATION;
        break;
        }
    case GL_MATRIX_INDEX_ARRAY_POINTER_OES: {
        which_state = GLClientState::MATRIXINDEX_LOCATION;
        break;
        }
    case GL_WEIGHT_ARRAY_POINTER_OES: {
        which_state = GLClientState::WEIGHT_LOCATION;
        break;
        }
    }
    if (which_state) {
        *params = m_currVaoState[*which_state].data;
    }
}

int GLClientState::setPixelStore(GLenum param, GLint value)
{
    int retval = 0;
    switch(param) {
    case GL_UNPACK_ALIGNMENT:
        m_pixelStore.unpack_alignment = value;
        break;
    case GL_PACK_ALIGNMENT:
        m_pixelStore.pack_alignment = value;
        break;
    case GL_UNPACK_ROW_LENGTH:
        m_pixelStore.unpack_row_length = value;
        break;
    case GL_UNPACK_IMAGE_HEIGHT:
        m_pixelStore.unpack_image_height = value;
        break;
    case GL_UNPACK_SKIP_PIXELS:
        m_pixelStore.unpack_skip_pixels = value;
        break;
    case GL_UNPACK_SKIP_ROWS:
        m_pixelStore.unpack_skip_rows = value;
        break;
    case GL_UNPACK_SKIP_IMAGES:
        m_pixelStore.unpack_skip_images = value;
        break;
    case GL_PACK_ROW_LENGTH:
        m_pixelStore.pack_row_length = value;
        break;
    case GL_PACK_SKIP_PIXELS:
        m_pixelStore.pack_skip_pixels = value;
        break;
    case GL_PACK_SKIP_ROWS:
        m_pixelStore.pack_skip_rows = value;
        break;
    default:
        retval = GL_INVALID_ENUM;
    }
    return retval;
}


size_t GLClientState::pixelDataSize(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, int pack) const
{
    if (width <= 0 || height <= 0 || depth <= 0) return 0;

    GFXSTREAM_VERBOSE("pack? %d", pack);
    if (pack) {
        GFXSTREAM_VERBOSE("pack stats");
        GFXSTREAM_VERBOSE("pack align %d", m_pixelStore.pack_alignment);
        GFXSTREAM_VERBOSE("pack rowlen %d", m_pixelStore.pack_row_length);
        GFXSTREAM_VERBOSE("pack skippixels %d", m_pixelStore.pack_skip_pixels);
        GFXSTREAM_VERBOSE("pack skiprows %d", m_pixelStore.pack_skip_rows);
    } else {
        GFXSTREAM_VERBOSE("unpack stats");
        GFXSTREAM_VERBOSE("unpack align %d", m_pixelStore.unpack_alignment);
        GFXSTREAM_VERBOSE("unpack rowlen %d", m_pixelStore.unpack_row_length);
        GFXSTREAM_VERBOSE("unpack imgheight %d", m_pixelStore.unpack_image_height);
        GFXSTREAM_VERBOSE("unpack skippixels %d", m_pixelStore.unpack_skip_pixels);
        GFXSTREAM_VERBOSE("unpack skiprows %d", m_pixelStore.unpack_skip_rows);
        GFXSTREAM_VERBOSE("unpack skipimages %d", m_pixelStore.unpack_skip_images);
    }
    return GLESTextureUtils::computeTotalImageSize(
            width, height, depth,
            format, type,
            pack ? m_pixelStore.pack_alignment : m_pixelStore.unpack_alignment,
            pack ? m_pixelStore.pack_row_length : m_pixelStore.unpack_row_length,
            pack ? 0 : m_pixelStore.unpack_image_height,
            pack ? m_pixelStore.pack_skip_pixels : m_pixelStore.unpack_skip_pixels,
            pack ? m_pixelStore.pack_skip_rows : m_pixelStore.unpack_skip_rows,
            pack ? 0 : m_pixelStore.unpack_skip_images);
}

size_t GLClientState::pboNeededDataSize(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, int pack, int ignoreTrailing) const
{
    if (width <= 0 || height <= 0 || depth <= 0) return 0;

    GFXSTREAM_VERBOSE("pack? %d", pack);
    if (pack) {
        GFXSTREAM_VERBOSE("pack stats");
        GFXSTREAM_VERBOSE("pack align %d", m_pixelStore.pack_alignment);
        GFXSTREAM_VERBOSE("pack rowlen %d", m_pixelStore.pack_row_length);
        GFXSTREAM_VERBOSE("pack skippixels %d", m_pixelStore.pack_skip_pixels);
        GFXSTREAM_VERBOSE("pack skiprows %d", m_pixelStore.pack_skip_rows);
    } else {
        GFXSTREAM_VERBOSE("unpack stats");
        GFXSTREAM_VERBOSE("unpack align %d", m_pixelStore.unpack_alignment);
        GFXSTREAM_VERBOSE("unpack rowlen %d", m_pixelStore.unpack_row_length);
        GFXSTREAM_VERBOSE("unpack imgheight %d", m_pixelStore.unpack_image_height);
        GFXSTREAM_VERBOSE("unpack skippixels %d", m_pixelStore.unpack_skip_pixels);
        GFXSTREAM_VERBOSE("unpack skiprows %d", m_pixelStore.unpack_skip_rows);
        GFXSTREAM_VERBOSE("unpack skipimages %d", m_pixelStore.unpack_skip_images);
    }
    return GLESTextureUtils::computeNeededBufferSize(
            width, height, depth,
            format, type,
            pack ? m_pixelStore.pack_alignment : m_pixelStore.unpack_alignment,
            pack ? m_pixelStore.pack_row_length : m_pixelStore.unpack_row_length,
            pack ? 0 : m_pixelStore.unpack_image_height,
            pack ? m_pixelStore.pack_skip_pixels : m_pixelStore.unpack_skip_pixels,
            pack ? m_pixelStore.pack_skip_rows : m_pixelStore.unpack_skip_rows,
            pack ? 0 : m_pixelStore.unpack_skip_images,
            ignoreTrailing);
}


size_t GLClientState::clearBufferNumElts(GLenum buffer) const
{
    switch (buffer) {
    case GL_COLOR:
        return 4;
    case GL_DEPTH:
    case GL_STENCIL:
        return 1;
    }
    return 1;
}

void GLClientState::getPackingOffsets2D(GLsizei width, GLsizei height, GLenum format, GLenum type, int* bpp, int* startOffset, int* pixelRowSize, int* totalRowSize, int* skipRows) const
{
    if (width <= 0 || height <= 0) {
        *startOffset = 0;
        *pixelRowSize = 0;
        *totalRowSize = 0;
        return;
    }

    GLESTextureUtils::computePackingOffsets2D(
            width, height,
            format, type,
            m_pixelStore.pack_alignment,
            m_pixelStore.pack_row_length,
            m_pixelStore.pack_skip_pixels,
            m_pixelStore.pack_skip_rows,
            bpp,
            startOffset,
            pixelRowSize,
            totalRowSize);

    *skipRows = m_pixelStore.pack_skip_rows;
}

void GLClientState::getUnpackingOffsets2D(GLsizei width, GLsizei height, GLenum format, GLenum type, int* bpp, int* startOffset, int* pixelRowSize, int* totalRowSize, int* skipRows) const
{
    if (width <= 0 || height <= 0) {
        *startOffset = 0;
        *pixelRowSize = 0;
        *totalRowSize = 0;
        return;
    }

    GLESTextureUtils::computePackingOffsets2D(
            width, height,
            format, type,
            m_pixelStore.unpack_alignment,
            m_pixelStore.unpack_row_length,
            m_pixelStore.unpack_skip_pixels,
            m_pixelStore.unpack_skip_rows,
            bpp,
            startOffset,
            pixelRowSize,
            totalRowSize);

    *skipRows = m_pixelStore.unpack_skip_rows;
}

void GLClientState::getUnpackingOffsets3D(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, int* bpp, int* startOffset, int* pixelRowSize, int* totalRowSize, int* pixelImageSize, int* totalImageSize, int* skipRows, int* skipImages) const
{
    if (width <= 0 || height <= 0) {
        *startOffset = 0;
        *pixelRowSize = 0;
        *totalRowSize = 0;
        return;
    }

    GLESTextureUtils::computePackingOffsets3D(
            width, height, depth,
            format, type,
            m_pixelStore.unpack_alignment,
            m_pixelStore.unpack_row_length,
            m_pixelStore.unpack_image_height,
            m_pixelStore.unpack_skip_pixels,
            m_pixelStore.unpack_skip_rows,
            m_pixelStore.unpack_skip_images,
            bpp,
            startOffset,
            pixelRowSize,
            totalRowSize,
            pixelImageSize,
            totalImageSize);

    *skipRows = m_pixelStore.unpack_skip_rows;
    *skipImages = m_pixelStore.unpack_skip_images;
}

void GLClientState::setNumActiveUniformsInUniformBlock(GLuint program, GLuint uniformBlockIndex, GLint numActiveUniforms) {
    UniformBlockInfoKey key;
    key.program = program;
    key.uniformBlockIndex = uniformBlockIndex;

    UniformBlockUniformInfo info;
    info.numActiveUniforms = (size_t)numActiveUniforms;

    m_uniformBlockInfoMap[key] = info;
}

size_t GLClientState::numActiveUniformsInUniformBlock(GLuint program, GLuint uniformBlockIndex) const {
    UniformBlockInfoKey key;
    key.program = program;
    key.uniformBlockIndex = uniformBlockIndex;
    UniformBlockInfoMap::const_iterator it =
        m_uniformBlockInfoMap.find(key);
    if (it == m_uniformBlockInfoMap.end()) return 0;
    return it->second.numActiveUniforms;
}

void GLClientState::associateProgramWithPipeline(GLuint program, GLuint pipeline) {
    m_programPipelines[program] = pipeline;
}

GLClientState::ProgramPipelineIterator GLClientState::programPipelineBegin() {
    return m_programPipelines.begin();
}

GLClientState::ProgramPipelineIterator GLClientState::programPipelineEnd() {
    return m_programPipelines.end();
}

GLenum GLClientState::setActiveTextureUnit(GLenum texture)
{
    GLuint unit = texture - GL_TEXTURE0;
    if (unit >= MAX_TEXTURE_UNITS) {
        return GL_INVALID_ENUM;
    }
    m_tex.activeUnit = &m_tex.unit[unit];
    return GL_NO_ERROR;
}

GLenum GLClientState::getActiveTextureUnit() const
{
    return GL_TEXTURE0 + (m_tex.activeUnit - &m_tex.unit[0]);
}

void GLClientState::enableTextureTarget(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_2D:
        m_tex.activeUnit->enables |= (1u << TEXTURE_2D);
        break;
    case GL_TEXTURE_EXTERNAL_OES:
        m_tex.activeUnit->enables |= (1u << TEXTURE_EXTERNAL);
        break;
    }
}

void GLClientState::disableTextureTarget(GLenum target)
{
    switch (target) {
    case GL_TEXTURE_2D:
        m_tex.activeUnit->enables &= ~(1u << TEXTURE_2D);
        break;
    case GL_TEXTURE_EXTERNAL_OES:
        m_tex.activeUnit->enables &= ~(1u << TEXTURE_EXTERNAL);
        break;
    }
}

bool GLClientState::bindSampler(GLuint unit, GLuint sampler) {
    SamplerInfo::ScopedView view(mSamplerInfo);
    view.ref(sampler);
    if (m_tex.unit[unit].boundSampler) {
        view.unref(sampler);
    }
    m_tex.unit[unit].boundSampler = sampler;
    return true;
}

bool GLClientState::isSamplerBindNoOp(GLuint unit, GLuint sampler) {
    return m_tex.unit[unit].boundSampler == sampler;
}

void GLClientState::onDeleteSamplers(GLsizei n, const GLuint* samplers) {
    for (GLsizei i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < MAX_TEXTURE_UNITS; ++j) {
            uint32_t currentSampler = m_tex.unit[j].boundSampler;
            if (currentSampler == samplers[i]) {
                m_tex.unit[j].boundSampler = 0;
            }
        }
    }
}

GLenum GLClientState::getPriorityEnabledTarget(GLenum allDisabled) const
{
    unsigned int enables = m_tex.activeUnit->enables;
    if (enables & (1u << TEXTURE_EXTERNAL)) {
        return GL_TEXTURE_EXTERNAL_OES;
    } else if (enables & (1u << TEXTURE_2D)) {
        return GL_TEXTURE_2D;
    } else {
        return allDisabled;
    }
}

int GLClientState::compareTexId(const void* pid, const void* prec)
{
    const GLuint* id = (const GLuint*)pid;
    const TextureRec* rec = (const TextureRec*)prec;
    return (GLint)(*id) - (GLint)rec->id;
}

GLenum GLClientState::bindTexture(GLenum target, GLuint texture,
        GLboolean* firstUse)
{
    GLboolean first = GL_FALSE;

    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) {
        texrec = addTextureRec(texture, target);
        first = GL_TRUE;
    }

    if (texture && target != texrec->target &&
        (target != GL_TEXTURE_EXTERNAL_OES &&
         texrec->target != GL_TEXTURE_EXTERNAL_OES)) {
        return GL_INVALID_OPERATION;
    }

    switch (target) {
    case GL_TEXTURE_2D:
        m_tex.activeUnit->texture[TEXTURE_2D] = texture;
        break;
    case GL_TEXTURE_EXTERNAL_OES:
        m_tex.activeUnit->texture[TEXTURE_EXTERNAL] = texture;
        break;
    case GL_TEXTURE_CUBE_MAP:
        m_tex.activeUnit->texture[TEXTURE_CUBE_MAP] = texture;
        break;
    case GL_TEXTURE_2D_ARRAY:
        m_tex.activeUnit->texture[TEXTURE_2D_ARRAY] = texture;
        break;
    case GL_TEXTURE_3D:
        m_tex.activeUnit->texture[TEXTURE_3D] = texture;
        break;
    case GL_TEXTURE_2D_MULTISAMPLE:
        m_tex.activeUnit->texture[TEXTURE_2D_MULTISAMPLE] = texture;
        break;
    case GL_TEXTURE_BUFFER_OES:
        m_tex.activeUnit->texture[TEXTURE_BUFFER] = texture;
        break;
    }

    if (firstUse) {
        *firstUse = first;
    }

    return GL_NO_ERROR;
}

void GLClientState::setBoundEGLImage(GLenum target, GLeglImageOES image, int width, int height) {
    (void)image;

    if (target == GL_RENDERBUFFER) {
        if (!boundRenderbuffer()) return;
        setBoundRenderbufferEGLImageBacked();
        setBoundRenderbufferFormat(GL_RGBA);
        setBoundRenderbufferSamples(0);
        setBoundRenderbufferDimensions(width, height);
    } else {
        GLuint texture = getBoundTexture(target);
        TextureRec* texrec = getTextureRecPtr(texture);
        if (!texrec) return;
        texrec->boundEGLImage = true;
        setBoundTextureInternalFormat(target, GL_RGBA);
        setBoundTextureFormat(target, GL_RGBA);
        setBoundTextureType(target, GL_UNSIGNED_BYTE);
        setBoundTextureSamples(target, 0);
        setBoundTextureDims(target, target, 0, width, height, 1);
    }
}

TextureRec* GLClientState::addTextureRec(GLuint id, GLenum target)
{
    TextureRec* tex = new TextureRec;
    tex->id = id;
    tex->target = target;
    tex->format = -1;
    tex->multisamples = 0;
    tex->immutable = false;
    tex->boundEGLImage = false;
    tex->hasStorage = false;
    tex->dims = new TextureDims[6];
    tex->hasCubeNegX = false;
    tex->hasCubePosX = false;
    tex->hasCubeNegY = false;
    tex->hasCubePosY = false;
    tex->hasCubeNegZ = false;
    tex->hasCubePosZ = false;

    AutoWriteLock guard(m_tex.textureRecs->lock);
    m_tex.textureRecs->map[id] = std::shared_ptr<TextureRec>(tex);
    return tex;
}

std::shared_ptr<TextureRec> GLClientState::getTextureRec(GLuint id) const {
    AutoReadLock guard(m_tex.textureRecs->lock);
    SharedTextureDataMap::const_iterator it =
        m_tex.textureRecs->map.find(id);
    if (it == m_tex.textureRecs->map.end()) {
        return NULL;
    }
    return it->second;
}

TextureRec* GLClientState::getTextureRecPtrLocked(GLuint id) const {
    SharedTextureDataMap::const_iterator it =
        m_tex.textureRecs->map.find(id);
    if (it == m_tex.textureRecs->map.end()) {
        return NULL;
    }
    return it->second.get();
}

TextureRec* GLClientState::getTextureRecPtr(GLuint id) const {
    AutoReadLock guard(m_tex.textureRecs->lock);
    return getTextureRecPtrLocked(id);
}

void GLClientState::setBoundTextureInternalFormat(GLenum target, GLint internalformat) {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return;
    texrec->internalformat = internalformat;
}

void GLClientState::setBoundTextureFormat(GLenum target, GLenum format) {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return;
    texrec->format = format;
}

void GLClientState::setBoundTextureType(GLenum target, GLenum type) {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return;
    texrec->type = type;
}

static size_t textureDimArrayOfCubeTarget(GLenum cubetarget) {
    switch (cubetarget) {
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            return 0;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            return 1;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            return 2;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            return 3;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            return 4;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            return 5;
    }
    return 0;
}

void GLClientState::setBoundTextureDims(GLenum target, GLenum cubetarget, GLsizei level, GLsizei width, GLsizei height, GLsizei depth) {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) {
        return;
    }

    texrec->hasStorage = true;

    size_t indexToSet = 0;

    if (target == GL_TEXTURE_CUBE_MAP) {
        if (static_cast<GLenum>(-1) == cubetarget) {
            setBoundTextureDims(target, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, level, width, height, depth);
            setBoundTextureDims(target, GL_TEXTURE_CUBE_MAP_POSITIVE_X, level, width, height, depth);
            setBoundTextureDims(target, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, level, width, height, depth);
            setBoundTextureDims(target, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, level, width, height, depth);
            setBoundTextureDims(target, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, level, width, height, depth);
            setBoundTextureDims(target, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, level, width, height, depth);
            return;
        }
        indexToSet = textureDimArrayOfCubeTarget(cubetarget);
    }


    if (level == -1) {
        GLsizei curr_width = width;
        GLsizei curr_height = height;
        GLsizei curr_depth = depth;
        GLsizei curr_level = 0;

        while (true) {
            texrec->dims[indexToSet].widths[curr_level] = curr_width;
            texrec->dims[indexToSet].heights[curr_level] = curr_height;
            texrec->dims[indexToSet].depths[curr_level] = curr_depth;
            if (curr_width >> 1 == 0 &&
                curr_height >> 1 == 0 &&
                ((target == GL_TEXTURE_3D && curr_depth == 0) ||
                 true)) {
                break;
            }
            curr_width = (curr_width >> 1) ? (curr_width >> 1) : 1;
            curr_height = (curr_height >> 1) ? (curr_height >> 1) : 1;
            if (target == GL_TEXTURE_3D) {
                curr_depth = (curr_depth >> 1) ? (curr_depth >> 1) : 1;
            }
            curr_level++;
        }

    } else {
        texrec->dims[indexToSet].widths[level] = width;
        texrec->dims[indexToSet].heights[level] = height;
        texrec->dims[indexToSet].depths[level] = depth;
    }

    setFboCompletenessDirtyForTexture(texture);
}

void GLClientState::setBoundTextureSamples(GLenum target, GLsizei samples) {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return;
    texrec->multisamples = samples;
}

void GLClientState::addTextureCubeMapImage(GLenum stateTarget, GLenum cubeTarget) {
    if (stateTarget != GL_TEXTURE_CUBE_MAP) return;

    GLuint texture = getBoundTexture(stateTarget);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return;

    switch (cubeTarget) {
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            texrec->hasCubeNegX = true;
            return;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            texrec->hasCubePosX = true;
            return;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            texrec->hasCubeNegY = true;
            return;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            texrec->hasCubePosY = true;
            return;
        case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            texrec->hasCubeNegZ = true;
            return;
        case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            texrec->hasCubePosZ = true;
            return;
    }
}

void GLClientState::setBoundTextureImmutableFormat(GLenum target) {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return;
    texrec->immutable = true;
    if (target == GL_TEXTURE_CUBE_MAP) {
        texrec->hasCubeNegX = true;
        texrec->hasCubePosX = true;
        texrec->hasCubeNegY = true;
        texrec->hasCubePosY = true;
        texrec->hasCubeNegZ = true;
        texrec->hasCubePosZ = true;
    }
}

bool GLClientState::isBoundTextureImmutableFormat(GLenum target) const {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return false;
    return texrec->immutable;
}

bool GLClientState::isBoundTextureComplete(GLenum target) const {
    GLuint texture = getBoundTexture(target);
    TextureRec* texrec = getTextureRecPtr(texture);
    if (!texrec) return false;

    if (texrec->immutable) return true;
    if (!texrec->hasStorage) return true;

    if (target == GL_TEXTURE_CUBE_MAP) {
        if (!(texrec->hasCubeNegX &&
             texrec->hasCubePosX &&
             texrec->hasCubeNegY &&
             texrec->hasCubePosY &&
             texrec->hasCubeNegZ &&
             texrec->hasCubePosZ)) return false;

        size_t currBaseLevel = texrec->dims[0].widths.begin()->first;
        size_t currWidth = texrec->dims[0].widths.begin()->second;
        size_t currHeight = texrec->dims[0].heights.begin()->second;
        for (size_t i = 1; i < 6; ++i) {
            size_t nextLevel = texrec->dims[i].widths.begin()->first;
            size_t nextWidth = texrec->dims[i].widths.begin()->second;
            size_t nextHeight = texrec->dims[i].heights.begin()->second;
            if (currBaseLevel != nextLevel) return false;
            if (currWidth != nextWidth) return false;
            if (currHeight != nextHeight) return false;
        }

        return true;
    }

    return true;
}


GLuint GLClientState::getBoundTexture(GLenum target) const
{
    switch (target) {
    case GL_TEXTURE_2D:
        return m_tex.activeUnit->texture[TEXTURE_2D];
    case GL_TEXTURE_EXTERNAL_OES:
        return m_tex.activeUnit->texture[TEXTURE_EXTERNAL];
    case GL_TEXTURE_CUBE_MAP:
        return m_tex.activeUnit->texture[TEXTURE_CUBE_MAP];
    case GL_TEXTURE_2D_ARRAY:
        return m_tex.activeUnit->texture[TEXTURE_2D_ARRAY];
    case GL_TEXTURE_3D:
        return m_tex.activeUnit->texture[TEXTURE_3D];
    case GL_TEXTURE_2D_MULTISAMPLE:
        return m_tex.activeUnit->texture[TEXTURE_2D_MULTISAMPLE];
    case GL_TEXTURE_BUFFER_OES:
        return m_tex.activeUnit->texture[TEXTURE_BUFFER];
    default:
        return 0;
    }
}

GLuint GLClientState::getBoundFramebuffer(GLenum target) const
{
    switch (target) {
    case GL_FRAMEBUFFER:
    case GL_DRAW_FRAMEBUFFER:
        return mFboState.boundDrawFramebuffer;
    case GL_READ_FRAMEBUFFER:
        return mFboState.boundReadFramebuffer;
    default:
        return 0;
    }
}

GLenum GLClientState::checkFramebufferCompleteness(GLenum target) {
    // Default framebuffer is complete
    // TODO: Check the case where the default framebuffer is 0x0
    if (0 == boundFramebuffer(target)) {
        return GL_FRAMEBUFFER_COMPLETE;
    }

    bool hasAttachment = false;
    FboProps& props = boundFboProps(target);

    if (!props.completenessDirty) {
        return props.cachedCompleteness;
    }

    int currentSamples = -1;

    for (int i = 0; i < getMaxColorAttachments(); i++) {
        if (!props.colorAttachmenti_hasTex[i] &&
            !props.colorAttachmenti_hasRbo[i]) continue;

        GLenum attachmentRes = checkFramebufferAttachmentCompleteness(target, glUtilsColorAttachmentName(i), &currentSamples);
        if (attachmentRes != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
            hasAttachment = true;
        }
        if (attachmentRes) {
            GFXSTREAM_DEBUG("color attachment %d not complete: 0x%x\n", i, attachmentRes);
            return attachmentRes;
        }
    }

    bool hasDepth = (props.depthAttachment_hasTexObj || props.depthAttachment_hasRbo || props.depthstencilAttachment_hasTexObj || props.depthstencilAttachment_hasRbo);
    bool hasStencil = (props.stencilAttachment_hasTexObj || props.stencilAttachment_hasRbo || props.depthstencilAttachment_hasTexObj || props.depthstencilAttachment_hasRbo);

    if (hasDepth) {
        GLenum depthAttachmentRes = checkFramebufferAttachmentCompleteness(target, GL_DEPTH_ATTACHMENT, &currentSamples);
        if (depthAttachmentRes != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
            hasAttachment = true;
        }
        if (depthAttachmentRes) {
            GFXSTREAM_DEBUG("depth attachment not complete: 0x%x", depthAttachmentRes);
            return depthAttachmentRes;
        }
    }

    if (hasStencil) {
        GLenum stencilAttachmentRes = checkFramebufferAttachmentCompleteness(target, GL_STENCIL_ATTACHMENT, &currentSamples);
        if (stencilAttachmentRes != GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
            hasAttachment = true;
        }
        if (stencilAttachmentRes) {
            GFXSTREAM_DEBUG("stencil attachment not complete: 0x%x", stencilAttachmentRes);
            return stencilAttachmentRes;
        }
    }

    if (hasDepth && hasStencil) {
        // In gles3, depth/stencil must use the same image.
        if (m_glesMajorVersion > 2) {
            if ((props.depthAttachment_hasTexObj && props.stencilAttachment_hasRbo) ||
                (props.stencilAttachment_hasTexObj && props.depthAttachment_hasRbo)) {
                GFXSTREAM_DEBUG("GL_FRAMEBUFFER_UNSUPPORTED: using different types of depth/stencil attachment images in GLES 3+");
                return GL_FRAMEBUFFER_UNSUPPORTED;
            }
            if (props.depthAttachment_hasTexObj) {
                if (props.depthAttachment_texture != props.stencilAttachment_texture) {
                    GFXSTREAM_DEBUG("GL_FRAMEBUFFER_UNSUPPORTED: using different texture images for depth and stencil attachments in GLES 3+");
                    return GL_FRAMEBUFFER_UNSUPPORTED;
                }
            }
            if (props.depthAttachment_hasRbo) {
                if (props.depthAttachment_rbo != props.stencilAttachment_rbo) {
                    GFXSTREAM_DEBUG("GL_FRAMEBUFFER_UNSUPPORTED: using different renderbuffers for depth and stencil attachments in GLES 3+");
                    return GL_FRAMEBUFFER_UNSUPPORTED;
                }
            }
        }
    }

    if (!hasAttachment) {
        // Framebuffers may be missing an attachment if they have nonzero
        // default width and height
        if (props.defaultWidth == 0 || props.defaultHeight == 0) {
            return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
        }
    }

    props.completenessDirty = false;
    props.cachedCompleteness = GL_FRAMEBUFFER_COMPLETE;
    return GL_FRAMEBUFFER_COMPLETE;
}

GLenum GLClientState::checkFramebufferAttachmentCompleteness(GLenum target, GLenum attachment, int* currentSamples) const {
    FboFormatInfo fbo_format_info;
    getBoundFramebufferFormat(target, attachment, &fbo_format_info);

    // Check format and renderability
    bool renderable = false;
    switch (fbo_format_info.type) {
        case FBO_ATTACHMENT_RENDERBUFFER:
            switch (attachment) {
                case GL_DEPTH_ATTACHMENT:
                    renderable = fbo_format_info.rb_external || depthRenderableFormat(fbo_format_info.rb_format);
                    break;
                case GL_STENCIL_ATTACHMENT:
                    renderable = fbo_format_info.rb_external || stencilRenderableFormat(fbo_format_info.rb_format);
                    break;
                default:
                    renderable = fbo_format_info.rb_external || colorRenderableFormat(
                            fbo_format_info.rb_format,
                            GL_UNSIGNED_BYTE,
                            m_glesMajorVersion, m_glesMinorVersion,
                            m_has_color_buffer_float_extension,
                            m_has_color_buffer_half_float_extension);
                    break;
            }
            break;
        case FBO_ATTACHMENT_TEXTURE:
            switch (attachment) {
                case GL_DEPTH_ATTACHMENT:
                    renderable = fbo_format_info.tex_external || depthRenderableFormat(fbo_format_info.tex_internalformat);
                    break;
                case GL_STENCIL_ATTACHMENT:
                    renderable = fbo_format_info.tex_external || stencilRenderableFormat(fbo_format_info.tex_internalformat);
                    break;
                default:
                    renderable = fbo_format_info.tex_external || colorRenderableFormat(
                            fbo_format_info.tex_internalformat,
                            fbo_format_info.tex_type,
                            m_glesMajorVersion, m_glesMinorVersion,
                            m_has_color_buffer_float_extension,
                            m_has_color_buffer_half_float_extension);
                    break;
            }
            break;
        case FBO_ATTACHMENT_NONE:
        default:
            return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }

    if (!renderable) {
        switch (fbo_format_info.type) {
            case FBO_ATTACHMENT_RENDERBUFFER:
                GFXSTREAM_DEBUG("rbo not color renderable. target=0x%x attachment=0x%x rb_format=0x%x "
                                "gles=%d.%d floatext=%d hfloatext=%d\n",
                                target, attachment, fbo_format_info.rb_format,
                                m_glesMajorVersion, m_glesMinorVersion,
                                m_has_color_buffer_float_extension,
                                m_has_color_buffer_half_float_extension);
                break;
            case FBO_ATTACHMENT_TEXTURE:
                GFXSTREAM_DEBUG("tex not color renderable. target=0x%x attachment=0x%x "
                                "tex_intformat=0x%x tex_format=0x%x tex_type=0x%x gles=%d.%d "
                                "floatext=%d hfloatext=%d\n",
                                target, attachment, fbo_format_info.tex_internalformat,
                                fbo_format_info.tex_format, fbo_format_info.tex_type, m_glesMajorVersion,
                                m_glesMinorVersion, m_has_color_buffer_float_extension,
                                m_has_color_buffer_half_float_extension);
                break;
            default:
                break;
        }
        return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    }

    // Check dimensions
    std::shared_ptr<TextureRec> texrec;
    std::shared_ptr<RboProps> rbo;
    switch (fbo_format_info.type) {
    case FBO_ATTACHMENT_RENDERBUFFER:
        rbo = getFboAttachmentRbo(target, attachment);
        if (!fbo_format_info.rb_external) {
            if (!rbo || 0 == rbo->width || 0 == rbo->height) {
                GFXSTREAM_DEBUG("rbo has zero dimension");
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
        }
        break;
    case FBO_ATTACHMENT_TEXTURE:
        texrec = getFboAttachmentTexture(target, attachment);
        if (!fbo_format_info.tex_external) {
            if (0 == texrec->dims->widths[fbo_format_info.tex_level] ||
                    0 == texrec->dims->heights[fbo_format_info.tex_level]) {
                GFXSTREAM_DEBUG("texture has zero dimension");
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
            GLsizei depth = texrec->dims->depths[fbo_format_info.tex_level];
            if (fbo_format_info.tex_layer >= depth) {
                GFXSTREAM_DEBUG("texture layer/zoffset too high, wanted %d but only have %d layers",
                      fbo_format_info.tex_layer, depth);
                return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            }
        }
        break;
    case FBO_ATTACHMENT_NONE:
    default:
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }

    // Check samples
    int currSamplesVal = *currentSamples;
    bool firstTime = -1 == currSamplesVal;
    int samplesThisAttachment = 0;
    switch (fbo_format_info.type) {
    case FBO_ATTACHMENT_RENDERBUFFER:
        samplesThisAttachment = fbo_format_info.rb_multisamples;
        break;
    case FBO_ATTACHMENT_TEXTURE:
        samplesThisAttachment = fbo_format_info.tex_multisamples;
        break;
    case FBO_ATTACHMENT_NONE:
        break;
    default:
        return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }

    if (firstTime) {
        *currentSamples = samplesThisAttachment;
    } else {
        if (samplesThisAttachment != currSamplesVal) {
            return GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
        }
    }

    return 0;
}

// BEGIN driver workarounds-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-
// (>' ')><(' '<)(>' ')><(' '<)(>' ')><(' '<)(>' ')><(' '<)(>' ')><(' '<)(>' ')>

static bool unreliableInternalFormat(GLenum internalformat) {
    switch (internalformat) {
    case GL_LUMINANCE:
        return true;
    default:
        return false;
    }
}

void GLClientState::writeCopyTexImageState
    (GLenum target, GLint level, GLenum internalformat) {
    if (unreliableInternalFormat(internalformat)) {
        CubeMapDef entry;
        entry.id = getBoundTexture(GL_TEXTURE_2D);
        entry.target = target;
        entry.level = level;
        entry.internalformat = internalformat;
        m_cubeMapDefs.insert(entry);
    }
}

static GLenum identifyPositiveCubeMapComponent(GLenum target) {
    switch (target) {
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
        return GL_TEXTURE_CUBE_MAP_POSITIVE_X;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
        return GL_TEXTURE_CUBE_MAP_POSITIVE_Y;
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
        return GL_TEXTURE_CUBE_MAP_POSITIVE_Z;
    default:
        return 0;
    }
}

GLenum GLClientState::copyTexImageNeededTarget
    (GLenum target, GLint level, GLenum internalformat) {
    if (unreliableInternalFormat(internalformat)) {
        GLenum positiveComponent =
            identifyPositiveCubeMapComponent(target);
        if (positiveComponent) {
            CubeMapDef query;
            query.id = getBoundTexture(GL_TEXTURE_2D);
            query.target = positiveComponent;
            query.level = level;
            query.internalformat = internalformat;
            if (m_cubeMapDefs.find(query) ==
                m_cubeMapDefs.end()) {
                return positiveComponent;
            }
        }
    }
    return 0;
}

GLenum GLClientState::copyTexImageLuminanceCubeMapAMDWorkaround
    (GLenum target, GLint level, GLenum internalformat) {
    writeCopyTexImageState(target, level, internalformat);
    return copyTexImageNeededTarget(target, level, internalformat);
}

// (>' ')><(' '<)(>' ')><(' '<)(>' ')><(' '<)(>' ')><(' '<)(>' ')><(' '<)(>' ')>
// END driver workarounds-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-

void GLClientState::deleteTextures(GLsizei n, const GLuint* textures)
{
    for (const GLuint* texture = textures; texture != textures + n; texture++) {
        setFboCompletenessDirtyForTexture(*texture);
    }

    // Updating the textures array could be made more efficient when deleting
    // several textures:
    // - compacting the array could be done in a single pass once the deleted
    //   textures are marked, or
    // - could swap deleted textures to the end and re-sort.
    TextureRec* texrec;
    for (const GLuint* texture = textures; texture != textures + n; texture++) {
        AutoWriteLock guard(m_tex.textureRecs->lock);
        texrec = getTextureRecPtrLocked(*texture);
        if (texrec && texrec->dims) {
            delete [] texrec->dims;
        }
        if (texrec) {
            m_tex.textureRecs->map.erase(*texture);
            for (TextureUnit* unit = m_tex.unit;
                 unit != m_tex.unit + MAX_TEXTURE_UNITS;
                 unit++)
            {
                if (unit->texture[TEXTURE_2D] == *texture) {
                    unit->texture[TEXTURE_2D] = 0;
                } else if (unit->texture[TEXTURE_EXTERNAL] == *texture) {
                    unit->texture[TEXTURE_EXTERNAL] = 0;
                }
            }
        }
    }
}

// RBO//////////////////////////////////////////////////////////////////////////

void GLClientState::addFreshRenderbuffer(GLuint name) {
    if (!name) return;

    RenderbufferInfo::ScopedView view(mRboState.rboData);
    view.addFresh(name);
}

void GLClientState::addRenderbuffers(GLsizei n, GLuint* renderbuffers) {
    for (GLsizei i = 0; i < n; i++) {
        addFreshRenderbuffer(renderbuffers[i]);
    }
}

void GLClientState::removeRenderbuffers(GLsizei n, const GLuint* renderbuffers) {
    bool unbindCurrent = false;
    {
        RenderbufferInfo::ScopedView view(mRboState.rboData);
        for (GLsizei i = 0; i < n; i++) {
            if (renderbuffers[i] != 0) { // Never remove the zero rb.
                auto rboPtr = view.get_shared_ptr(renderbuffers[i]);
                if (!rboPtr) {
                    continue;
                }
                unbindCurrent |=
                        (mRboState.boundRenderbuffer == rboPtr);
                setFboCompletenessDirtyForRbo(rboPtr);
                view.remove(renderbuffers[i]);
            }
        }
    }

    if (unbindCurrent) {
        bindRenderbuffer(GL_RENDERBUFFER, 0);
    }
}

bool GLClientState::usedRenderbufferName(GLuint name) const {
    if (!name) return false;

    RenderbufferInfo::ScopedView view(mRboState.rboData);
    return view.get_const(name) != 0;
}

void GLClientState::bindRenderbuffer(GLenum target, GLuint name) {

    (void)target; // Must be GL_RENDERBUFFER
    RenderbufferInfo::ScopedView view(mRboState.rboData);
    mRboState.boundRenderbuffer = view.bind(name);
}

GLuint GLClientState::boundRenderbuffer() const {
    return mRboState.boundRenderbuffer->id;
}

void GLClientState::setBoundRenderbufferFormat(GLenum format) {
    mRboState.boundRenderbuffer->format = format;
}

void GLClientState::setBoundRenderbufferSamples(GLsizei samples) {
    mRboState.boundRenderbuffer->multisamples = samples;
}

void GLClientState::setBoundRenderbufferDimensions(GLsizei width, GLsizei height) {
    mRboState.boundRenderbuffer->width = width;
    mRboState.boundRenderbuffer->height = height;
}

void GLClientState::setBoundRenderbufferEGLImageBacked() {
    mRboState.boundRenderbuffer->boundEGLImage = true;
}

// FBO//////////////////////////////////////////////////////////////////////////

GLint GLClientState::queryTexInternalFormat(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return -1;
    return texrec->internalformat;
}

GLsizei GLClientState::queryTexWidth(GLsizei level, GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) {
        return 0;
    }
    return texrec->dims->widths[level];
}

GLsizei GLClientState::queryTexHeight(GLsizei level, GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return 0;
    return texrec->dims->heights[level];
}

GLsizei GLClientState::queryTexDepth(GLsizei level, GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return 0;
    return texrec->dims->depths[level];
}

bool GLClientState::queryTexEGLImageBacked(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return false;
    return texrec->boundEGLImage;
}

GLenum GLClientState::queryTexFormat(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return -1;
    return texrec->format;
}

GLenum GLClientState::queryTexType(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return -1;
    return texrec->type;
}

GLsizei GLClientState::queryTexSamples(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return 0;
    return texrec->multisamples;
}

GLenum GLClientState::queryTexLastBoundTarget(GLuint tex_name) const {
    TextureRec* texrec = getTextureRecPtr(tex_name);
    if (!texrec) return GL_NONE;
    return texrec->target;
}

void GLClientState::getBoundFramebufferFormat(
        GLenum target,
        GLenum attachment, FboFormatInfo* res_info) const {
    const FboProps& props = boundFboProps_const(target);

    res_info->type = FBO_ATTACHMENT_NONE;
    res_info->rb_format = GL_NONE;
    res_info->rb_multisamples = 0;
    res_info->rb_external = false;
    res_info->tex_internalformat = -1;
    res_info->tex_format = GL_NONE;
    res_info->tex_type = GL_NONE;
    res_info->tex_multisamples = 0;
    res_info->tex_external = false;

    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    if (colorAttachmentIndex != -1) {
        if (props.colorAttachmenti_hasRbo[colorAttachmentIndex]) {
            res_info->type = FBO_ATTACHMENT_RENDERBUFFER;
            res_info->rb_format = props.colorAttachmenti_rbos[colorAttachmentIndex]->format;
            res_info->rb_multisamples =
                    props.colorAttachmenti_rbos[colorAttachmentIndex]->multisamples;
            res_info->rb_external =
                    props.colorAttachmenti_rbos[colorAttachmentIndex]->boundEGLImage;
        } else if (props.colorAttachmenti_hasTex[colorAttachmentIndex]) {
            res_info->type = FBO_ATTACHMENT_TEXTURE;
            res_info->tex_external =
                    props.colorAttachmenti_textures[colorAttachmentIndex]->boundEGLImage;
            res_info->tex_internalformat =
                    props.colorAttachmenti_textures[colorAttachmentIndex]->internalformat;
            res_info->tex_format =
                    props.colorAttachmenti_textures[colorAttachmentIndex]->format;
            res_info->tex_type =
                    props.colorAttachmenti_textures[colorAttachmentIndex]->type;
            res_info->tex_multisamples =
                    props.colorAttachmenti_textures[colorAttachmentIndex]->multisamples;
            res_info->tex_level = props.colorAttachmenti_texture_levels[colorAttachmentIndex];
            res_info->tex_layer = props.colorAttachmenti_texture_layers[colorAttachmentIndex];
        } else {
            res_info->type = FBO_ATTACHMENT_NONE;
        }
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        if (props.depthAttachment_hasRbo) {
            res_info->type = FBO_ATTACHMENT_RENDERBUFFER;
            res_info->rb_format = props.depthAttachment_rbo->format;
            res_info->rb_multisamples = props.depthAttachment_rbo->multisamples;
            res_info->rb_external = props.depthAttachment_rbo->boundEGLImage;
        } else if (props.depthAttachment_hasTexObj) {
            res_info->type = FBO_ATTACHMENT_TEXTURE;
            res_info->tex_external = props.depthAttachment_texture->boundEGLImage;
            res_info->tex_internalformat = props.depthAttachment_texture->internalformat;
            res_info->tex_format = props.depthAttachment_texture->format;
            res_info->tex_type = props.depthAttachment_texture->type;
            res_info->tex_multisamples = props.depthAttachment_texture->multisamples;
            res_info->tex_level = props.depthAttachment_texture_level;
            res_info->tex_layer = props.depthAttachment_texture_layer;
        } else {
            res_info->type = FBO_ATTACHMENT_NONE;
        }
        break;
    case GL_STENCIL_ATTACHMENT:
        if (props.stencilAttachment_hasRbo) {
            res_info->type = FBO_ATTACHMENT_RENDERBUFFER;
            res_info->rb_format = props.stencilAttachment_rbo->format;
            res_info->rb_multisamples = props.stencilAttachment_rbo->multisamples;
            res_info->rb_external = props.stencilAttachment_rbo->boundEGLImage;
        } else if (props.stencilAttachment_hasTexObj) {
            res_info->type = FBO_ATTACHMENT_TEXTURE;
            res_info->tex_external = props.stencilAttachment_texture->boundEGLImage;
            res_info->tex_internalformat = props.stencilAttachment_texture->internalformat;
            res_info->tex_format = props.stencilAttachment_texture->format;
            res_info->tex_type = props.stencilAttachment_texture->type;
            res_info->tex_multisamples = props.stencilAttachment_texture->multisamples;
            res_info->tex_level = props.depthAttachment_texture_level;
            res_info->tex_layer = props.depthAttachment_texture_layer;
        } else {
            res_info->type = FBO_ATTACHMENT_NONE;
        }
        break;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        if (props.depthstencilAttachment_hasRbo) {
            res_info->type = FBO_ATTACHMENT_RENDERBUFFER;
            res_info->rb_format = props.depthstencilAttachment_rbo->format;
            res_info->rb_multisamples = props.depthstencilAttachment_rbo->multisamples;
            res_info->rb_external = props.depthstencilAttachment_rbo->boundEGLImage;
        } else if (props.depthstencilAttachment_hasTexObj) {
            res_info->type = FBO_ATTACHMENT_TEXTURE;
            res_info->tex_external = props.depthstencilAttachment_texture->boundEGLImage;
            res_info->tex_internalformat = props.depthstencilAttachment_texture->internalformat;
            res_info->tex_format = props.depthstencilAttachment_texture->format;
            res_info->tex_type = props.depthstencilAttachment_texture->type;
            res_info->tex_multisamples = props.depthstencilAttachment_texture->multisamples;
            res_info->tex_level = props.depthAttachment_texture_level;
            res_info->tex_layer = props.depthAttachment_texture_layer;
        } else {
            res_info->type = FBO_ATTACHMENT_NONE;
        }
        break;
    }
}

FboAttachmentType GLClientState::getBoundFramebufferAttachmentType(GLenum target, GLenum attachment) const {
    FboFormatInfo info;
    getBoundFramebufferFormat(target, attachment, &info);
    return info.type;
}

int GLClientState::getMaxColorAttachments() const {
    return m_hostDriverCaps.max_color_attachments;
}

int GLClientState::getMaxDrawBuffers() const {
    return m_hostDriverCaps.max_draw_buffers;
}

#define UNIFORM_VALIDATION_ERR_COND(cond, code) if (cond) { *err = code; return; }

#define UNIFORM_VALIDATION_INFO_VAR_NAME info

#define UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_FLOATS \
    (!(UNIFORM_VALIDATION_INFO_VAR_NAME->isBool) && (UNIFORM_VALIDATION_INFO_VAR_NAME->isInt || UNIFORM_VALIDATION_INFO_VAR_NAME->isSampler))

#define UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_INTS \
    (!(UNIFORM_VALIDATION_INFO_VAR_NAME->isBool) && (!UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_FLOATS || UNIFORM_VALIDATION_INFO_VAR_NAME->isUnsigned))

#define UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_UNSIGNED_INTS \
    (!(UNIFORM_VALIDATION_INFO_VAR_NAME->isBool) && (!UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_FLOATS || !(UNIFORM_VALIDATION_INFO_VAR_NAME->isUnsigned)))

#define UNIFORM_VALIDATION_INLINING

void GLClientState::validateUniform(bool isFloat, bool isUnsigned, GLint columns, GLint rows, GLint location, GLsizei count, GLenum* err) {
    UNIFORM_VALIDATION_ERR_COND(!m_currentProgram && !m_currentShaderProgram, GL_INVALID_OPERATION);
    if (-1 == location) return;
    auto info = currentUniformValidationInfo.get_const(location);
    UNIFORM_VALIDATION_ERR_COND(!info || !info->valid, GL_INVALID_OPERATION);
    UNIFORM_VALIDATION_ERR_COND(static_cast<uint32_t>(columns) != info->columns ||
                                static_cast<uint32_t>(rows) != info->rows, GL_INVALID_OPERATION);
    UNIFORM_VALIDATION_ERR_COND(count > 1 && !info->isArray, GL_INVALID_OPERATION);
    if (isFloat) {
        UNIFORM_VALIDATION_ERR_COND(UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_FLOATS, GL_INVALID_OPERATION);
    } else {
        if (isUnsigned) {
            UNIFORM_VALIDATION_ERR_COND(UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_UNSIGNED_INTS, GL_INVALID_OPERATION);
        } else {
            UNIFORM_VALIDATION_ERR_COND(UNIFORM_VALIDATION_TYPE_VIOLATION_FOR_INTS, GL_INVALID_OPERATION);
        }
    }
}

bool GLClientState::isAttribIndexUsedByProgram(int index) {
    auto info = currentAttribValidationInfo.get_const(index);
    if (!info) return false;
    if (!info->validInProgram) return false;
    return true;
}

void GLClientState::addFreshFramebuffer(GLuint name) {
    FboProps props;
    props.name = name;
    props.previouslyBound = false;

    props.completenessDirty = true;

    props.colorAttachmenti_textures.resize(m_hostDriverCaps.max_color_attachments, 0);
    props.colorAttachmenti_texture_levels.resize(m_hostDriverCaps.max_color_attachments, 0);
    props.colorAttachmenti_texture_layers.resize(m_hostDriverCaps.max_color_attachments, 0);

    props.depthAttachment_texture_level = 0;
    props.depthAttachment_texture_layer = 0;
    props.stencilAttachment_texture_level = 0;
    props.stencilAttachment_texture_layer = 0;

    props.depthAttachment_texture = 0;
    props.stencilAttachment_texture = 0;
    props.depthstencilAttachment_texture = 0;

    props.colorAttachmenti_hasTex.resize(m_hostDriverCaps.max_color_attachments, false);
    props.depthAttachment_hasTexObj = false;
    props.stencilAttachment_hasTexObj = false;
    props.depthstencilAttachment_hasTexObj = false;

    props.colorAttachmenti_rbos.resize(m_hostDriverCaps.max_color_attachments, 0);
    props.depthAttachment_rbo = 0;
    props.stencilAttachment_rbo = 0;
    props.depthstencilAttachment_rbo = 0;

    props.colorAttachmenti_hasRbo.resize(m_hostDriverCaps.max_color_attachments, false);
    props.depthAttachment_hasRbo = false;
    props.stencilAttachment_hasRbo = false;
    props.depthstencilAttachment_hasRbo = false;

    props.defaultWidth = 0;
    props.defaultHeight = 0;

    mFboState.fboData[name] = props;
}

void GLClientState::addFramebuffers(GLsizei n, GLuint* framebuffers) {
    for (GLsizei i = 0; i < n; i++) {
        addFreshFramebuffer(framebuffers[i]);
    }
}

void GLClientState::removeFramebuffers(GLsizei n, const GLuint* framebuffers) {
    RenderbufferInfo::ScopedView view(mRboState.rboData);
    for (GLsizei i = 0; i < n; i++) {
        if (framebuffers[i] != 0) { // Never remove the zero fb.
            if (framebuffers[i] == mFboState.boundDrawFramebuffer) {
                bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            }
            if (framebuffers[i] == mFboState.boundReadFramebuffer) {
                bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }
            mFboState.fboData.erase(framebuffers[i]);
        }
    }
}

bool GLClientState::usedFramebufferName(GLuint name) const {
    return mFboState.fboData.find(name) != mFboState.fboData.end();
}

FboProps& GLClientState::boundFboProps(GLenum target) {
    switch (target) {
    case GL_DRAW_FRAMEBUFFER:
        return mFboState.fboData[mFboState.boundDrawFramebuffer];
    case GL_READ_FRAMEBUFFER:
        return mFboState.fboData[mFboState.boundReadFramebuffer];
    case GL_FRAMEBUFFER:
        return mFboState.fboData[mFboState.boundDrawFramebuffer];
    }
    return mFboState.fboData[mFboState.boundDrawFramebuffer];
}

const FboProps& GLClientState::boundFboProps_const(GLenum target) const {
    switch (target) {
    case GL_DRAW_FRAMEBUFFER:
        return mFboState.fboData.find(mFboState.boundDrawFramebuffer)->second;
    case GL_READ_FRAMEBUFFER:
        return mFboState.fboData.find(mFboState.boundReadFramebuffer)->second;
    case GL_FRAMEBUFFER:
        return mFboState.fboData.find(mFboState.boundDrawFramebuffer)->second;
    }
    return mFboState.fboData.find(mFboState.boundDrawFramebuffer)->second;
}

void GLClientState::bindFramebuffer(GLenum target, GLuint name) {
    // If unused, add it.
    if (!usedFramebufferName(name)) {
        addFreshFramebuffer(name);
    }
    switch (target) {
        case GL_DRAW_FRAMEBUFFER:
            mFboState.boundDrawFramebuffer = name;
            break;
        case GL_READ_FRAMEBUFFER:
            mFboState.boundReadFramebuffer = name;
            break;
        default: // case GL_FRAMEBUFFER:
            mFboState.boundDrawFramebuffer = name;
            mFboState.boundReadFramebuffer = name;
            break;
    }
    boundFboProps(target).previouslyBound = true;
}

void GLClientState::setCheckFramebufferStatus(GLenum target, GLenum status) {
    switch (target) {
        case GL_DRAW_FRAMEBUFFER:
            mFboState.drawFboCheckStatus = status;
            break;
        case GL_READ_FRAMEBUFFER:
            mFboState.readFboCheckStatus = status;
            break;
        case GL_FRAMEBUFFER:
            mFboState.drawFboCheckStatus = status;
            break;
    }
}

void GLClientState::setFramebufferParameter(GLenum target, GLenum pname, GLint param) {
    switch (pname) {
        case GL_FRAMEBUFFER_DEFAULT_WIDTH:
            boundFboProps(target).defaultWidth = param;
            boundFboProps(target).completenessDirty = true;
            break;
        case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
            boundFboProps(target).defaultHeight = param;
            boundFboProps(target).completenessDirty = true;
            break;
    }
}

GLenum GLClientState::getCheckFramebufferStatus(GLenum target) const {
    switch (target) {
    case GL_DRAW_FRAMEBUFFER:
        return mFboState.drawFboCheckStatus;
    case GL_READ_FRAMEBUFFER:
        return mFboState.readFboCheckStatus;
    case GL_FRAMEBUFFER:
        return mFboState.drawFboCheckStatus;
    }
    return mFboState.drawFboCheckStatus;
}

GLuint GLClientState::boundFramebuffer(GLenum target) const {
    return boundFboProps_const(target).name;
}

// Texture objects for FBOs/////////////////////////////////////////////////////

void GLClientState::attachTextureObject(
        GLenum target,
        GLenum attachment, GLuint texture, GLint level, GLint layer) {

    bool attach = texture != 0;
    std::shared_ptr<TextureRec> texrec = getTextureRec(texture);

    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    boundFboProps(target).completenessDirty = true;

    if (colorAttachmentIndex != -1) {
        boundFboProps(target).colorAttachmenti_textures[colorAttachmentIndex] = texrec;
        boundFboProps(target).colorAttachmenti_texture_levels[colorAttachmentIndex] = level;
        boundFboProps(target).colorAttachmenti_texture_layers[colorAttachmentIndex] = layer;
        boundFboProps(target).colorAttachmenti_hasTex[colorAttachmentIndex] = attach;
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        boundFboProps(target).depthAttachment_texture = texrec;
        boundFboProps(target).depthAttachment_texture_level = level;
        boundFboProps(target).depthAttachment_texture_layer = layer;
        boundFboProps(target).depthAttachment_hasTexObj = attach;
        break;
    case GL_STENCIL_ATTACHMENT:
        boundFboProps(target).stencilAttachment_texture = texrec;
        boundFboProps(target).stencilAttachment_texture_level = level;
        boundFboProps(target).stencilAttachment_texture_layer = layer;
        boundFboProps(target).stencilAttachment_hasTexObj = attach;
        break;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        boundFboProps(target).depthstencilAttachment_texture = texrec;
        boundFboProps(target).depthstencilAttachment_hasTexObj = attach;
        boundFboProps(target).stencilAttachment_texture = texrec;
        boundFboProps(target).stencilAttachment_hasTexObj = attach;
        boundFboProps(target).depthAttachment_texture = texrec;
        boundFboProps(target).depthAttachment_hasTexObj = attach;
        boundFboProps(target).depthAttachment_texture_level = level;
        boundFboProps(target).depthAttachment_texture_layer = layer;
        boundFboProps(target).stencilAttachment_texture_level = level;
        boundFboProps(target).stencilAttachment_texture_layer = layer;
        break;
    }
}

std::shared_ptr<TextureRec> GLClientState::getFboAttachmentTexture(GLenum target, GLenum attachment) const {
    std::shared_ptr<TextureRec> res = {}; // conservative

    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    if (colorAttachmentIndex != -1) {
        res = boundFboProps_const(target).colorAttachmenti_textures[colorAttachmentIndex];
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        res = boundFboProps_const(target).depthAttachment_texture;
        break;
    case GL_STENCIL_ATTACHMENT:
        res = boundFboProps_const(target).stencilAttachment_texture;
        break;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        res = boundFboProps_const(target).depthstencilAttachment_texture;
        break;
    }
    return res;
}

// RBOs for FBOs////////////////////////////////////////////////////////////////

void GLClientState::detachRbo(GLuint renderbuffer) {
    for (int i = 0; i < m_hostDriverCaps.max_color_attachments; i++) {
        detachRboFromFbo(GL_DRAW_FRAMEBUFFER, glUtilsColorAttachmentName(i), renderbuffer);
        detachRboFromFbo(GL_READ_FRAMEBUFFER, glUtilsColorAttachmentName(i), renderbuffer);
    }

    detachRboFromFbo(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, renderbuffer);
    detachRboFromFbo(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, renderbuffer);

    detachRboFromFbo(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, renderbuffer);
    detachRboFromFbo(GL_READ_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, renderbuffer);

    detachRboFromFbo(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, renderbuffer);
    detachRboFromFbo(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, renderbuffer);
}

void GLClientState::detachRboFromFbo(GLenum target, GLenum attachment, GLuint renderbuffer) {
    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    boundFboProps(target).completenessDirty = true;

    RenderbufferInfo::ScopedView view(mRboState.rboData);
    auto renderBufferSharedPtr = view.get_shared_ptr(renderbuffer);
    if (colorAttachmentIndex != -1) {
        if (boundFboProps(target).colorAttachmenti_hasRbo[colorAttachmentIndex] &&
            boundFboProps(target).colorAttachmenti_rbos[colorAttachmentIndex]
                    == renderBufferSharedPtr) {
            boundFboProps(target).colorAttachmenti_rbos[colorAttachmentIndex] = nullptr;
            boundFboProps(target).colorAttachmenti_hasRbo[colorAttachmentIndex] = false;
        }
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        if (boundFboProps(target).depthAttachment_rbo == renderBufferSharedPtr &&
            boundFboProps(target).depthAttachment_hasRbo) {
            boundFboProps(target).depthAttachment_rbo = nullptr;
            boundFboProps(target).depthAttachment_hasRbo = false;
        }
        break;
    case GL_STENCIL_ATTACHMENT:
        if (boundFboProps(target).stencilAttachment_rbo == renderBufferSharedPtr &&
            boundFboProps(target).stencilAttachment_hasRbo) {
            boundFboProps(target).stencilAttachment_rbo = nullptr;
            boundFboProps(target).stencilAttachment_hasRbo = false;
        }
        break;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        if (boundFboProps(target).depthAttachment_rbo == renderBufferSharedPtr &&
            boundFboProps(target).depthAttachment_hasRbo) {
            boundFboProps(target).depthAttachment_rbo = nullptr;
            boundFboProps(target).depthAttachment_hasRbo = false;
        }
        if (boundFboProps(target).stencilAttachment_rbo == renderBufferSharedPtr &&
            boundFboProps(target).stencilAttachment_hasRbo) {
            boundFboProps(target).stencilAttachment_rbo = nullptr;
            boundFboProps(target).stencilAttachment_hasRbo = false;
        }
        if (boundFboProps(target).depthstencilAttachment_rbo == renderBufferSharedPtr &&
            boundFboProps(target).depthstencilAttachment_hasRbo) {
            boundFboProps(target).depthstencilAttachment_rbo = nullptr;
            boundFboProps(target).depthstencilAttachment_hasRbo = false;
        }
        break;
    }
}

void GLClientState::attachRbo(GLenum target, GLenum attachment, GLuint renderbuffer) {

    bool attach = 0 != renderbuffer;

    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    boundFboProps(target).completenessDirty = true;

    RenderbufferInfo::ScopedView view(mRboState.rboData);
    auto rboSharedPtr = view.get_or_add_shared_ptr(renderbuffer);
    if (colorAttachmentIndex != -1) {
        boundFboProps(target).colorAttachmenti_rbos[colorAttachmentIndex] = rboSharedPtr;
        boundFboProps(target).colorAttachmenti_hasRbo[colorAttachmentIndex] = attach;
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        boundFboProps(target).depthAttachment_rbo = rboSharedPtr;
        boundFboProps(target).depthAttachment_hasRbo = attach;
        break;
    case GL_STENCIL_ATTACHMENT:
        boundFboProps(target).stencilAttachment_rbo = rboSharedPtr;
        boundFboProps(target).stencilAttachment_hasRbo = attach;
        break;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        boundFboProps(target).depthAttachment_rbo = rboSharedPtr;
        boundFboProps(target).depthAttachment_hasRbo = attach;
        boundFboProps(target).stencilAttachment_rbo = rboSharedPtr;
        boundFboProps(target).stencilAttachment_hasRbo = attach;
        boundFboProps(target).depthstencilAttachment_rbo = rboSharedPtr;
        boundFboProps(target).depthstencilAttachment_hasRbo = attach;
        break;
    }
}

std::shared_ptr<RboProps> GLClientState::getFboAttachmentRbo(GLenum target, GLenum attachment) const {
    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    if (colorAttachmentIndex != -1) {
        return boundFboProps_const(target).colorAttachmenti_rbos[colorAttachmentIndex];
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        return  boundFboProps_const(target).depthAttachment_rbo;
    case GL_STENCIL_ATTACHMENT:
        return  boundFboProps_const(target).stencilAttachment_rbo;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        return  boundFboProps_const(target).depthstencilAttachment_rbo;
    }

    // Bad attachment enum. Should be unreachable.
    return nullptr;
}

void GLClientState::setFboCompletenessDirtyForTexture(GLuint texture) {
    std::shared_ptr<TextureRec> texrec = getTextureRec(texture);
    std::map<GLuint, FboProps>::iterator it = mFboState.fboData.begin();
    while (it != mFboState.fboData.end()) {
        FboProps& props = it->second;
        for (int i = 0; i < m_hostDriverCaps.max_color_attachments; ++i) {
            if (props.colorAttachmenti_hasTex[i]) {
                if (texrec == props.colorAttachmenti_textures[i]) {
                    props.completenessDirty = true;
                    return;
                }
            }
        }

        if (props.depthAttachment_hasTexObj) {
            if (texrec == props.depthAttachment_texture) {
                    props.completenessDirty = true;
                    return;
            }
        }

        if (props.stencilAttachment_hasTexObj) {
            if (texrec == props.stencilAttachment_texture) {
                props.completenessDirty = true;
                return;
            }
        }

        if (props.depthstencilAttachment_hasTexObj) {
            if (texrec == props.depthstencilAttachment_texture) {
                props.completenessDirty = true;
                return;
            }
        }
        ++it;
    }
}

void GLClientState::setFboCompletenessDirtyForRbo(std::shared_ptr<RboProps> rbo) {
    std::map<GLuint, FboProps>::iterator it = mFboState.fboData.begin();
    while (it != mFboState.fboData.end()) {
        FboProps& props = it->second;
        for (int i = 0; i < m_hostDriverCaps.max_color_attachments; ++i) {
            if (props.colorAttachmenti_hasRbo[i]) {
                if (rbo == props.colorAttachmenti_rbos[i]) {
                    props.completenessDirty = true;
                    return;
                }
            }
        }

        if (props.depthAttachment_hasRbo) {
            if (rbo == props.depthAttachment_rbo) {
                    props.completenessDirty = true;
                    return;
            }
        }

        if (props.stencilAttachment_hasRbo) {
            if (rbo == props.stencilAttachment_rbo) {
                props.completenessDirty = true;
                return;
            }
        }

        if (props.depthstencilAttachment_hasRbo) {
            if (rbo == props.depthstencilAttachment_rbo) {
                props.completenessDirty = true;
                return;
            }
        }
        ++it;
    }
}

bool GLClientState::attachmentHasObject(GLenum target, GLenum attachment) const {
    bool res = true; // liberal

    int colorAttachmentIndex =
        glUtilsColorAttachmentIndex(attachment);

    if (colorAttachmentIndex != -1) {
        res = boundFboProps_const(target).colorAttachmenti_hasTex[colorAttachmentIndex] ||
              boundFboProps_const(target).colorAttachmenti_hasRbo[colorAttachmentIndex];
    }

    switch (attachment) {
    case GL_DEPTH_ATTACHMENT:
        res = (boundFboProps_const(target).depthAttachment_hasTexObj) ||
              (boundFboProps_const(target).depthAttachment_hasRbo);
        break;
    case GL_STENCIL_ATTACHMENT:
        res = (boundFboProps_const(target).stencilAttachment_hasTexObj) ||
              (boundFboProps_const(target).stencilAttachment_hasRbo);
        break;
    case GL_DEPTH_STENCIL_ATTACHMENT:
        res = (boundFboProps_const(target).depthstencilAttachment_hasTexObj) ||
              (boundFboProps_const(target).depthstencilAttachment_hasRbo);
        break;
    }
    return res;
}

bool GLClientState::depthStencilHasSameObject(GLenum target) const {
    const FboProps& props = boundFboProps_const(target);

    if (props.depthAttachment_hasTexObj != props.stencilAttachment_hasTexObj
            || props.depthAttachment_hasRbo != props.stencilAttachment_hasRbo) {
        return false;
    }
    if (props.depthAttachment_hasTexObj) {
        return props.depthAttachment_texture == props.stencilAttachment_texture;
    }
    if (props.depthAttachment_hasRbo) {
        return props.depthAttachment_rbo == props.stencilAttachment_rbo;
    }
    // No attachment in either
    return true;
}

void GLClientState::setTransformFeedbackActive(bool active) {
    m_transformFeedbackActive = active;
}

void GLClientState::setTransformFeedbackUnpaused(bool unpaused) {
    m_transformFeedbackUnpaused = unpaused;
}

void GLClientState::setTransformFeedbackVaryingsCountForLinking(uint32_t count) {
    m_transformFeedbackVaryingsCountForLinking = count;
}

bool GLClientState::getTransformFeedbackActive() const {
    return m_transformFeedbackActive;
}

bool GLClientState::getTransformFeedbackUnpaused() const {
    return m_transformFeedbackUnpaused;
}

bool GLClientState::getTransformFeedbackActiveUnpaused() const {
    return m_transformFeedbackActive && m_transformFeedbackUnpaused;
}

uint32_t GLClientState::getTransformFeedbackVaryingsCountForLinking() const {
    return m_transformFeedbackVaryingsCountForLinking;
}

void GLClientState::stencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state_GL_STENCIL_FUNC = func;
        state_GL_STENCIL_REF = ref;
        state_GL_STENCIL_VALUE_MASK = mask;
    }

    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state_GL_STENCIL_BACK_FUNC = func;
        state_GL_STENCIL_BACK_REF = ref;
        state_GL_STENCIL_BACK_VALUE_MASK = mask;
    }
}

void GLClientState::stencilMaskSeparate(GLenum face, GLuint mask) {
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state_GL_STENCIL_WRITEMASK = mask;
    }

    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state_GL_STENCIL_BACK_WRITEMASK = mask;
    }
}

void GLClientState::stencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) {
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        state_GL_STENCIL_FAIL = fail;
        state_GL_STENCIL_PASS_DEPTH_FAIL = zfail;
        state_GL_STENCIL_PASS_DEPTH_PASS = zpass;
    }

    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        state_GL_STENCIL_BACK_FAIL = fail;
        state_GL_STENCIL_BACK_PASS_DEPTH_FAIL = zfail;
        state_GL_STENCIL_BACK_PASS_DEPTH_PASS = zpass;
    }
}

void GLClientState::setTextureData(SharedTextureDataMap* sharedTexData) {
    m_tex.textureRecs = sharedTexData;
}

void GLClientState::setRenderbufferInfo(RenderbufferInfo* rbInfo) {
    mRboState.rboData = rbInfo;
    if (rbInfo) {
        RenderbufferInfo::ScopedView view(mRboState.rboData);
        auto rbo = view.get_or_add_shared_ptr(0);
        mRboState.boundRenderbuffer = rbo;
    }
}

void GLClientState::setSamplerInfo(SamplerInfo* samplerInfo) {
    mSamplerInfo = samplerInfo;
}

bool GLClientState::compressedTexImageSizeCompatible(GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLsizei imageSize) {
    bool error = false;
    GLsizei compressedSize = GLESTextureUtils::getCompressedImageSize(internalformat, width, height, depth, &error);
    if (error) return false;
    return imageSize == compressedSize;
}

void GLClientState::fromMakeCurrent() {
    if (mFboState.fboData.find(0) == mFboState.fboData.end()) {
        addFreshFramebuffer(0);
        FboProps& default_fb_props = mFboState.fboData[0];
        default_fb_props.colorAttachmenti_hasRbo[0] = true;
        default_fb_props.depthAttachment_hasRbo = true;
        default_fb_props.stencilAttachment_hasRbo = true;
        default_fb_props.depthstencilAttachment_hasRbo = true;
        RenderbufferInfo::ScopedView view(mRboState.rboData);
        // Use RBO 0 as placeholder
        auto rbo0 = view.get_or_add_shared_ptr(0);
        default_fb_props.colorAttachmenti_rbos[0] = rbo0;
        default_fb_props.depthAttachment_rbo = rbo0;
        default_fb_props.stencilAttachment_rbo = rbo0;
        default_fb_props.depthstencilAttachment_rbo = rbo0;

    }

    if (!samplerExists(0)) {
        GLuint id = 0;
        setExistence(ObjectType::Sampler, true, 1, &id);
    }

}

void GLClientState::initFromCaps(
    const HostDriverCaps& caps) {
    m_hostDriverCaps = caps;

    // Override some of them
    m_hostDriverCaps.max_vertex_attribs = CODEC_MAX_VERTEX_ATTRIBUTES;
    m_hostDriverCaps.max_vertex_attrib_bindings = m_hostDriverCaps.max_vertex_attribs;

    // Derive some other settings
    m_log2MaxTextureSize = 0;
    int current = 1;
    while (current < m_hostDriverCaps.max_texture_size) {
        current = current << 1;
        ++m_log2MaxTextureSize;
    }

    if (m_glesMajorVersion >= 3) {
        if (m_hostDriverCaps.max_transform_feedback_separate_attribs)
            m_indexedTransformFeedbackBuffers.resize(m_hostDriverCaps.max_transform_feedback_separate_attribs);
        if (m_hostDriverCaps.max_uniform_buffer_bindings)
            m_indexedUniformBuffers.resize(m_hostDriverCaps.max_uniform_buffer_bindings);
        if (m_hostDriverCaps.max_atomic_counter_buffer_bindings)
            m_indexedAtomicCounterBuffers.resize(m_hostDriverCaps.max_atomic_counter_buffer_bindings);
        if (m_hostDriverCaps.max_shader_storage_buffer_bindings)
            m_indexedShaderStorageBuffers.resize(m_hostDriverCaps.max_shader_storage_buffer_bindings);

        BufferBinding buf0Binding;
        buf0Binding.buffer = 0;
        buf0Binding.offset = 0;
        buf0Binding.size = 0;
        buf0Binding.stride = 0;
        buf0Binding.effectiveStride = 0;

        for (size_t i = 0; i < m_indexedTransformFeedbackBuffers.size(); ++i)
            m_indexedTransformFeedbackBuffers[i] = buf0Binding;
        for (size_t i = 0; i < m_indexedUniformBuffers.size(); ++i)
            m_indexedUniformBuffers[i] = buf0Binding;
        for (size_t i = 0; i < m_indexedAtomicCounterBuffers.size(); ++i)
            m_indexedAtomicCounterBuffers[i] = buf0Binding;
        for (size_t i = 0; i < m_indexedShaderStorageBuffers.size(); ++i)
            m_indexedShaderStorageBuffers[i] = buf0Binding;
    }

    addFreshFramebuffer(0);

    m_initialized = true;
}

bool GLClientState::needsInitFromCaps() const {
    return !m_initialized;
}

void GLClientState::setExtensions(const std::string& extensions) {
    if (!m_extensions_set) m_extensions = extensions;

    m_has_color_buffer_float_extension =
        hasExtension("GL_EXT_color_buffer_float");
    m_has_color_buffer_half_float_extension =
        hasExtension("GL_EXT_color_buffer_half_float");
    m_extensions_set = true;
}

bool GLClientState::hasExtension(const char* ext) const {
    return m_extensions.find(ext) != std::string::npos;
}

using gfxstream::guest::AutoLock;
using gfxstream::guest::Lock;

// A process-wide fence registry (because we can use fence sync objects across multiple contexts)
struct FenceRegistry {
    Lock lock;
    PredicateMap<uint64_t, false> existence;

    void onFenceCreated(GLsync sync) {
        AutoLock<Lock> scopedLock(lock);
        uint64_t asUint64 = (uint64_t)(uintptr_t)(sync);
        existence.add(asUint64);
        existence.set(asUint64, true);
    }

    void onFenceDestroyed(GLsync sync) {
        AutoLock<Lock> scopedLock(lock);
        uint64_t asUint64 = (uint64_t)(uintptr_t)(sync);
        existence.remove(asUint64);
    }

    bool exists(GLsync sync) {
        AutoLock<Lock> scopedLock(lock);
        uint64_t asUint64 = (uint64_t)(uintptr_t)(sync);
        return existence.get(asUint64);
    }
};

static FenceRegistry sFenceRegistry;

void GLClientState::onFenceCreated(GLsync sync) {
    sFenceRegistry.onFenceCreated(sync);
}

void GLClientState::onFenceDestroyed(GLsync sync) {
    sFenceRegistry.onFenceDestroyed(sync);
}

bool GLClientState::fenceExists(GLsync sync) {
    return sFenceRegistry.exists(sync);
}

}  // namespace guest
}  // namespace gfxstream
