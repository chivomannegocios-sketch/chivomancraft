// Cargador minimo de OpenGL 3.3 (sin dependencias extra): todo se pide a GLFW.
#pragma once
#ifdef ANDROID_GAME
#include "android_glfw.h"
#else
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define MI_GLAPI __stdcall
#else
#define MI_GLAPI
#endif

typedef unsigned int GLenum, GLbitfield, GLuint;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr, GLintptr;

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_MULTISAMPLE 0x809D
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RG 0x8227
#define GL_RG8 0x822B
#define GL_RGBA8 0x8058
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_NEAREST 0x2600
#define GL_RGBA 0x1908
#define GL_RGBA16F 0x881A
#define GL_HALF_FLOAT 0x140B
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_NONE 0
#define GL_ONE 1
#define GL_POLYGON_OFFSET_FILL 0x8037

#define GL_FUNCS(X) \
    X(void, glClear, (GLbitfield)) \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glEnable, (GLenum)) \
    X(void, glDisable, (GLenum)) \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glBlendFunc, (GLenum, GLenum)) \
    X(void, glDepthMask, (GLboolean)) \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, glCullFace, (GLenum)) \
    X(void, glFrontFace, (GLenum)) \
    X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void*)) \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
    X(const GLubyte*, glGetString, (GLenum)) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, glCompileShader, (GLuint)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glLinkProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, glDeleteShader, (GLuint)) \
    X(void, glUseProgram, (GLuint)) \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar*)) \
    X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, glUniform1f, (GLint, GLfloat)) \
    X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, glUniform1i, (GLint, GLint)) \
    X(void, glGenTextures, (GLsizei, GLuint*)) \
    X(void, glDeleteTextures, (GLsizei, const GLuint*)) \
    X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
    X(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glActiveTexture, (GLenum)) \
    X(void, glPixelStorei, (GLenum, GLint)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint*)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glDeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, glGenBuffers, (GLsizei, GLuint*)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, glDeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, glGenQueries, (GLsizei, GLuint*)) \
    X(void, glBeginQuery, (GLenum, GLuint)) \
    X(void, glEndQuery, (GLenum)) \
    X(void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint*)) \
    X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint*)) \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void, glDrawBuffer, (GLenum)) \
    X(void, glReadBuffer, (GLenum)) \
    X(void, glPolygonOffset, (GLfloat, GLfloat))

#define X(ret, name, args) static ret (MI_GLAPI *name) args = nullptr;
GL_FUNCS(X)
#undef X

static bool cargarGL() {
    bool ok = true;
#define X(ret, name, args) \
    name = (ret (MI_GLAPI *) args) glfwGetProcAddress(#name); \
    if (!name && std::strcmp(#name, "glDrawBuffer") != 0 && std::strcmp(#name, "glReadBuffer") != 0) { std::fprintf(stderr, "Falta la funcion OpenGL %s\n", #name); ok = false; }
    GL_FUNCS(X)
#undef X
    return ok;
}
