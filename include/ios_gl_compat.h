#ifndef LAB3D_IOS_GL_COMPAT_H
#define LAB3D_IOS_GL_COMPAT_H

#ifndef GLdouble
typedef double GLdouble;
#endif
#ifndef GLAPI
#define GLAPI
#endif

#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_POLYGON
#define GL_POLYGON 0x0009
#endif
#ifndef GL_CLAMP
#define GL_CLAMP GL_CLAMP_TO_EDGE
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 GL_RGBA
#endif
#ifndef GL_RGBA4
#define GL_RGBA4 GL_RGBA
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#endif
#ifndef GL_PIXEL_MAP_I_TO_R
#define GL_PIXEL_MAP_I_TO_R 0x0C72
#endif
#ifndef GL_PIXEL_MAP_I_TO_G
#define GL_PIXEL_MAP_I_TO_G 0x0C73
#endif
#ifndef GL_PIXEL_MAP_I_TO_B
#define GL_PIXEL_MAP_I_TO_B 0x0C74
#endif
#ifndef GL_PIXEL_MAP_I_TO_A
#define GL_PIXEL_MAP_I_TO_A 0x0C75
#endif

typedef void (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (*PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
typedef void (*PFNGLDRAWBUFFERSPROC)(GLsizei n, const GLenum *bufs);
typedef void (*PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (*PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void (*PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (*PFNGLFRAMEBUFFERTEXTUREPROC)(GLenum target, GLenum attachment, GLuint texture, GLint level);
typedef void (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);

#define GLU_VERSION 100800
#define GLU_EXTENSIONS 100801

static inline const GLubyte *gluErrorString(GLenum error)
{
    (void)error;
    return (const GLubyte *)"OpenGL ES error";
}

static inline const GLubyte *gluGetString(GLenum name)
{
    (void)name;
    return (const GLubyte *)"OpenGL ES compatibility";
}

enum { LAB3D_IOS_MAX_IMMEDIATE_VERTICES = 8192 };

static GLenum lab3d_ios_gl_mode;
static GLsizei lab3d_ios_gl_vertex_count;
static GLfloat lab3d_ios_gl_vertices[LAB3D_IOS_MAX_IMMEDIATE_VERTICES][3];
static GLfloat lab3d_ios_gl_texcoords[LAB3D_IOS_MAX_IMMEDIATE_VERTICES][2];
static GLfloat lab3d_ios_gl_colors[LAB3D_IOS_MAX_IMMEDIATE_VERTICES][4];
static GLfloat lab3d_ios_gl_current_texcoord[2] = { 0.0f, 0.0f };
static GLfloat lab3d_ios_gl_current_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

static inline void lab3d_ios_glBegin(GLenum mode)
{
    lab3d_ios_gl_mode = mode;
    lab3d_ios_gl_vertex_count = 0;
}

static inline void lab3d_ios_glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    lab3d_ios_gl_current_color[0] = red;
    lab3d_ios_gl_current_color[1] = green;
    lab3d_ios_gl_current_color[2] = blue;
    lab3d_ios_gl_current_color[3] = 1.0f;
}

static inline void lab3d_ios_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    lab3d_ios_gl_current_color[0] = red;
    lab3d_ios_gl_current_color[1] = green;
    lab3d_ios_gl_current_color[2] = blue;
    lab3d_ios_gl_current_color[3] = alpha;
}

static inline void lab3d_ios_glTexCoord2f(GLfloat s, GLfloat t)
{
    lab3d_ios_gl_current_texcoord[0] = s;
    lab3d_ios_gl_current_texcoord[1] = t;
}

static inline void lab3d_ios_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (lab3d_ios_gl_vertex_count >= LAB3D_IOS_MAX_IMMEDIATE_VERTICES) {
        return;
    }
    lab3d_ios_gl_vertices[lab3d_ios_gl_vertex_count][0] = x;
    lab3d_ios_gl_vertices[lab3d_ios_gl_vertex_count][1] = y;
    lab3d_ios_gl_vertices[lab3d_ios_gl_vertex_count][2] = z;
    lab3d_ios_gl_texcoords[lab3d_ios_gl_vertex_count][0] = lab3d_ios_gl_current_texcoord[0];
    lab3d_ios_gl_texcoords[lab3d_ios_gl_vertex_count][1] = lab3d_ios_gl_current_texcoord[1];
    lab3d_ios_gl_colors[lab3d_ios_gl_vertex_count][0] = lab3d_ios_gl_current_color[0];
    lab3d_ios_gl_colors[lab3d_ios_gl_vertex_count][1] = lab3d_ios_gl_current_color[1];
    lab3d_ios_gl_colors[lab3d_ios_gl_vertex_count][2] = lab3d_ios_gl_current_color[2];
    lab3d_ios_gl_colors[lab3d_ios_gl_vertex_count][3] = lab3d_ios_gl_current_color[3];
    lab3d_ios_gl_vertex_count++;
}

static inline void lab3d_ios_glVertex2s(GLshort x, GLshort y)
{
    lab3d_ios_glVertex3f((GLfloat)x, (GLfloat)y, 0.0f);
}

static inline void lab3d_ios_glVertex3i(GLint x, GLint y, GLint z)
{
    lab3d_ios_glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

static inline void lab3d_ios_glEnd(void)
{
    GLsizei i;

    if (lab3d_ios_gl_vertex_count <= 0) {
        return;
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, lab3d_ios_gl_vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, lab3d_ios_gl_texcoords);
    glColorPointer(4, GL_FLOAT, 0, lab3d_ios_gl_colors);

    if (lab3d_ios_gl_mode == GL_QUADS) {
        for (i = 0; i + 3 < lab3d_ios_gl_vertex_count; i += 4) {
            glDrawArrays(GL_TRIANGLE_FAN, i, 4);
        }
    } else if (lab3d_ios_gl_mode == GL_POLYGON) {
        glDrawArrays(GL_TRIANGLE_FAN, 0, lab3d_ios_gl_vertex_count);
    } else {
        glDrawArrays(lab3d_ios_gl_mode, 0, lab3d_ios_gl_vertex_count);
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static inline void glDrawBuffer(GLenum buf) { (void)buf; }
static inline void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values) { (void)map; (void)mapsize; (void)values; }

static inline void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val)
{
    glOrthof((GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
}

static inline void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val)
{
    glFrustumf((GLfloat)left, (GLfloat)right, (GLfloat)bottom, (GLfloat)top, (GLfloat)near_val, (GLfloat)far_val);
}

static inline void glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
}

static inline void lab3d_ios_glPixelStorei(GLenum pname, GLint param)
{
    if (pname == GL_UNPACK_ROW_LENGTH || pname == GL_UNPACK_SKIP_PIXELS || pname == GL_UNPACK_SKIP_ROWS) {
        return;
    }
    glPixelStorei(pname, param);
}

#define glBegin lab3d_ios_glBegin
#define glEnd lab3d_ios_glEnd
#define glColor3f lab3d_ios_glColor3f
#define glColor4f lab3d_ios_glColor4f
#define glTexCoord2f lab3d_ios_glTexCoord2f
#define glVertex2s lab3d_ios_glVertex2s
#define glVertex3i lab3d_ios_glVertex3i
#define glVertex3f lab3d_ios_glVertex3f
#define glPixelStorei lab3d_ios_glPixelStorei

#endif
