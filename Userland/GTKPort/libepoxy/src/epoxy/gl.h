#pragma once

#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int          GLint;
typedef int          GLsizei;
typedef float        GLfloat;
typedef double       GLdouble;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef void         GLvoid;
typedef uint32_t     GLbitfield;
typedef int8_t       GLbyte;
typedef uint16_t     GLushort;
typedef int16_t      GLshort;
typedef float        GLclampf;
typedef double       GLclampd;
typedef intptr_t     GLsizeiptr;
typedef intptr_t     GLintptr;
typedef char         GLchar;

#define GL_FALSE 0
#define GL_TRUE  1
#define GL_NO_ERROR 0
#define GL_ZERO   0
#define GL_ONE    1

#define GL_TRIANGLES    0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_QUADS        0x0007
#define GL_LINES        0x0001
#define GL_LINE_STRIP   0x0003
#define GL_POINTS       0x0000

#define GL_TEXTURE_2D       0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S   0x2802
#define GL_TEXTURE_WRAP_T   0x2803
#define GL_LINEAR           0x2601
#define GL_NEAREST          0x2600
#define GL_CLAMP_TO_EDGE    0x812F
#define GL_REPEAT           0x2901

#define GL_RGB              0x1907
#define GL_RGBA             0x1908
#define GL_UNSIGNED_BYTE    0x1401
#define GL_FLOAT            0x1406

#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400

#define GL_BLEND            0x0BE2
#define GL_DEPTH_TEST       0x0B71
#define GL_SCISSOR_TEST     0x0C11
#define GL_STENCIL_TEST     0x0B90

#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

#define GL_ARRAY_BUFFER    0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW     0x88E4
#define GL_DYNAMIC_DRAW    0x88E8

#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82

#define GL_FRAMEBUFFER     0x8D40
#define GL_RENDERBUFFER    0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03

void glClear(GLbitfield mask);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
GLenum glGetError(void);
const GLubyte *glGetString(GLenum name);
void glFlush(void);
void glFinish(void);

void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glPixelStorei(GLenum pname, GLint param);

GLuint glCreateShader(GLenum type);
void   glShaderSource(GLuint shader, GLsizei count, const GLchar **string, const GLint *length);
void   glCompileShader(GLuint shader);
void   glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
void   glDeleteShader(GLuint shader);

GLuint glCreateProgram(void);
void   glAttachShader(GLuint program, GLuint shader);
void   glLinkProgram(GLuint program);
void   glUseProgram(GLuint program);
void   glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void   glDeleteProgram(GLuint program);
GLint  glGetUniformLocation(GLuint program, const GLchar *name);
GLint  glGetAttribLocation(GLuint program, const GLchar *name);
void   glUniform1i(GLint location, GLint v0);
void   glUniform1f(GLint location, GLfloat v0);
void   glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void   glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glGenVertexArrays(GLsizei n, GLuint *arrays);
void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
void glBindVertexArray(GLuint array);
void glEnableVertexAttribArray(GLuint index);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);

void glGenFramebuffers(GLsizei n, GLuint *ids);
void glDeleteFramebuffers(GLsizei n, const GLuint *ids);
void glBindFramebuffer(GLenum target, GLuint fb);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
GLenum glCheckFramebufferStatus(GLenum target);

int epoxy_gl_version(void);
int epoxy_is_desktop_gl(void);
int epoxy_has_gl_extension(const char *extension);
