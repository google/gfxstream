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

// Generated Code - DO NOT EDIT !!
// generated by './scripts/generate-apigen-sources.sh'
#ifndef __gles1_server_context_t_h
#define __gles1_server_context_t_h

#include "gles1_server_proc.h"

#include "gles1_types.h"


struct gles1_server_context_t {

	glAlphaFunc_server_proc_t glAlphaFunc;
	glClearColor_server_proc_t glClearColor;
	glClearDepthf_server_proc_t glClearDepthf;
	glClipPlanef_server_proc_t glClipPlanef;
	glColor4f_server_proc_t glColor4f;
	glDepthRangef_server_proc_t glDepthRangef;
	glFogf_server_proc_t glFogf;
	glFogfv_server_proc_t glFogfv;
	glFrustumf_server_proc_t glFrustumf;
	glGetClipPlanef_server_proc_t glGetClipPlanef;
	glGetFloatv_server_proc_t glGetFloatv;
	glGetLightfv_server_proc_t glGetLightfv;
	glGetMaterialfv_server_proc_t glGetMaterialfv;
	glGetTexEnvfv_server_proc_t glGetTexEnvfv;
	glGetTexParameterfv_server_proc_t glGetTexParameterfv;
	glLightModelf_server_proc_t glLightModelf;
	glLightModelfv_server_proc_t glLightModelfv;
	glLightf_server_proc_t glLightf;
	glLightfv_server_proc_t glLightfv;
	glLineWidth_server_proc_t glLineWidth;
	glLoadMatrixf_server_proc_t glLoadMatrixf;
	glMaterialf_server_proc_t glMaterialf;
	glMaterialfv_server_proc_t glMaterialfv;
	glMultMatrixf_server_proc_t glMultMatrixf;
	glMultiTexCoord4f_server_proc_t glMultiTexCoord4f;
	glNormal3f_server_proc_t glNormal3f;
	glOrthof_server_proc_t glOrthof;
	glPointParameterf_server_proc_t glPointParameterf;
	glPointParameterfv_server_proc_t glPointParameterfv;
	glPointSize_server_proc_t glPointSize;
	glPolygonOffset_server_proc_t glPolygonOffset;
	glRotatef_server_proc_t glRotatef;
	glScalef_server_proc_t glScalef;
	glTexEnvf_server_proc_t glTexEnvf;
	glTexEnvfv_server_proc_t glTexEnvfv;
	glTexParameterf_server_proc_t glTexParameterf;
	glTexParameterfv_server_proc_t glTexParameterfv;
	glTranslatef_server_proc_t glTranslatef;
	glActiveTexture_server_proc_t glActiveTexture;
	glAlphaFuncx_server_proc_t glAlphaFuncx;
	glBindBuffer_server_proc_t glBindBuffer;
	glBindTexture_server_proc_t glBindTexture;
	glBlendFunc_server_proc_t glBlendFunc;
	glBufferData_server_proc_t glBufferData;
	glBufferSubData_server_proc_t glBufferSubData;
	glClear_server_proc_t glClear;
	glClearColorx_server_proc_t glClearColorx;
	glClearDepthx_server_proc_t glClearDepthx;
	glClearStencil_server_proc_t glClearStencil;
	glClientActiveTexture_server_proc_t glClientActiveTexture;
	glColor4ub_server_proc_t glColor4ub;
	glColor4x_server_proc_t glColor4x;
	glColorMask_server_proc_t glColorMask;
	glColorPointer_server_proc_t glColorPointer;
	glCompressedTexImage2D_server_proc_t glCompressedTexImage2D;
	glCompressedTexSubImage2D_server_proc_t glCompressedTexSubImage2D;
	glCopyTexImage2D_server_proc_t glCopyTexImage2D;
	glCopyTexSubImage2D_server_proc_t glCopyTexSubImage2D;
	glCullFace_server_proc_t glCullFace;
	glDeleteBuffers_dec_server_proc_t glDeleteBuffers;
	glDeleteBuffers_server_proc_t glDeleteBuffers_dec;
	glDeleteTextures_dec_server_proc_t glDeleteTextures;
	glDeleteTextures_server_proc_t glDeleteTextures_dec;
	glDepthFunc_server_proc_t glDepthFunc;
	glDepthMask_server_proc_t glDepthMask;
	glDepthRangex_server_proc_t glDepthRangex;
	glDisable_server_proc_t glDisable;
	glDisableClientState_server_proc_t glDisableClientState;
	glDrawArrays_server_proc_t glDrawArrays;
	glDrawElements_server_proc_t glDrawElements;
	glEnable_server_proc_t glEnable;
	glEnableClientState_server_proc_t glEnableClientState;
	glFinish_server_proc_t glFinish;
	glFlush_server_proc_t glFlush;
	glFogx_server_proc_t glFogx;
	glFogxv_server_proc_t glFogxv;
	glFrontFace_server_proc_t glFrontFace;
	glFrustumx_server_proc_t glFrustumx;
	glGetBooleanv_server_proc_t glGetBooleanv;
	glGetBufferParameteriv_server_proc_t glGetBufferParameteriv;
	glClipPlanex_server_proc_t glClipPlanex;
	glGenBuffers_dec_server_proc_t glGenBuffers;
	glGenBuffers_server_proc_t glGenBuffers_dec;
	glGenTextures_dec_server_proc_t glGenTextures;
	glGenTextures_server_proc_t glGenTextures_dec;
	glGetError_server_proc_t glGetError;
	glGetFixedv_server_proc_t glGetFixedv;
	glGetIntegerv_server_proc_t glGetIntegerv;
	glGetLightxv_server_proc_t glGetLightxv;
	glGetMaterialxv_server_proc_t glGetMaterialxv;
	glGetPointerv_server_proc_t glGetPointerv;
	glGetString_server_proc_t glGetString;
	glGetTexEnviv_server_proc_t glGetTexEnviv;
	glGetTexEnvxv_server_proc_t glGetTexEnvxv;
	glGetTexParameteriv_server_proc_t glGetTexParameteriv;
	glGetTexParameterxv_server_proc_t glGetTexParameterxv;
	glHint_server_proc_t glHint;
	glIsBuffer_server_proc_t glIsBuffer;
	glIsEnabled_server_proc_t glIsEnabled;
	glIsTexture_server_proc_t glIsTexture;
	glLightModelx_server_proc_t glLightModelx;
	glLightModelxv_server_proc_t glLightModelxv;
	glLightx_server_proc_t glLightx;
	glLightxv_server_proc_t glLightxv;
	glLineWidthx_server_proc_t glLineWidthx;
	glLoadIdentity_server_proc_t glLoadIdentity;
	glLoadMatrixx_server_proc_t glLoadMatrixx;
	glLogicOp_server_proc_t glLogicOp;
	glMaterialx_server_proc_t glMaterialx;
	glMaterialxv_server_proc_t glMaterialxv;
	glMatrixMode_server_proc_t glMatrixMode;
	glMultMatrixx_server_proc_t glMultMatrixx;
	glMultiTexCoord4x_server_proc_t glMultiTexCoord4x;
	glNormal3x_server_proc_t glNormal3x;
	glNormalPointer_server_proc_t glNormalPointer;
	glOrthox_server_proc_t glOrthox;
	glPixelStorei_server_proc_t glPixelStorei;
	glPointParameterx_server_proc_t glPointParameterx;
	glPointParameterxv_server_proc_t glPointParameterxv;
	glPointSizex_server_proc_t glPointSizex;
	glPolygonOffsetx_server_proc_t glPolygonOffsetx;
	glPopMatrix_server_proc_t glPopMatrix;
	glPushMatrix_server_proc_t glPushMatrix;
	glReadPixels_server_proc_t glReadPixels;
	glRotatex_server_proc_t glRotatex;
	glSampleCoverage_server_proc_t glSampleCoverage;
	glSampleCoveragex_server_proc_t glSampleCoveragex;
	glScalex_server_proc_t glScalex;
	glScissor_server_proc_t glScissor;
	glShadeModel_server_proc_t glShadeModel;
	glStencilFunc_server_proc_t glStencilFunc;
	glStencilMask_server_proc_t glStencilMask;
	glStencilOp_server_proc_t glStencilOp;
	glTexCoordPointer_server_proc_t glTexCoordPointer;
	glTexEnvi_server_proc_t glTexEnvi;
	glTexEnvx_server_proc_t glTexEnvx;
	glTexEnviv_server_proc_t glTexEnviv;
	glTexEnvxv_server_proc_t glTexEnvxv;
	glTexImage2D_server_proc_t glTexImage2D;
	glTexParameteri_server_proc_t glTexParameteri;
	glTexParameterx_server_proc_t glTexParameterx;
	glTexParameteriv_server_proc_t glTexParameteriv;
	glTexParameterxv_server_proc_t glTexParameterxv;
	glTexSubImage2D_server_proc_t glTexSubImage2D;
	glTranslatex_server_proc_t glTranslatex;
	glVertexPointer_server_proc_t glVertexPointer;
	glViewport_server_proc_t glViewport;
	glPointSizePointerOES_server_proc_t glPointSizePointerOES;
	glVertexPointerOffset_server_proc_t glVertexPointerOffset;
	glColorPointerOffset_server_proc_t glColorPointerOffset;
	glNormalPointerOffset_server_proc_t glNormalPointerOffset;
	glPointSizePointerOffset_server_proc_t glPointSizePointerOffset;
	glTexCoordPointerOffset_server_proc_t glTexCoordPointerOffset;
	glWeightPointerOffset_server_proc_t glWeightPointerOffset;
	glMatrixIndexPointerOffset_server_proc_t glMatrixIndexPointerOffset;
	glVertexPointerData_server_proc_t glVertexPointerData;
	glColorPointerData_server_proc_t glColorPointerData;
	glNormalPointerData_server_proc_t glNormalPointerData;
	glTexCoordPointerData_server_proc_t glTexCoordPointerData;
	glPointSizePointerData_server_proc_t glPointSizePointerData;
	glWeightPointerData_server_proc_t glWeightPointerData;
	glMatrixIndexPointerData_server_proc_t glMatrixIndexPointerData;
	glDrawElementsOffset_server_proc_t glDrawElementsOffset;
	glDrawElementsData_server_proc_t glDrawElementsData;
	glGetCompressedTextureFormats_server_proc_t glGetCompressedTextureFormats;
	glFinishRoundTrip_server_proc_t glFinishRoundTrip;
	glBlendEquationSeparateOES_server_proc_t glBlendEquationSeparateOES;
	glBlendFuncSeparateOES_server_proc_t glBlendFuncSeparateOES;
	glBlendEquationOES_server_proc_t glBlendEquationOES;
	glDrawTexsOES_server_proc_t glDrawTexsOES;
	glDrawTexiOES_server_proc_t glDrawTexiOES;
	glDrawTexxOES_server_proc_t glDrawTexxOES;
	glDrawTexsvOES_server_proc_t glDrawTexsvOES;
	glDrawTexivOES_server_proc_t glDrawTexivOES;
	glDrawTexxvOES_server_proc_t glDrawTexxvOES;
	glDrawTexfOES_server_proc_t glDrawTexfOES;
	glDrawTexfvOES_server_proc_t glDrawTexfvOES;
	glEGLImageTargetTexture2DOES_server_proc_t glEGLImageTargetTexture2DOES;
	glEGLImageTargetRenderbufferStorageOES_server_proc_t glEGLImageTargetRenderbufferStorageOES;
	glAlphaFuncxOES_server_proc_t glAlphaFuncxOES;
	glClearColorxOES_server_proc_t glClearColorxOES;
	glClearDepthxOES_server_proc_t glClearDepthxOES;
	glClipPlanexOES_server_proc_t glClipPlanexOES;
	glClipPlanexIMG_server_proc_t glClipPlanexIMG;
	glColor4xOES_server_proc_t glColor4xOES;
	glDepthRangexOES_server_proc_t glDepthRangexOES;
	glFogxOES_server_proc_t glFogxOES;
	glFogxvOES_server_proc_t glFogxvOES;
	glFrustumxOES_server_proc_t glFrustumxOES;
	glGetClipPlanexOES_server_proc_t glGetClipPlanexOES;
	glGetClipPlanex_server_proc_t glGetClipPlanex;
	glGetFixedvOES_server_proc_t glGetFixedvOES;
	glGetLightxvOES_server_proc_t glGetLightxvOES;
	glGetMaterialxvOES_server_proc_t glGetMaterialxvOES;
	glGetTexEnvxvOES_server_proc_t glGetTexEnvxvOES;
	glGetTexParameterxvOES_server_proc_t glGetTexParameterxvOES;
	glLightModelxOES_server_proc_t glLightModelxOES;
	glLightModelxvOES_server_proc_t glLightModelxvOES;
	glLightxOES_server_proc_t glLightxOES;
	glLightxvOES_server_proc_t glLightxvOES;
	glLineWidthxOES_server_proc_t glLineWidthxOES;
	glLoadMatrixxOES_server_proc_t glLoadMatrixxOES;
	glMaterialxOES_server_proc_t glMaterialxOES;
	glMaterialxvOES_server_proc_t glMaterialxvOES;
	glMultMatrixxOES_server_proc_t glMultMatrixxOES;
	glMultiTexCoord4xOES_server_proc_t glMultiTexCoord4xOES;
	glNormal3xOES_server_proc_t glNormal3xOES;
	glOrthoxOES_server_proc_t glOrthoxOES;
	glPointParameterxOES_server_proc_t glPointParameterxOES;
	glPointParameterxvOES_server_proc_t glPointParameterxvOES;
	glPointSizexOES_server_proc_t glPointSizexOES;
	glPolygonOffsetxOES_server_proc_t glPolygonOffsetxOES;
	glRotatexOES_server_proc_t glRotatexOES;
	glSampleCoveragexOES_server_proc_t glSampleCoveragexOES;
	glScalexOES_server_proc_t glScalexOES;
	glTexEnvxOES_server_proc_t glTexEnvxOES;
	glTexEnvxvOES_server_proc_t glTexEnvxvOES;
	glTexParameterxOES_server_proc_t glTexParameterxOES;
	glTexParameterxvOES_server_proc_t glTexParameterxvOES;
	glTranslatexOES_server_proc_t glTranslatexOES;
	glIsRenderbufferOES_server_proc_t glIsRenderbufferOES;
	glBindRenderbufferOES_server_proc_t glBindRenderbufferOES;
	glDeleteRenderbuffersOES_dec_server_proc_t glDeleteRenderbuffersOES;
	glDeleteRenderbuffersOES_server_proc_t glDeleteRenderbuffersOES_dec;
	glGenRenderbuffersOES_dec_server_proc_t glGenRenderbuffersOES;
	glGenRenderbuffersOES_server_proc_t glGenRenderbuffersOES_dec;
	glRenderbufferStorageOES_server_proc_t glRenderbufferStorageOES;
	glGetRenderbufferParameterivOES_server_proc_t glGetRenderbufferParameterivOES;
	glIsFramebufferOES_server_proc_t glIsFramebufferOES;
	glBindFramebufferOES_server_proc_t glBindFramebufferOES;
	glDeleteFramebuffersOES_dec_server_proc_t glDeleteFramebuffersOES;
	glDeleteFramebuffersOES_server_proc_t glDeleteFramebuffersOES_dec;
	glGenFramebuffersOES_dec_server_proc_t glGenFramebuffersOES;
	glGenFramebuffersOES_server_proc_t glGenFramebuffersOES_dec;
	glCheckFramebufferStatusOES_server_proc_t glCheckFramebufferStatusOES;
	glFramebufferRenderbufferOES_server_proc_t glFramebufferRenderbufferOES;
	glFramebufferTexture2DOES_server_proc_t glFramebufferTexture2DOES;
	glGetFramebufferAttachmentParameterivOES_server_proc_t glGetFramebufferAttachmentParameterivOES;
	glGenerateMipmapOES_server_proc_t glGenerateMipmapOES;
	glMapBufferOES_server_proc_t glMapBufferOES;
	glUnmapBufferOES_server_proc_t glUnmapBufferOES;
	glGetBufferPointervOES_server_proc_t glGetBufferPointervOES;
	glCurrentPaletteMatrixOES_server_proc_t glCurrentPaletteMatrixOES;
	glLoadPaletteFromModelViewMatrixOES_server_proc_t glLoadPaletteFromModelViewMatrixOES;
	glMatrixIndexPointerOES_server_proc_t glMatrixIndexPointerOES;
	glWeightPointerOES_server_proc_t glWeightPointerOES;
	glQueryMatrixxOES_server_proc_t glQueryMatrixxOES;
	glDepthRangefOES_server_proc_t glDepthRangefOES;
	glFrustumfOES_server_proc_t glFrustumfOES;
	glOrthofOES_server_proc_t glOrthofOES;
	glClipPlanefOES_server_proc_t glClipPlanefOES;
	glClipPlanefIMG_server_proc_t glClipPlanefIMG;
	glGetClipPlanefOES_server_proc_t glGetClipPlanefOES;
	glClearDepthfOES_server_proc_t glClearDepthfOES;
	glTexGenfOES_server_proc_t glTexGenfOES;
	glTexGenfvOES_server_proc_t glTexGenfvOES;
	glTexGeniOES_server_proc_t glTexGeniOES;
	glTexGenivOES_server_proc_t glTexGenivOES;
	glTexGenxOES_server_proc_t glTexGenxOES;
	glTexGenxvOES_server_proc_t glTexGenxvOES;
	glGetTexGenfvOES_server_proc_t glGetTexGenfvOES;
	glGetTexGenivOES_server_proc_t glGetTexGenivOES;
	glGetTexGenxvOES_server_proc_t glGetTexGenxvOES;
	glBindVertexArrayOES_server_proc_t glBindVertexArrayOES;
	glDeleteVertexArraysOES_dec_server_proc_t glDeleteVertexArraysOES;
	glDeleteVertexArraysOES_server_proc_t glDeleteVertexArraysOES_dec;
	glGenVertexArraysOES_dec_server_proc_t glGenVertexArraysOES;
	glGenVertexArraysOES_server_proc_t glGenVertexArraysOES_dec;
	glIsVertexArrayOES_server_proc_t glIsVertexArrayOES;
	glDiscardFramebufferEXT_server_proc_t glDiscardFramebufferEXT;
	glMultiDrawArraysEXT_server_proc_t glMultiDrawArraysEXT;
	glMultiDrawElementsEXT_server_proc_t glMultiDrawElementsEXT;
	glMultiDrawArraysSUN_server_proc_t glMultiDrawArraysSUN;
	glMultiDrawElementsSUN_server_proc_t glMultiDrawElementsSUN;
	glRenderbufferStorageMultisampleIMG_server_proc_t glRenderbufferStorageMultisampleIMG;
	glFramebufferTexture2DMultisampleIMG_server_proc_t glFramebufferTexture2DMultisampleIMG;
	glDeleteFencesNV_server_proc_t glDeleteFencesNV;
	glGenFencesNV_server_proc_t glGenFencesNV;
	glIsFenceNV_server_proc_t glIsFenceNV;
	glTestFenceNV_server_proc_t glTestFenceNV;
	glGetFenceivNV_server_proc_t glGetFenceivNV;
	glFinishFenceNV_server_proc_t glFinishFenceNV;
	glSetFenceNV_server_proc_t glSetFenceNV;
	glGetDriverControlsQCOM_server_proc_t glGetDriverControlsQCOM;
	glGetDriverControlStringQCOM_server_proc_t glGetDriverControlStringQCOM;
	glEnableDriverControlQCOM_server_proc_t glEnableDriverControlQCOM;
	glDisableDriverControlQCOM_server_proc_t glDisableDriverControlQCOM;
	glExtGetTexturesQCOM_server_proc_t glExtGetTexturesQCOM;
	glExtGetBuffersQCOM_server_proc_t glExtGetBuffersQCOM;
	glExtGetRenderbuffersQCOM_server_proc_t glExtGetRenderbuffersQCOM;
	glExtGetFramebuffersQCOM_server_proc_t glExtGetFramebuffersQCOM;
	glExtGetTexLevelParameterivQCOM_server_proc_t glExtGetTexLevelParameterivQCOM;
	glExtTexObjectStateOverrideiQCOM_server_proc_t glExtTexObjectStateOverrideiQCOM;
	glExtGetTexSubImageQCOM_server_proc_t glExtGetTexSubImageQCOM;
	glExtGetBufferPointervQCOM_server_proc_t glExtGetBufferPointervQCOM;
	glExtGetShadersQCOM_server_proc_t glExtGetShadersQCOM;
	glExtGetProgramsQCOM_server_proc_t glExtGetProgramsQCOM;
	glExtIsProgramBinaryQCOM_server_proc_t glExtIsProgramBinaryQCOM;
	glExtGetProgramBinarySourceQCOM_server_proc_t glExtGetProgramBinarySourceQCOM;
	glStartTilingQCOM_server_proc_t glStartTilingQCOM;
	glEndTilingQCOM_server_proc_t glEndTilingQCOM;
	glGetGraphicsResetStatusEXT_server_proc_t glGetGraphicsResetStatusEXT;
	glReadnPixelsEXT_server_proc_t glReadnPixelsEXT;
	virtual ~gles1_server_context_t() {}
	int initDispatchByName( void *(*getProc)(const char *name, void *userData), void *userData);
};

#endif
