#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>

// Test-only libc faults; the production writer has no injection API.
static bool fail(const char* operation) {
    const char* value = std::getenv("LIARSOFT_TEST_IO_FAILURE");
    if (!value || std::strcmp(value, operation)) return false;
    errno = EIO;
    return true;
}
extern "C" int fflush(FILE* file) {
    const auto real = reinterpret_cast<int(*)(FILE*)>(dlsym(RTLD_NEXT, "fflush"));
    return fail("write") ? EOF : real(file);
}
extern "C" int fclose(FILE* file) {
    const auto real = reinterpret_cast<int(*)(FILE*)>(dlsym(RTLD_NEXT, "fclose"));
    const int result = real(file);
    return fail("close") ? EOF : result;
}
extern "C" int rename(const char* source, const char* target) {
    const auto real = reinterpret_cast<int(*)(const char*, const char*)>(dlsym(RTLD_NEXT, "rename"));
    return fail("replace") ? -1 : real(source, target);
}
