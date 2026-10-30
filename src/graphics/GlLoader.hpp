#pragma once

// Windows only: opengl32.dll exports OpenGL 1.1, so every newer function the renderer uses is
// loaded through SDL once a context exists (LoadGlFunctions). The macros at the bottom point the
// gl* names used in Gles.cpp at the loaded pointers.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL3/SDL_opengl.h>

#define TH_GL_FUNCTIONS(X)                                                                                             \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                                         \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                                           \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                                               \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                                     \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                                                   \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                                     \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                                               \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                                                         \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                                         \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                                         \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                                           \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                                           \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                                                   \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                                     \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                                     \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                                           \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                                               \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                                     \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                                                   \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                                     \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                                   \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                                             \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                                               \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                                             \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                                             \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                                           \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                                                                 \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                                                 \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                                                 \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                                                   \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                                               \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f)                                                                       \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define TH_GL_DECLARE(type, name) extern type th_##name;
TH_GL_FUNCTIONS(TH_GL_DECLARE)
#undef TH_GL_DECLARE

bool LoadGlFunctions();

#define glActiveTexture th_glActiveTexture
#define glAttachShader th_glAttachShader
#define glBindBuffer th_glBindBuffer
#define glBindFramebuffer th_glBindFramebuffer
#define glBindRenderbuffer th_glBindRenderbuffer
#define glBindVertexArray th_glBindVertexArray
#define glBufferData th_glBufferData
#define glBufferSubData th_glBufferSubData
#define glCompileShader th_glCompileShader
#define glCreateProgram th_glCreateProgram
#define glCreateShader th_glCreateShader
#define glDeleteShader th_glDeleteShader
#define glDisableVertexAttribArray th_glDisableVertexAttribArray
#define glEnableVertexAttribArray th_glEnableVertexAttribArray
#define glFramebufferRenderbuffer th_glFramebufferRenderbuffer
#define glFramebufferTexture2D th_glFramebufferTexture2D
#define glGenBuffers th_glGenBuffers
#define glGenFramebuffers th_glGenFramebuffers
#define glGenRenderbuffers th_glGenRenderbuffers
#define glGenVertexArrays th_glGenVertexArrays
#define glGetShaderInfoLog th_glGetShaderInfoLog
#define glGetShaderiv th_glGetShaderiv
#define glGetUniformLocation th_glGetUniformLocation
#define glLinkProgram th_glLinkProgram
#define glRenderbufferStorage th_glRenderbufferStorage
#define glShaderSource th_glShaderSource
#define glUniform1f th_glUniform1f
#define glUniform1i th_glUniform1i
#define glUniform4f th_glUniform4f
#define glUniformMatrix4fv th_glUniformMatrix4fv
#define glUseProgram th_glUseProgram
#define glVertexAttrib4f th_glVertexAttrib4f
#define glVertexAttribPointer th_glVertexAttribPointer
