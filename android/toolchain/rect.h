/* Minimal <android/rect.h> (public NDK ABI) */
#ifndef ANDROID_RECT_H
#define ANDROID_RECT_H

#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

typedef struct ARect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} ARect;

__END_DECLS

#endif
