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

// Auto-generated with: ./scripts/gen-entries.py --mode=static_translator_namespaced_header host/gl/OpenGLESDispatch/gles1_only.entries --output=host/gl/OpenGLESDispatch/include/OpenGLESDispatch/gles1_only_static_translator_namespaced_header.h
// DO NOT EDIT THIS FILE

#pragma once

#include <GLES/gl.h>

namespace translator {
namespace gles1 {
GL_APICALL void GL_APIENTRY glAlphaFunc(GLenum func, GLclampf ref);
GL_APICALL void GL_APIENTRY glBegin(GLenum mode);
GL_APICALL void GL_APIENTRY glClientActiveTexture(GLenum texture);
GL_APICALL void GL_APIENTRY glClipPlane(GLenum plane, const GLdouble * equation);
GL_APICALL void GL_APIENTRY glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
GL_APICALL void GL_APIENTRY glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
GL_APICALL void GL_APIENTRY glColor4fv(const GLfloat * v);
GL_APICALL void GL_APIENTRY glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
GL_APICALL void GL_APIENTRY glColor4ubv(const GLubyte * v);
GL_APICALL void GL_APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid * pointer);
GL_APICALL void GL_APIENTRY glDisableClientState(GLenum array);
GL_APICALL void GL_APIENTRY glEnableClientState(GLenum array);
GL_APICALL void GL_APIENTRY glEnd();
GL_APICALL void GL_APIENTRY glFogf(GLenum pname, GLfloat param);
GL_APICALL void GL_APIENTRY glFogfv(GLenum pname, const GLfloat * params);
GL_APICALL void GL_APIENTRY glGetClipPlane(GLenum plane, GLdouble * equation);
GL_APICALL void GL_APIENTRY glGetDoublev(GLenum pname, GLdouble * params);
GL_APICALL void GL_APIENTRY glGetLightfv(GLenum light, GLenum pname, GLfloat * params);
GL_APICALL void GL_APIENTRY glGetMaterialfv(GLenum face, GLenum pname, GLfloat * params);
GL_APICALL void GL_APIENTRY glGetPointerv(GLenum pname, GLvoid* * params);
GL_APICALL void GL_APIENTRY glGetTexEnvfv(GLenum target, GLenum pname, GLfloat * params);
GL_APICALL void GL_APIENTRY glGetTexEnviv(GLenum target, GLenum pname, GLint * params);
GL_APICALL void GL_APIENTRY glLightf(GLenum light, GLenum pname, GLfloat param);
GL_APICALL void GL_APIENTRY glLightfv(GLenum light, GLenum pname, const GLfloat * params);
GL_APICALL void GL_APIENTRY glLightModelf(GLenum pname, GLfloat param);
GL_APICALL void GL_APIENTRY glLightModelfv(GLenum pname, const GLfloat * params);
GL_APICALL void GL_APIENTRY glLoadIdentity();
GL_APICALL void GL_APIENTRY glLoadMatrixf(const GLfloat * m);
GL_APICALL void GL_APIENTRY glLogicOp(GLenum opcode);
GL_APICALL void GL_APIENTRY glMaterialf(GLenum face, GLenum pname, GLfloat param);
GL_APICALL void GL_APIENTRY glMaterialfv(GLenum face, GLenum pname, const GLfloat * params);
GL_APICALL void GL_APIENTRY glMultiTexCoord2fv(GLenum target, const GLfloat * v);
GL_APICALL void GL_APIENTRY glMultiTexCoord2sv(GLenum target, const GLshort * v);
GL_APICALL void GL_APIENTRY glMultiTexCoord3fv(GLenum target, const GLfloat * v);
GL_APICALL void GL_APIENTRY glMultiTexCoord3sv(GLenum target, const GLshort * v);
GL_APICALL void GL_APIENTRY glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
GL_APICALL void GL_APIENTRY glMultiTexCoord4fv(GLenum target, const GLfloat * v);
GL_APICALL void GL_APIENTRY glMultiTexCoord4sv(GLenum target, const GLshort * v);
GL_APICALL void GL_APIENTRY glMultMatrixf(const GLfloat * m);
GL_APICALL void GL_APIENTRY glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
GL_APICALL void GL_APIENTRY glNormal3fv(const GLfloat * v);
GL_APICALL void GL_APIENTRY glNormal3sv(const GLshort * v);
GL_APICALL void GL_APIENTRY glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
GL_APICALL void GL_APIENTRY glPointParameterf(GLenum param, GLfloat value);
GL_APICALL void GL_APIENTRY glPointParameterfv(GLenum param, const GLfloat * values);
GL_APICALL void GL_APIENTRY glPointSize(GLfloat size);
GL_APICALL void GL_APIENTRY glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
GL_APICALL void GL_APIENTRY glScalef(GLfloat x, GLfloat y, GLfloat z);
GL_APICALL void GL_APIENTRY glTexEnvf(GLenum target, GLenum pname, GLfloat param);
GL_APICALL void GL_APIENTRY glTexEnvfv(GLenum target, GLenum pname, const GLfloat * params);
GL_APICALL void GL_APIENTRY glMatrixMode(GLenum mode);
GL_APICALL void GL_APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid * pointer);
GL_APICALL void GL_APIENTRY glPopMatrix();
GL_APICALL void GL_APIENTRY glPushMatrix();
GL_APICALL void GL_APIENTRY glShadeModel(GLenum mode);
GL_APICALL void GL_APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid * pointer);
GL_APICALL void GL_APIENTRY glTexEnvi(GLenum target, GLenum pname, GLint param);
GL_APICALL void GL_APIENTRY glTexEnviv(GLenum target, GLenum pname, const GLint * params);
GL_APICALL void GL_APIENTRY glTranslatef(GLfloat x, GLfloat y, GLfloat z);
GL_APICALL void GL_APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid * pointer);
GL_APICALL void GL_APIENTRY glClipPlanef(GLenum plane, const GLfloat * equation);
GL_APICALL void GL_APIENTRY glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
GL_APICALL void GL_APIENTRY glGetClipPlanef(GLenum pname, GLfloat eqn[4]);
GL_APICALL void GL_APIENTRY glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar);
GL_APICALL void GL_APIENTRY glAlphaFuncx(GLenum func, GLclampx ref);
GL_APICALL void GL_APIENTRY glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
GL_APICALL void GL_APIENTRY glClearDepthx(GLclampx depth);
GL_APICALL void GL_APIENTRY glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
GL_APICALL void GL_APIENTRY glDepthRangex(GLclampx zNear, GLclampx zFar);
GL_APICALL void GL_APIENTRY glFogx(GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glFogxv(GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
GL_APICALL void GL_APIENTRY glClipPlanex(GLenum pname, const GLfixed * eqn);
GL_APICALL void GL_APIENTRY glGetFixedv(GLenum pname, GLfixed * params);
GL_APICALL void GL_APIENTRY glGetLightxv(GLenum light, GLenum pname, GLfixed * params);
GL_APICALL void GL_APIENTRY glGetMaterialxv(GLenum face, GLenum pname, GLfixed * params);
GL_APICALL void GL_APIENTRY glGetTexEnvxv(GLenum env, GLenum pname, GLfixed * params);
GL_APICALL void GL_APIENTRY glGetTexParameterxv(GLenum target, GLenum pname, GLfixed * params);
GL_APICALL void GL_APIENTRY glLightModelx(GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glLightModelxv(GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glLightx(GLenum light, GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glLightxv(GLenum light, GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glLineWidthx(GLfixed width);
GL_APICALL void GL_APIENTRY glLoadMatrixx(const GLfixed * m);
GL_APICALL void GL_APIENTRY glMaterialx(GLenum face, GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glMaterialxv(GLenum face, GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glMultMatrixx(const GLfixed * m);
GL_APICALL void GL_APIENTRY glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q);
GL_APICALL void GL_APIENTRY glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz);
GL_APICALL void GL_APIENTRY glOrthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
GL_APICALL void GL_APIENTRY glPointParameterx(GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glPointParameterxv(GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glPointSizex(GLfixed size);
GL_APICALL void GL_APIENTRY glPolygonOffsetx(GLfixed factor, GLfixed units);
GL_APICALL void GL_APIENTRY glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
GL_APICALL void GL_APIENTRY glSampleCoveragex(GLclampx value, GLboolean invert);
GL_APICALL void GL_APIENTRY glScalex(GLfixed x, GLfixed y, GLfixed z);
GL_APICALL void GL_APIENTRY glTexEnvx(GLenum target, GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glTexEnvxv(GLenum target, GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glTexParameterx(GLenum target, GLenum pname, GLfixed param);
GL_APICALL void GL_APIENTRY glTexParameterxv(GLenum target, GLenum pname, const GLfixed * params);
GL_APICALL void GL_APIENTRY glTranslatex(GLfixed x, GLfixed y, GLfixed z);
GL_APICALL void GL_APIENTRY glGetClipPlanex(GLenum pname, GLfixed eqn[4]);
} // namespace translator
} // namespace gles1
