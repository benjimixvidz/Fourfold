// Test-only preload for Rack: makes XQueryPointer report the pointer position written to the
// file $FAKE_POINTER ("X Y" in window coordinates), so synthetic events from build/x-send are
// not overridden by the real pointer (GLFW asks the server where the pointer is). The user's
// own pointer is never moved. GLFW resolves libX11 with dlsym, hence the dlsym hook.
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef Bool (*QueryPointerFn)(Display*, Window, Window*, Window*, int*, int*, int*, int*, unsigned*);
typedef void* (*DlsymFn)(void*, const char*);

static QueryPointerFn realQueryPointer;

static DlsymFn realDlsym(void)
{
    static DlsymFn fn;
    if (!fn)
        fn = (DlsymFn)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
    if (!fn)
        fn = (DlsymFn)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    return fn;
}

static Bool fakeQueryPointer(Display* d, Window w, Window* root, Window* child, int* rx, int* ry, int* wx,
                             int* wy, unsigned* mask)
{
    const Bool ok = realQueryPointer(d, w, root, child, rx, ry, wx, wy, mask);
    const char* path = getenv("FAKE_POINTER");
    FILE* f = path ? fopen(path, "r") : NULL;
    int x, y;
    if (f && fscanf(f, "%d %d", &x, &y) == 2) {
        *rx += x - *wx;
        *ry += y - *wy;
        *wx = x;
        *wy = y;
        *child = None;
    }
    if (f)
        fclose(f);
    return ok;
}

void* dlsym(void* handle, const char* name)
{
    void* p = realDlsym()(handle, name);
    if (p && !strcmp(name, "XQueryPointer")) {
        realQueryPointer = (QueryPointerFn)p;
        return (void*)fakeQueryPointer;
    }
    return p;
}
