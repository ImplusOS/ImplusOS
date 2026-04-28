#include "epoxy/gl.h"
#include "epoxy/egl.h"
#include <string.h>

extern void *calloc(unsigned long, unsigned long);

static GLuint next_id = 1;

void glClear(GLbitfield m) { (void)m; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { (void)r;(void)g;(void)b;(void)a; }
void glEnable(GLenum c) { (void)c; }
void glDisable(GLenum c) { (void)c; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x;(void)y;(void)w;(void)h; }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x;(void)y;(void)w;(void)h; }
void glBlendFunc(GLenum s, GLenum d) { (void)s;(void)d; }
GLenum glGetError(void) { return GL_NO_ERROR; }
const GLubyte *glGetString(GLenum n) {
    switch(n) {
        case GL_VENDOR:   return (const GLubyte*)"ImplusOS";
        case GL_RENDERER: return (const GLubyte*)"Software";
        case GL_VERSION:  return (const GLubyte*)"3.3";
        default:          return (const GLubyte*)"";
    }
}
void glFlush(void) {}
void glFinish(void) {}

void glGenTextures(GLsizei n, GLuint *t) { for(GLsizei i=0;i<n;i++) t[i]=next_id++; }
void glDeleteTextures(GLsizei n, const GLuint *t) { (void)n;(void)t; }
void glBindTexture(GLenum tgt, GLuint t) { (void)tgt;(void)t; }
void glTexImage2D(GLenum tgt, GLint lv, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum tp, const void *px) {
    (void)tgt;(void)lv;(void)ifmt;(void)w;(void)h;(void)b;(void)fmt;(void)tp;(void)px;
}
void glTexSubImage2D(GLenum tgt, GLint lv, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum tp, const void *px) {
    (void)tgt;(void)lv;(void)x;(void)y;(void)w;(void)h;(void)fmt;(void)tp;(void)px;
}
void glTexParameteri(GLenum tgt, GLenum pn, GLint p) { (void)tgt;(void)pn;(void)p; }
void glPixelStorei(GLenum pn, GLint p) { (void)pn;(void)p; }

GLuint glCreateShader(GLenum t) { (void)t; return next_id++; }
void glShaderSource(GLuint s, GLsizei c, const GLchar **str, const GLint *len) { (void)s;(void)c;(void)str;(void)len; }
void glCompileShader(GLuint s) { (void)s; }
void glGetShaderiv(GLuint s, GLenum pn, GLint *p) { (void)s; if(pn==GL_COMPILE_STATUS && p) *p=GL_TRUE; }
void glDeleteShader(GLuint s) { (void)s; }

GLuint glCreateProgram(void) { return next_id++; }
void glAttachShader(GLuint p, GLuint s) { (void)p;(void)s; }
void glLinkProgram(GLuint p) { (void)p; }
void glUseProgram(GLuint p) { (void)p; }
void glGetProgramiv(GLuint p, GLenum pn, GLint *par) { (void)p; if(pn==GL_LINK_STATUS && par) *par=GL_TRUE; }
void glDeleteProgram(GLuint p) { (void)p; }
GLint glGetUniformLocation(GLuint p, const GLchar *n) { (void)p;(void)n; return 0; }
GLint glGetAttribLocation(GLuint p, const GLchar *n) { (void)p;(void)n; return 0; }
void glUniform1i(GLint l, GLint v) { (void)l;(void)v; }
void glUniform1f(GLint l, GLfloat v) { (void)l;(void)v; }
void glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)l;(void)a;(void)b;(void)c;(void)d; }
void glUniformMatrix4fv(GLint l, GLsizei c, GLboolean t, const GLfloat *v) { (void)l;(void)c;(void)t;(void)v; }

void glGenBuffers(GLsizei n, GLuint *b) { for(GLsizei i=0;i<n;i++) b[i]=next_id++; }
void glDeleteBuffers(GLsizei n, const GLuint *b) { (void)n;(void)b; }
void glBindBuffer(GLenum tgt, GLuint b) { (void)tgt;(void)b; }
void glBufferData(GLenum tgt, GLsizeiptr sz, const void *d, GLenum u) { (void)tgt;(void)sz;(void)d;(void)u; }
void glGenVertexArrays(GLsizei n, GLuint *a) { for(GLsizei i=0;i<n;i++) a[i]=next_id++; }
void glDeleteVertexArrays(GLsizei n, const GLuint *a) { (void)n;(void)a; }
void glBindVertexArray(GLuint a) { (void)a; }
void glEnableVertexAttribArray(GLuint i) { (void)i; }
void glVertexAttribPointer(GLuint i, GLint sz, GLenum tp, GLboolean nr, GLsizei st, const void *p) { (void)i;(void)sz;(void)tp;(void)nr;(void)st;(void)p; }
void glDrawArrays(GLenum m, GLint f, GLsizei c) { (void)m;(void)f;(void)c; }
void glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) { (void)m;(void)c;(void)t;(void)i; }

void glGenFramebuffers(GLsizei n, GLuint *ids) { for(GLsizei i=0;i<n;i++) ids[i]=next_id++; }
void glDeleteFramebuffers(GLsizei n, const GLuint *ids) { (void)n;(void)ids; }
void glBindFramebuffer(GLenum tgt, GLuint fb) { (void)tgt;(void)fb; }
void glFramebufferTexture2D(GLenum tgt, GLenum att, GLenum ttgt, GLuint tex, GLint lv) { (void)tgt;(void)att;(void)ttgt;(void)tex;(void)lv; }
GLenum glCheckFramebufferStatus(GLenum tgt) { (void)tgt; return GL_FRAMEBUFFER_COMPLETE; }

int epoxy_gl_version(void) { return 33; }
int epoxy_is_desktop_gl(void) { return 1; }
int epoxy_has_gl_extension(const char *ext) { (void)ext; return 0; }
int epoxy_egl_version(void *dpy) { (void)dpy; return 14; }
int epoxy_has_egl_extension(void *dpy, const char *ext) { (void)dpy;(void)ext; return 0; }
