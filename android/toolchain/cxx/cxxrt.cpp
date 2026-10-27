/* The few C++ runtime pieces CascLib needs, without libc++ (it is built with -fno-exceptions -fno-rtti):
   operator new/delete on malloc/free, and the pure-virtual trap. */
#include <stdlib.h>

typedef __SIZE_TYPE__ size_t;
namespace std { struct nothrow_t { explicit nothrow_t() = default; }; extern const nothrow_t nothrow; }
const std::nothrow_t std::nothrow{};

void *operator new(size_t n) { void *p = malloc(n ? n : 1); if (!p) abort(); return p; }
void *operator new[](size_t n) { return operator new(n); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return malloc(n ? n : 1); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return malloc(n ? n : 1); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { free(p); }
extern "C" void __cxa_pure_virtual(void) { abort(); }
