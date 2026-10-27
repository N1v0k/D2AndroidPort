/*
 * Minimal <android/native_window.h> for the NDK API used by SDL2 (the public NDK ABI,
 * declarations only; the functions live in the device's libandroid.so / libnativewindow.so).
 */
#ifndef ANDROID_NATIVE_WINDOW_H
#define ANDROID_NATIVE_WINDOW_H

#include <stdint.h>
#include <sys/cdefs.h>
#include <android/hardware_buffer.h>
#include <android/rect.h>

__BEGIN_DECLS

enum ANativeWindow_LegacyFormat {
    WINDOW_FORMAT_RGBA_8888 = 1,
    WINDOW_FORMAT_RGBX_8888 = 2,
    WINDOW_FORMAT_RGB_565   = 4,
};

enum ANativeWindowTransform {
    ANATIVEWINDOW_TRANSFORM_IDENTITY          = 0x00,
    ANATIVEWINDOW_TRANSFORM_MIRROR_HORIZONTAL = 0x01,
    ANATIVEWINDOW_TRANSFORM_MIRROR_VERTICAL   = 0x02,
    ANATIVEWINDOW_TRANSFORM_ROTATE_90         = 0x04,
    ANATIVEWINDOW_TRANSFORM_ROTATE_180 = ANATIVEWINDOW_TRANSFORM_MIRROR_HORIZONTAL | ANATIVEWINDOW_TRANSFORM_MIRROR_VERTICAL,
    ANATIVEWINDOW_TRANSFORM_ROTATE_270 = ANATIVEWINDOW_TRANSFORM_ROTATE_180 | ANATIVEWINDOW_TRANSFORM_ROTATE_90,
};

struct ANativeWindow;
typedef struct ANativeWindow ANativeWindow;

typedef struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
} ANativeWindow_Buffer;

void ANativeWindow_acquire(ANativeWindow *window);
void ANativeWindow_release(ANativeWindow *window);
int32_t ANativeWindow_getWidth(ANativeWindow *window);
int32_t ANativeWindow_getHeight(ANativeWindow *window);
int32_t ANativeWindow_getFormat(ANativeWindow *window);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *window, int32_t width, int32_t height, int32_t format);
int32_t ANativeWindow_lock(ANativeWindow *window, ANativeWindow_Buffer *outBuffer, ARect *inOutDirtyBounds);
int32_t ANativeWindow_unlockAndPost(ANativeWindow *window);
int32_t ANativeWindow_setBuffersTransform(ANativeWindow *window, int32_t transform) __INTRODUCED_IN(26);
int32_t ANativeWindow_setBuffersDataSpace(ANativeWindow *window, int32_t dataSpace) __INTRODUCED_IN(28);
int32_t ANativeWindow_getBuffersDataSpace(ANativeWindow *window) __INTRODUCED_IN(28);

__END_DECLS

#endif
