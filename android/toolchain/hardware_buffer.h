/* Minimal <android/hardware_buffer.h> (public NDK ABI: the opaque type and the pixel formats) */
#ifndef ANDROID_HARDWARE_BUFFER_H
#define ANDROID_HARDWARE_BUFFER_H

#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

enum AHardwareBuffer_Format {
    AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM     = 1,
    AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM     = 2,
    AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM       = 3,
    AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM       = 4,
    AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT = 0x16,
    AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM  = 0x2b,
    AHARDWAREBUFFER_FORMAT_BLOB               = 0x21,
};

typedef struct AHardwareBuffer AHardwareBuffer;

__END_DECLS

#endif
