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

// Auto-generated with: ./scripts/gen-entries.py --mode=funcargs host/gl/OpenGLESDispatch/gles1_only.entries --output=host/gl/OpenGLESDispatch/include/OpenGLESDispatch/gles1_only_functions.h
// DO NOT EDIT THIS FILE

#ifndef GLES1_ONLY_FUNCTIONS_H
#define GLES1_ONLY_FUNCTIONS_H

#define LIST_GLES1_ONLY_FUNCTIONS(X) \
  X(void, glAlphaFunc, (GLenum func, GLclampf ref), (func, ref)) \
  X(void, glBegin, (GLenum mode), (mode)) \
  X(void, glClientActiveTexture, (GLenum texture), (texture)) \
  X(void, glClipPlane, (GLenum plane, const GLdouble * equation), (plane, equation)) \
  X(void, glColor4d, (GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha), (red, green, blue, alpha)) \
  X(void, glColor4f, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha), (red, green, blue, alpha)) \
  X(void, glColor4fv, (const GLfloat * v), (v)) \
  X(void, glColor4ub, (GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha), (red, green, blue, alpha)) \
  X(void, glColor4ubv, (const GLubyte * v), (v)) \
  X(void, glColorPointer, (GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (size, type, stride, pointer)) \
  X(void, glDisableClientState, (GLenum array), (array)) \
  X(void, glEnableClientState, (GLenum array), (array)) \
  X(void, glEnd, (), ()) \
  X(void, glFogf, (GLenum pname, GLfloat param), (pname, param)) \
  X(void, glFogfv, (GLenum pname, const GLfloat * params), (pname, params)) \
  X(void, glGetClipPlane, (GLenum plane, GLdouble * equation), (plane, equation)) \
  X(void, glGetDoublev, (GLenum pname, GLdouble * params), (pname, params)) \
  X(void, glGetLightfv, (GLenum light, GLenum pname, GLfloat * params), (light, pname, params)) \
  X(void, glGetMaterialfv, (GLenum face, GLenum pname, GLfloat * params), (face, pname, params)) \
  X(void, glGetPointerv, (GLenum pname, GLvoid* * params), (pname, params)) \
  X(void, glGetTexEnvfv, (GLenum target, GLenum pname, GLfloat * params), (target, pname, params)) \
  X(void, glGetTexEnviv, (GLenum target, GLenum pname, GLint * params), (target, pname, params)) \
  X(void, glLightf, (GLenum light, GLenum pname, GLfloat param), (light, pname, param)) \
  X(void, glLightfv, (GLenum light, GLenum pname, const GLfloat * params), (light, pname, params)) \
  X(void, glLightModelf, (GLenum pname, GLfloat param), (pname, param)) \
  X(void, glLightModelfv, (GLenum pname, const GLfloat * params), (pname, params)) \
  X(void, glLoadIdentity, (), ()) \
  X(void, glLoadMatrixf, (const GLfloat * m), (m)) \
  X(void, glLogicOp, (GLenum opcode), (opcode)) \
  X(void, glMaterialf, (GLenum face, GLenum pname, GLfloat param), (face, pname, param)) \
  X(void, glMaterialfv, (GLenum face, GLenum pname, const GLfloat * params), (face, pname, params)) \
  X(void, glMultiTexCoord2fv, (GLenum target, const GLfloat * v), (target, v)) \
  X(void, glMultiTexCoord2sv, (GLenum target, const GLshort * v), (target, v)) \
  X(void, glMultiTexCoord3fv, (GLenum target, const GLfloat * v), (target, v)) \
  X(void, glMultiTexCoord3sv, (GLenum target, const GLshort * v), (target, v)) \
  X(void, glMultiTexCoord4f, (GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q), (target, s, t, r, q)) \
  X(void, glMultiTexCoord4fv, (GLenum target, const GLfloat * v), (target, v)) \
  X(void, glMultiTexCoord4sv, (GLenum target, const GLshort * v), (target, v)) \
  X(void, glMultMatrixf, (const GLfloat * m), (m)) \
  X(void, glNormal3f, (GLfloat nx, GLfloat ny, GLfloat nz), (nx, ny, nz)) \
  X(void, glNormal3fv, (const GLfloat * v), (v)) \
  X(void, glNormal3sv, (const GLshort * v), (v)) \
  X(void, glOrtho, (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glPointParameterf, (GLenum param, GLfloat value), (param, value)) \
  X(void, glPointParameterfv, (GLenum param, const GLfloat * values), (param, values)) \
  X(void, glPointSize, (GLfloat size), (size)) \
  X(void, glRotatef, (GLfloat angle, GLfloat x, GLfloat y, GLfloat z), (angle, x, y, z)) \
  X(void, glScalef, (GLfloat x, GLfloat y, GLfloat z), (x, y, z)) \
  X(void, glTexEnvf, (GLenum target, GLenum pname, GLfloat param), (target, pname, param)) \
  X(void, glTexEnvfv, (GLenum target, GLenum pname, const GLfloat * params), (target, pname, params)) \
  X(void, glMatrixMode, (GLenum mode), (mode)) \
  X(void, glNormalPointer, (GLenum type, GLsizei stride, const GLvoid * pointer), (type, stride, pointer)) \
  X(void, glPopMatrix, (), ()) \
  X(void, glPushMatrix, (), ()) \
  X(void, glShadeModel, (GLenum mode), (mode)) \
  X(void, glTexCoordPointer, (GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (size, type, stride, pointer)) \
  X(void, glTexEnvi, (GLenum target, GLenum pname, GLint param), (target, pname, param)) \
  X(void, glTexEnviv, (GLenum target, GLenum pname, const GLint * params), (target, pname, params)) \
  X(void, glTranslatef, (GLfloat x, GLfloat y, GLfloat z), (x, y, z)) \
  X(void, glVertexPointer, (GLint size, GLenum type, GLsizei stride, const GLvoid * pointer), (size, type, stride, pointer)) \
  X(void, glClipPlanef, (GLenum plane, const GLfloat * equation), (plane, equation)) \
  X(void, glFrustumf, (GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glGetClipPlanef, (GLenum pname, GLfloat eqn[4]), (pname, eqn[4])) \
  X(void, glOrthof, (GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glAlphaFuncx, (GLenum func, GLclampx ref), (func, ref)) \
  X(void, glClearColorx, (GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha), (red, green, blue, alpha)) \
  X(void, glClearDepthx, (GLclampx depth), (depth)) \
  X(void, glColor4x, (GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha), (red, green, blue, alpha)) \
  X(void, glDepthRangex, (GLclampx zNear, GLclampx zFar), (zNear, zFar)) \
  X(void, glFogx, (GLenum pname, GLfixed param), (pname, param)) \
  X(void, glFogxv, (GLenum pname, const GLfixed * params), (pname, params)) \
  X(void, glFrustumx, (GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glClipPlanex, (GLenum pname, const GLfixed * eqn), (pname, eqn)) \
  X(void, glGetFixedv, (GLenum pname, GLfixed * params), (pname, params)) \
  X(void, glGetLightxv, (GLenum light, GLenum pname, GLfixed * params), (light, pname, params)) \
  X(void, glGetMaterialxv, (GLenum face, GLenum pname, GLfixed * params), (face, pname, params)) \
  X(void, glGetTexEnvxv, (GLenum env, GLenum pname, GLfixed * params), (env, pname, params)) \
  X(void, glGetTexParameterxv, (GLenum target, GLenum pname, GLfixed * params), (target, pname, params)) \
  X(void, glLightModelx, (GLenum pname, GLfixed param), (pname, param)) \
  X(void, glLightModelxv, (GLenum pname, const GLfixed * params), (pname, params)) \
  X(void, glLightx, (GLenum light, GLenum pname, GLfixed param), (light, pname, param)) \
  X(void, glLightxv, (GLenum light, GLenum pname, const GLfixed * params), (light, pname, params)) \
  X(void, glLineWidthx, (GLfixed width), (width)) \
  X(void, glLoadMatrixx, (const GLfixed * m), (m)) \
  X(void, glMaterialx, (GLenum face, GLenum pname, GLfixed param), (face, pname, param)) \
  X(void, glMaterialxv, (GLenum face, GLenum pname, const GLfixed * params), (face, pname, params)) \
  X(void, glMultMatrixx, (const GLfixed * m), (m)) \
  X(void, glMultiTexCoord4x, (GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q), (target, s, t, r, q)) \
  X(void, glNormal3x, (GLfixed nx, GLfixed ny, GLfixed nz), (nx, ny, nz)) \
  X(void, glOrthox, (GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar), (left, right, bottom, top, zNear, zFar)) \
  X(void, glPointParameterx, (GLenum pname, GLfixed param), (pname, param)) \
  X(void, glPointParameterxv, (GLenum pname, const GLfixed * params), (pname, params)) \
  X(void, glPointSizex, (GLfixed size), (size)) \
  X(void, glPolygonOffsetx, (GLfixed factor, GLfixed units), (factor, units)) \
  X(void, glRotatex, (GLfixed angle, GLfixed x, GLfixed y, GLfixed z), (angle, x, y, z)) \
  X(void, glSampleCoveragex, (GLclampx value, GLboolean invert), (value, invert)) \
  X(void, glScalex, (GLfixed x, GLfixed y, GLfixed z), (x, y, z)) \
  X(void, glTexEnvx, (GLenum target, GLenum pname, GLfixed param), (target, pname, param)) \
  X(void, glTexEnvxv, (GLenum target, GLenum pname, const GLfixed * params), (target, pname, params)) \
  X(void, glTexParameterx, (GLenum target, GLenum pname, GLfixed param), (target, pname, param)) \
  X(void, glTexParameterxv, (GLenum target, GLenum pname, const GLfixed * params), (target, pname, params)) \
  X(void, glTranslatex, (GLfixed x, GLfixed y, GLfixed z), (x, y, z)) \
  X(void, glGetClipPlanex, (GLenum pname, GLfixed eqn[4]), (pname, eqn[4])) \


#endif  // GLES1_ONLY_FUNCTIONS_H
