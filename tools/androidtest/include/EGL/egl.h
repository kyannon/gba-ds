typedef void* EGLDisplay; typedef void* EGLContext; typedef void* EGLSurface; typedef void* EGLConfig; typedef int EGLint; typedef unsigned EGLBoolean;
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_CONFIG_ID 0x3028
#define EGL_NONE 0x3038
#define EGL_DRAW 0x3059
#define EGL_READ 0x305A
#define EGL_NATIVE_VISUAL_ID 0x302E
EGLDisplay eglGetCurrentDisplay(void); EGLContext eglGetCurrentContext(void); EGLSurface eglGetCurrentSurface(EGLint);
EGLBoolean eglQueryContext(EGLDisplay, EGLContext, EGLint, EGLint*); EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig, EGLint, EGLint*); EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, void*, const EGLint*);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext); EGLBoolean eglSwapInterval(EGLDisplay, EGLint); EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface);
EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface); EGLint eglGetError(void);
