#include <stdint.h>
typedef struct ANativeWindow ANativeWindow;
typedef struct { int32_t width, height, stride, format; void* bits; uint32_t reserved[6]; } ANativeWindow_Buffer;
enum { WINDOW_FORMAT_RGBX_8888 = 2 };
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow*, int32_t, int32_t, int32_t);
int32_t ANativeWindow_lock(ANativeWindow*, ANativeWindow_Buffer*, void*);
int32_t ANativeWindow_unlockAndPost(ANativeWindow*);
void ANativeWindow_release(ANativeWindow*);
