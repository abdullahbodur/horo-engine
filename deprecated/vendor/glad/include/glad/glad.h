/*
 * glad — OpenGL 4.1 Core Profile loader
 * Generated for: OpenGL 4.1, Core profile, no extensions
 * Source: https://glad.dav1d.de
 *
 * Committed to repository — do not regenerate unless targeting a different GL
 * version.
 */
#ifndef __glad_h_
#define __glad_h_

#ifdef __gl_h_
#error OpenGL header already included before glad -- include glad first
#endif
#define __gl_h_

#if defined(_WIN32) && !defined(APIENTRY) && !defined(__CYGWIN__) &&           \
    !defined(__SCITECH_SNAP__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif
#ifndef GLAPI
#define GLAPI extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Scalar types ---- */
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLclampx;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef char GLchar;
typedef long GLintptr;
typedef long GLsizeiptr;
typedef long long GLint64;
typedef unsigned long long GLuint64;
typedef struct __GLsync *GLsync;

/* ---- Constants ---- */
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_NO_ERROR 0

/* Primitives */
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006

/* Blending */
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308

/* Depth test */
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

/* Errors */
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506

/* Buffer bits */
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000

/* Enable caps */
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_DITHER 0x0BD0
#define GL_LINE_SMOOTH 0x0B20
#define GL_MULTISAMPLE 0x809D
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#define GL_SCISSOR_TEST 0x0C11
#define GL_STENCIL_TEST 0x0B90
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_PROGRAM_POINT_SIZE 0x8642

/* Face/winding */
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408

/* Polygon mode */
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02

/* Buffer targets */
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_COPY_READ_BUFFER 0x8F36
#define GL_COPY_WRITE_BUFFER 0x8F37
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E

/* Buffer usage */
#define GL_STREAM_DRAW 0x88E0
#define GL_STREAM_READ 0x88E1
#define GL_STREAM_COPY 0x88E2
#define GL_STATIC_DRAW 0x88E4
#define GL_STATIC_READ 0x88E5
#define GL_STATIC_COPY 0x88E6
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_DYNAMIC_READ 0x88E9
#define GL_DYNAMIC_COPY 0x88EA

/* Buffer map */
#define GL_READ_ONLY 0x88B8
#define GL_WRITE_ONLY 0x88B9
#define GL_READ_WRITE 0x88BA

/* Shader types */
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_GEOMETRY_SHADER 0x8DD9
#define GL_TESS_EVALUATION_SHADER 0x8E87
#define GL_TESS_CONTROL_SHADER 0x8E88
#define GL_COMPUTE_SHADER 0x91B9

/* Shader / program params */
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ATTACHED_SHADERS 0x8B85
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ACTIVE_UNIFORM_MAX_LENGTH 0x8B87
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#define GL_ACTIVE_ATTRIBUTE_MAX_LENGTH 0x8B8A
#define GL_DELETE_STATUS 0x8B80

/* Texture targets */
#define GL_TEXTURE_1D 0x0DE0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_1D_ARRAY 0x8C18
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE_RECTANGLE 0x84F5

/* Texture params */
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_MIRRORED_REPEAT 0x8370

/* Texture units */
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3

/* Pixel formats */
#define GL_RED 0x1903
#define GL_RG 0x8227
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_BGR 0x80E0
#define GL_BGRA 0x80E1
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_STENCIL 0x84F9

/* Pixel types */
#define GL_UNSIGNED_BYTE 0x1401
#define GL_BYTE 0x1400
#define GL_UNSIGNED_SHORT 0x1403
#define GL_SHORT 0x1402
#define GL_UNSIGNED_INT 0x1405
#define GL_INT 0x1404
#define GL_FLOAT 0x1406

/* Internal formats */
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_RGBA8 0x8058
#define GL_RGB8 0x8051

/* Framebuffer */
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD

/* Vertex attrib */
#define GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING 0x889F

/* Misc */
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_VIEWPORT 0x0BA2
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_MAX_UNIFORM_LOCATIONS 0x826E

/* ---- Function pointer typedefs (subset used by Monolith) ---- */
typedef void(APIENTRYP PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void(APIENTRYP PFNGLCULLFACEPROC)(GLenum mode);
typedef void(APIENTRYP PFNGLFRONTFACEPROC)(GLenum mode);
typedef void(APIENTRYP PFNGLLINEWIDTHPROC)(GLfloat width);
typedef void(APIENTRYP PFNGLPOLYGONMODEPROC)(GLenum face, GLenum mode);
typedef void(APIENTRYP PFNGLPOLYGONOFFSETPROC)(GLfloat factor, GLfloat units);
typedef void(APIENTRYP PFNGLSCISSORPROC)(GLint x, GLint y, GLsizei width,
                                         GLsizei height);
typedef void(APIENTRYP PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level,
                                            GLint internalformat, GLsizei width,
                                            GLsizei height, GLint border,
                                            GLenum format, GLenum type,
                                            const void *pixels);
typedef void(APIENTRYP PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void(APIENTRYP PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width,
                                            GLsizei height, GLenum format,
                                            GLenum type, void *pixels);
typedef void(APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width,
                                          GLsizei height);
typedef void(APIENTRYP PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count,
                                              GLenum type, const void *indices);
typedef void(APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first,
                                            GLsizei count);
typedef void(APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void(APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green,
                                            GLfloat blue, GLfloat alpha);
typedef void(APIENTRYP PFNGLCLEARDEPTHPROC)(GLdouble depth);
typedef void(APIENTRYP PFNGLENABLEPROC)(GLenum cap);
typedef void(APIENTRYP PFNGLDISABLEPROC)(GLenum cap);
typedef GLboolean(APIENTRYP PFNGLISENABLEDPROC)(GLenum cap);
typedef void(APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef void(APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n,
                                                const GLuint *textures);
typedef void(APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void(APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void(APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname,
                                               GLint param);
typedef void(APIENTRYP PFNGLGENERATEMIPMAPPROC)(GLenum target);
typedef void(APIENTRYP PFNGLPIXELSTOREIPROC)(GLenum pname, GLint param);
typedef GLenum(APIENTRYP PFNGLGETERRORPROC)(void);
typedef const GLubyte *(APIENTRYP PFNGLGETSTRINGPROC)(GLenum name);
typedef void(APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint *data);
/* VAO */
typedef void(APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void(APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n,
                                                    const GLuint *arrays);
typedef void(APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
/* VBO */
typedef void(APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void(APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei n,
                                               const GLuint *buffers);
typedef void(APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void(APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size,
                                            const void *data, GLenum usage);
typedef void(APIENTRYP PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset,
                                               GLsizeiptr size,
                                               const void *data);
/* Vertex attribs */
typedef void(APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size,
                                                     GLenum type,
                                                     GLboolean normalized,
                                                     GLsizei stride,
                                                     const void *pointer);
typedef void(APIENTRYP PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint index, GLint size,
                                                      GLenum type,
                                                      GLsizei stride,
                                                      const void *pointer);
typedef void(APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void(APIENTRYP PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);
/* Shaders */
typedef GLuint(APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void(APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef void(APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count,
                                              const GLchar *const *string,
                                              const GLint *length);
typedef void(APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void(APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname,
                                             GLint *params);
typedef void(APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader,
                                                  GLsizei bufSize,
                                                  GLsizei *length,
                                                  GLchar *infoLog);
/* Programs */
typedef GLuint(APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void(APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void(APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void(APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void(APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname,
                                              GLint *params);
typedef void(APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program,
                                                   GLsizei bufSize,
                                                   GLsizei *length,
                                                   GLchar *infoLog);
typedef GLint(APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program,
                                                     const GLchar *name);
typedef void(APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void(APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void(APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0,
                                           GLfloat v1);
typedef void(APIENTRYP PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0,
                                           GLfloat v1, GLfloat v2);
typedef void(APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0,
                                           GLfloat v1, GLfloat v2, GLfloat v3);
typedef void(APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count,
                                                  GLboolean transpose,
                                                  const GLfloat *value);
typedef void(APIENTRYP PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count,
                                                  GLboolean transpose,
                                                  const GLfloat *value);
/* Framebuffers */
typedef void(APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n,
                                                 GLuint *framebuffers);
typedef void(APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n,
                                                    const GLuint *framebuffers);
typedef void(APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target,
                                                 GLuint framebuffer);
typedef GLenum(APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void(APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target,
                                                      GLenum attachment,
                                                      GLenum textarget,
                                                      GLuint texture,
                                                      GLint level);
typedef void(APIENTRYP PFNGLGENRENDERBUFFERSPROC)(GLsizei n,
                                                  GLuint *renderbuffers);
typedef void(APIENTRYP PFNGLDELETERENDERBUFFERSPROC)(
    GLsizei n, const GLuint *renderbuffers);
typedef void(APIENTRYP PFNGLBINDRENDERBUFFERPROC)(GLenum target,
                                                  GLuint renderbuffer);
typedef void(APIENTRYP PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target,
                                                     GLenum internalformat,
                                                     GLsizei width,
                                                     GLsizei height);
typedef void(APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFERPROC)(
    GLenum target, GLenum attachment, GLenum renderbuffertarget,
    GLuint renderbuffer);
/* Debug */
typedef void(APIENTRYP PFNGLDEBUGMESSAGECALLBACKPROC)(void *callback,
                                                      const void *userParam);

/* ---- Extern declarations ---- */
GLAPI PFNGLBLENDFUNCPROC glad_glBlendFunc;
GLAPI PFNGLCULLFACEPROC glad_glCullFace;
GLAPI PFNGLFRONTFACEPROC glad_glFrontFace;
GLAPI PFNGLLINEWIDTHPROC glad_glLineWidth;
GLAPI PFNGLPOLYGONMODEPROC glad_glPolygonMode;
GLAPI PFNGLPOLYGONOFFSETPROC glad_glPolygonOffset;
GLAPI PFNGLSCISSORPROC glad_glScissor;
GLAPI PFNGLTEXIMAGE2DPROC glad_glTexImage2D;
GLAPI PFNGLDEPTHFUNCPROC glad_glDepthFunc;
GLAPI PFNGLVIEWPORTPROC glad_glViewport;
GLAPI PFNGLDRAWELEMENTSPROC glad_glDrawElements;
GLAPI PFNGLDRAWARRAYSPROC glad_glDrawArrays;
GLAPI PFNGLCLEARPROC glad_glClear;
GLAPI PFNGLCLEARCOLORPROC glad_glClearColor;
GLAPI PFNGLCLEARDEPTHPROC glad_glClearDepth;
GLAPI PFNGLENABLEPROC glad_glEnable;
GLAPI PFNGLDISABLEPROC glad_glDisable;
GLAPI PFNGLISENABLEDPROC glad_glIsEnabled;
GLAPI PFNGLGENTEXTURESPROC glad_glGenTextures;
GLAPI PFNGLDELETETEXTURESPROC glad_glDeleteTextures;
GLAPI PFNGLBINDTEXTUREPROC glad_glBindTexture;
GLAPI PFNGLACTIVETEXTUREPROC glad_glActiveTexture;
GLAPI PFNGLTEXPARAMETERIPROC glad_glTexParameteri;
GLAPI PFNGLGENERATEMIPMAPPROC glad_glGenerateMipmap;
GLAPI PFNGLPIXELSTOREIPROC glad_glPixelStorei;
GLAPI PFNGLGETERRORPROC glad_glGetError;
GLAPI PFNGLGETSTRINGPROC glad_glGetString;
GLAPI PFNGLGETINTEGERVPROC glad_glGetIntegerv;
GLAPI PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays;
GLAPI PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays;
GLAPI PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray;
GLAPI PFNGLGENBUFFERSPROC glad_glGenBuffers;
GLAPI PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers;
GLAPI PFNGLBINDBUFFERPROC glad_glBindBuffer;
GLAPI PFNGLBUFFERDATAPROC glad_glBufferData;
GLAPI PFNGLBUFFERSUBDATAPROC glad_glBufferSubData;
GLAPI PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer;
GLAPI PFNGLVERTEXATTRIBIPOINTERPROC glad_glVertexAttribIPointer;
GLAPI PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray;
GLAPI PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;
GLAPI PFNGLCREATESHADERPROC glad_glCreateShader;
GLAPI PFNGLDELETESHADERPROC glad_glDeleteShader;
GLAPI PFNGLSHADERSOURCEPROC glad_glShaderSource;
GLAPI PFNGLCOMPILESHADERPROC glad_glCompileShader;
GLAPI PFNGLGETSHADERIVPROC glad_glGetShaderiv;
GLAPI PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog;
GLAPI PFNGLCREATEPROGRAMPROC glad_glCreateProgram;
GLAPI PFNGLDELETEPROGRAMPROC glad_glDeleteProgram;
GLAPI PFNGLATTACHSHADERPROC glad_glAttachShader;
GLAPI PFNGLLINKPROGRAMPROC glad_glLinkProgram;
GLAPI PFNGLUSEPROGRAMPROC glad_glUseProgram;
GLAPI PFNGLGETPROGRAMIVPROC glad_glGetProgramiv;
GLAPI PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog;
GLAPI PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation;
GLAPI PFNGLUNIFORM1IPROC glad_glUniform1i;
GLAPI PFNGLUNIFORM1FPROC glad_glUniform1f;
GLAPI PFNGLUNIFORM2FPROC glad_glUniform2f;
GLAPI PFNGLUNIFORM3FPROC glad_glUniform3f;
GLAPI PFNGLUNIFORM4FPROC glad_glUniform4f;
GLAPI PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv;
GLAPI PFNGLUNIFORMMATRIX3FVPROC glad_glUniformMatrix3fv;
GLAPI PFNGLREADPIXELSPROC glad_glReadPixels;
GLAPI PFNGLGENFRAMEBUFFERSPROC glad_glGenFramebuffers;
GLAPI PFNGLDELETEFRAMEBUFFERSPROC glad_glDeleteFramebuffers;
GLAPI PFNGLBINDFRAMEBUFFERPROC glad_glBindFramebuffer;
GLAPI PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus;
GLAPI PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;
GLAPI PFNGLGENRENDERBUFFERSPROC glad_glGenRenderbuffers;
GLAPI PFNGLDELETERENDERBUFFERSPROC glad_glDeleteRenderbuffers;
GLAPI PFNGLBINDRENDERBUFFERPROC glad_glBindRenderbuffer;
GLAPI PFNGLRENDERBUFFERSTORAGEPROC glad_glRenderbufferStorage;
GLAPI PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;

/* ---- Macros ---- */
#define glBlendFunc glad_glBlendFunc
#define glCullFace glad_glCullFace
#define glFrontFace glad_glFrontFace
#define glLineWidth glad_glLineWidth
#define glPolygonMode glad_glPolygonMode
#define glPolygonOffset glad_glPolygonOffset
#define glScissor glad_glScissor
#define glTexImage2D glad_glTexImage2D
#define glDepthFunc glad_glDepthFunc
#define glViewport glad_glViewport
#define glDrawElements glad_glDrawElements
#define glDrawArrays glad_glDrawArrays
#define glClear glad_glClear
#define glClearColor glad_glClearColor
#define glClearDepth glad_glClearDepth
#define glEnable glad_glEnable
#define glDisable glad_glDisable
#define glIsEnabled glad_glIsEnabled
#define glGenTextures glad_glGenTextures
#define glDeleteTextures glad_glDeleteTextures
#define glBindTexture glad_glBindTexture
#define glActiveTexture glad_glActiveTexture
#define glTexParameteri glad_glTexParameteri
#define glGenerateMipmap glad_glGenerateMipmap
#define glPixelStorei glad_glPixelStorei
#define glGetError glad_glGetError
#define glGetString glad_glGetString
#define glGetIntegerv glad_glGetIntegerv
#define glGenVertexArrays glad_glGenVertexArrays
#define glDeleteVertexArrays glad_glDeleteVertexArrays
#define glBindVertexArray glad_glBindVertexArray
#define glGenBuffers glad_glGenBuffers
#define glDeleteBuffers glad_glDeleteBuffers
#define glBindBuffer glad_glBindBuffer
#define glBufferData glad_glBufferData
#define glBufferSubData glad_glBufferSubData
#define glVertexAttribPointer glad_glVertexAttribPointer
#define glVertexAttribIPointer glad_glVertexAttribIPointer
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glDisableVertexAttribArray glad_glDisableVertexAttribArray
#define glCreateShader glad_glCreateShader
#define glDeleteShader glad_glDeleteShader
#define glShaderSource glad_glShaderSource
#define glCompileShader glad_glCompileShader
#define glGetShaderiv glad_glGetShaderiv
#define glGetShaderInfoLog glad_glGetShaderInfoLog
#define glCreateProgram glad_glCreateProgram
#define glDeleteProgram glad_glDeleteProgram
#define glAttachShader glad_glAttachShader
#define glLinkProgram glad_glLinkProgram
#define glUseProgram glad_glUseProgram
#define glGetProgramiv glad_glGetProgramiv
#define glGetProgramInfoLog glad_glGetProgramInfoLog
#define glGetUniformLocation glad_glGetUniformLocation
#define glUniform1i glad_glUniform1i
#define glUniform1f glad_glUniform1f
#define glUniform2f glad_glUniform2f
#define glUniform3f glad_glUniform3f
#define glUniform4f glad_glUniform4f
#define glUniformMatrix4fv glad_glUniformMatrix4fv
#define glUniformMatrix3fv glad_glUniformMatrix3fv
#define glReadPixels glad_glReadPixels
#define glGenFramebuffers glad_glGenFramebuffers
#define glDeleteFramebuffers glad_glDeleteFramebuffers
#define glBindFramebuffer glad_glBindFramebuffer
#define glCheckFramebufferStatus glad_glCheckFramebufferStatus
#define glFramebufferTexture2D glad_glFramebufferTexture2D
#define glGenRenderbuffers glad_glGenRenderbuffers
#define glDeleteRenderbuffers glad_glDeleteRenderbuffers
#define glBindRenderbuffer glad_glBindRenderbuffer
#define glRenderbufferStorage glad_glRenderbufferStorage
#define glFramebufferRenderbuffer glad_glFramebufferRenderbuffer

/* ---- GL constants not originally in this minimal header ---- */
#define GL_BOOL           0x8B56
#define GL_R8             0x8229
#define GL_R32I           0x8235
#define GL_RED_INTEGER    0x8D94
#define GL_COLOR          0x1800
#define GL_UNSIGNED_INT_24_8 0x84FA
#define GL_COLOR_ATTACHMENT1  0x8CE1
#define GL_COLOR_ATTACHMENT2  0x8CE2
#define GL_COLOR_ATTACHMENT3  0x8CE3

/* ---- Additional GL 4.1 Core functions needed by renderer/opengl/ ---- */
typedef void (APIENTRYP PFNGLDRAWBUFFERSPROC)(GLsizei n, const GLenum *bufs);
typedef void (APIENTRYP PFNGLDRAWBUFFERPROC)(GLenum buf);
typedef void (APIENTRYP PFNGLREADBUFFERPROC)(GLenum src);
typedef void (APIENTRYP PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level,
    GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
    GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLVERTEXATTRIBDIVISORPROC)(GLuint index, GLuint divisor);
typedef void (APIENTRYP PFNGLCLEARBUFFERIVPROC)(GLenum buffer, GLint drawbuffer,
    const GLint *value);

GLAPI PFNGLDRAWBUFFERSPROC     glad_glDrawBuffers;
GLAPI PFNGLDRAWBUFFERPROC      glad_glDrawBuffer;
GLAPI PFNGLREADBUFFERPROC      glad_glReadBuffer;
GLAPI PFNGLTEXSUBIMAGE2DPROC   glad_glTexSubImage2D;
GLAPI PFNGLVERTEXATTRIBDIVISORPROC glad_glVertexAttribDivisor;
GLAPI PFNGLCLEARBUFFERIVPROC   glad_glClearBufferiv;

#define glDrawBuffers        glad_glDrawBuffers
#define glDrawBuffer         glad_glDrawBuffer
#define glReadBuffer         glad_glReadBuffer
#define glTexSubImage2D      glad_glTexSubImage2D
#define glVertexAttribDivisor glad_glVertexAttribDivisor
#define glClearBufferiv      glad_glClearBufferiv

/* ---- Loader ---- */
typedef void *(*GLADloadproc)(const char *name);
int gladLoadGLLoader(GLADloadproc load);

#ifdef __cplusplus
}
#endif

#endif /* __glad_h_ */
