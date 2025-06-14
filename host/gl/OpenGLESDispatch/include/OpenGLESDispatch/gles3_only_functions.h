// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Auto-generated with: ./scripts/gen-entries.py --mode=funcargs host/gl/OpenGLESDispatch/gles3_only.entries --output=host/gl/OpenGLESDispatch/include/OpenGLESDispatch/gles3_only_functions.h
// DO NOT EDIT THIS FILE

#ifndef GLES3_ONLY_FUNCTIONS_H
#define GLES3_ONLY_FUNCTIONS_H

#include <GLES/gl.h>
#include <GLES3/gl3.h>
#define LIST_GLES3_ONLY_FUNCTIONS(X) \
  X(GLconstubyteptr, glGetStringi, (GLenum name, GLint index), (name, index)) \
  X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays), (n, arrays)) \
  X(void, glBindVertexArray, (GLuint array), (array)) \
  X(void, glDeleteVertexArrays, (GLsizei n, const GLuint * arrays), (n, arrays)) \
  X(GLboolean, glIsVertexArray, (GLuint array), (array)) \
  X(void *, glMapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access), (target, offset, length, access)) \
  X(GLboolean, glUnmapBuffer, (GLenum target), (target)) \
  X(void, glFlushMappedBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length), (target, offset, length)) \
  X(void, glBindBufferRange, (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size), (target, index, buffer, offset, size)) \
  X(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer), (target, index, buffer)) \
  X(void, glCopyBufferSubData, (GLenum readtarget, GLenum writetarget, GLintptr readoffset, GLintptr writeoffset, GLsizeiptr size), (readtarget, writetarget, readoffset, writeoffset, size)) \
  X(void, glClearBufferiv, (GLenum buffer, GLint drawBuffer, const GLint * value), (buffer, drawBuffer, value)) \
  X(void, glClearBufferuiv, (GLenum buffer, GLint drawBuffer, const GLuint * value), (buffer, drawBuffer, value)) \
  X(void, glClearBufferfv, (GLenum buffer, GLint drawBuffer, const GLfloat * value), (buffer, drawBuffer, value)) \
  X(void, glClearBufferfi, (GLenum buffer, GLint drawBuffer, GLfloat depth, GLint stencil), (buffer, drawBuffer, depth, stencil)) \
  X(void, glGetBufferParameteri64v, (GLenum target, GLenum value, GLint64 * data), (target, value, data)) \
  X(void, glGetBufferPointerv, (GLenum target, GLenum pname, GLvoid ** params), (target, pname, params)) \
  X(void, glUniformBlockBinding, (GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding), (program, uniformBlockIndex, uniformBlockBinding)) \
  X(GLuint, glGetUniformBlockIndex, (GLuint program, const GLchar * uniformBlockName), (program, uniformBlockName)) \
  X(void, glGetUniformIndices, (GLuint program, GLsizei uniformCount, const GLchar ** uniformNames, GLuint * uniformIndices), (program, uniformCount, uniformNames, uniformIndices)) \
  X(void, glGetActiveUniformBlockiv, (GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint * params), (program, uniformBlockIndex, pname, params)) \
  X(void, glGetActiveUniformBlockName, (GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei * length, GLchar * uniformBlockName), (program, uniformBlockIndex, bufSize, length, uniformBlockName)) \
  X(void, glUniform1ui, (GLint location, GLuint v0), (location, v0)) \
  X(void, glUniform2ui, (GLint location, GLuint v0, GLuint v1), (location, v0, v1)) \
  X(void, glUniform3ui, (GLint location, GLuint v0, GLuint v1, GLuint v2), (location, v0, v1, v2)) \
  X(void, glUniform4ui, (GLint location, GLint v0, GLuint v1, GLuint v2, GLuint v3), (location, v0, v1, v2, v3)) \
  X(void, glUniform1uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniform2uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniform3uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniform4uiv, (GLint location, GLsizei count, const GLuint * value), (location, count, value)) \
  X(void, glUniformMatrix2x3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix3x2fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix2x4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix4x2fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix3x4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glUniformMatrix4x3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat * value), (location, count, transpose, value)) \
  X(void, glGetUniformuiv, (GLuint program, GLint location, GLuint * params), (program, location, params)) \
  X(void, glGetActiveUniformsiv, (GLuint program, GLsizei uniformCount, const GLuint * uniformIndices, GLenum pname, GLint * params), (program, uniformCount, uniformIndices, pname, params)) \
  X(void, glVertexAttribI4i, (GLuint index, GLint v0, GLint v1, GLint v2, GLint v3), (index, v0, v1, v2, v3)) \
  X(void, glVertexAttribI4ui, (GLuint index, GLuint v0, GLuint v1, GLuint v2, GLuint v3), (index, v0, v1, v2, v3)) \
  X(void, glVertexAttribI4iv, (GLuint index, const GLint * v), (index, v)) \
  X(void, glVertexAttribI4uiv, (GLuint index, const GLuint * v), (index, v)) \
  X(void, glVertexAttribIPointer, (GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (index, size, type, stride, pointer)) \
  X(void, glGetVertexAttribIiv, (GLuint index, GLenum pname, GLint * params), (index, pname, params)) \
  X(void, glGetVertexAttribIuiv, (GLuint index, GLenum pname, GLuint * params), (index, pname, params)) \
  X(void, glVertexAttribDivisor, (GLuint index, GLuint divisor), (index, divisor)) \
  X(void, glDrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei primcount), (mode, first, count, primcount)) \
  X(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void * indices, GLsizei primcount), (mode, count, type, indices, primcount)) \
  X(void, glDrawRangeElements, (GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid * indices), (mode, start, end, count, type, indices)) \
  X(GLsync, glFenceSync, (GLenum condition, GLbitfield flags), (condition, flags)) \
  X(GLenum, glClientWaitSync, (GLsync wait_on, GLbitfield flags, GLuint64 timeout), (wait_on, flags, timeout)) \
  X(void, glWaitSync, (GLsync wait_on, GLbitfield flags, GLuint64 timeout), (wait_on, flags, timeout)) \
  X(void, glDeleteSync, (GLsync to_delete), (to_delete)) \
  X(GLboolean, glIsSync, (GLsync sync), (sync)) \
  X(void, glGetSynciv, (GLsync sync, GLenum pname, GLsizei bufSize, GLsizei * length, GLint * values), (sync, pname, bufSize, length, values)) \
  X(void, glDrawBuffers, (GLsizei n, const GLenum * bufs), (n, bufs)) \
  X(void, glReadBuffer, (GLenum src), (src)) \
  X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter), (srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter)) \
  X(void, glInvalidateFramebuffer, (GLenum target, GLsizei numAttachments, const GLenum * attachments), (target, numAttachments, attachments)) \
  X(void, glInvalidateSubFramebuffer, (GLenum target, GLsizei numAttachments, const GLenum * attachments, GLint x, GLint y, GLsizei width, GLsizei height), (target, numAttachments, attachments, x, y, width, height)) \
  X(void, glFramebufferTextureLayer, (GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer), (target, attachment, texture, level, layer)) \
  X(void, glGetInternalformativ, (GLenum target, GLenum internalformat, GLenum pname, GLsizei bufSize, GLint * params), (target, internalformat, pname, bufSize, params)) \
  X(void, glTexStorage2D, (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height), (target, levels, internalformat, width, height)) \
  X(void, glBeginTransformFeedback, (GLenum primitiveMode), (primitiveMode)) \
  X(void, glEndTransformFeedback, (), ()) \
  X(void, glGenTransformFeedbacks, (GLsizei n, GLuint * ids), (n, ids)) \
  X(void, glDeleteTransformFeedbacks, (GLsizei n, const GLuint * ids), (n, ids)) \
  X(void, glBindTransformFeedback, (GLenum target, GLuint id), (target, id)) \
  X(void, glPauseTransformFeedback, (), ()) \
  X(void, glResumeTransformFeedback, (), ()) \
  X(GLboolean, glIsTransformFeedback, (GLuint id), (id)) \
  X(void, glTransformFeedbackVaryings, (GLuint program, GLsizei count, const char ** varyings, GLenum bufferMode), (program, count, varyings, bufferMode)) \
  X(void, glGetTransformFeedbackVarying, (GLuint program, GLuint index, GLsizei bufSize, GLsizei * length, GLsizei * size, GLenum * type, char * name), (program, index, bufSize, length, size, type, name)) \
  X(void, glGenSamplers, (GLsizei n, GLuint * samplers), (n, samplers)) \
  X(void, glDeleteSamplers, (GLsizei n, const GLuint * samplers), (n, samplers)) \
  X(void, glBindSampler, (GLuint unit, GLuint sampler), (unit, sampler)) \
  X(void, glSamplerParameterf, (GLuint sampler, GLenum pname, GLfloat param), (sampler, pname, param)) \
  X(void, glSamplerParameteri, (GLuint sampler, GLenum pname, GLint param), (sampler, pname, param)) \
  X(void, glSamplerParameterfv, (GLuint sampler, GLenum pname, const GLfloat * params), (sampler, pname, params)) \
  X(void, glSamplerParameteriv, (GLuint sampler, GLenum pname, const GLint * params), (sampler, pname, params)) \
  X(void, glGetSamplerParameterfv, (GLuint sampler, GLenum pname, GLfloat * params), (sampler, pname, params)) \
  X(void, glGetSamplerParameteriv, (GLuint sampler, GLenum pname, GLint * params), (sampler, pname, params)) \
  X(GLboolean, glIsSampler, (GLuint sampler), (sampler)) \
  X(void, glGenQueries, (GLsizei n, GLuint * queries), (n, queries)) \
  X(void, glDeleteQueries, (GLsizei n, const GLuint * queries), (n, queries)) \
  X(void, glBeginQuery, (GLenum target, GLuint query), (target, query)) \
  X(void, glEndQuery, (GLenum target), (target)) \
  X(void, glGetQueryiv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glGetQueryObjectuiv, (GLuint query, GLenum pname, GLuint * params), (query, pname, params)) \
  X(GLboolean, glIsQuery, (GLuint query), (query)) \
  X(void, glProgramParameteri, (GLuint program, GLenum pname, GLint value), (program, pname, value)) \
  X(void, glProgramBinary, (GLuint program, GLenum binaryFormat, const void * binary, GLsizei length), (program, binaryFormat, binary, length)) \
  X(void, glGetProgramBinary, (GLuint program, GLsizei bufsize, GLsizei * length, GLenum * binaryFormat, void * binary), (program, bufsize, length, binaryFormat, binary)) \
  X(GLint, glGetFragDataLocation, (GLuint program, const char * name), (program, name)) \
  X(void, glGetInteger64v, (GLenum pname, GLint64 * data), (pname, data)) \
  X(void, glGetIntegeri_v, (GLenum target, GLuint index, GLint * data), (target, index, data)) \
  X(void, glGetInteger64i_v, (GLenum target, GLuint index, GLint64 * data), (target, index, data)) \
  X(void, glTexImage3D, (GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid * data), (target, level, internalFormat, width, height, depth, border, format, type, data)) \
  X(void, glTexStorage3D, (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth), (target, levels, internalformat, width, height, depth)) \
  X(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid * data), (target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, data)) \
  X(void, glCompressedTexImage3D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid * data), (target, level, internalformat, width, height, depth, border, imageSize, data)) \
  X(void, glCompressedTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const GLvoid * data), (target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data)) \
  X(void, glCopyTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLint x, GLint y, GLsizei width, GLsizei height), (target, level, xoffset, yoffset, zoffset, x, y, width, height)) \


#endif  // GLES3_ONLY_FUNCTIONS_H
