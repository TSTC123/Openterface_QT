#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

// Use dlsym to get the real dlopen at runtime instead of relying on
// linker --wrap mechanism, which is sensitive to link order.
typedef void* (*real_dlopen_t)(const char*, int);

static real_dlopen_t get_real_dlopen(void) {
    static real_dlopen_t real_fn = NULL;
    if (!real_fn) {
        real_fn = (real_dlopen_t)dlsym(RTLD_NEXT, "dlopen");
    }
    return real_fn;
}

static int in_dlopen_wrapper = 0;

void* __wrap_dlopen(const char* filename, int flag) {
    if (in_dlopen_wrapper) {
        return NULL;
    }

    in_dlopen_wrapper = 1;
    void* result = NULL;
    real_dlopen_t real_dlopen = get_real_dlopen();

    if (filename) {
        if (strstr(filename, "libva") ||
            strstr(filename, "va.so") ||
            strstr(filename, "va-drm") ||
            strstr(filename, "va-x11") ||
            strstr(filename, "vaapi")) {
            printf("Static build: Allowing VAAPI library: dlopen(\"%s\")\n", filename);
            result = real_dlopen ? real_dlopen(filename, flag) : dlopen(filename, flag);
        }
        else if (strstr(filename, "libdrm") ||
                 strstr(filename, "libEGL") ||
                 strstr(filename, "libGL")) {
            printf("Static build: Allowing graphics library: dlopen(\"%s\")\n", filename);
            result = real_dlopen ? real_dlopen(filename, flag) : dlopen(filename, flag);
        }
        else {
            printf("Static build: dlopen(\"%s\") disabled\n", filename);
            result = NULL;
        }
    }

    in_dlopen_wrapper = 0;
    return result;
}
