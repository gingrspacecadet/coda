#include <dlfcn.h>
#include <stdio.h>

#include "backend.h"

typedef struct {
    void *handle;
    Backend backend;
} LoadedBackend;


//! TODO: make this OS agnostic!
bool backend_load(LoadedBackend *loaded, const char *path) {
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (handle == NULL) {
        fprintf(stderr, "failed to load backend: %s\n", dlerror());
        return false;
    }

    BackendGetApi get_api = (BackendGetApi)dlsym(handle, "coda_backend_get_api");

    if (get_api == NULL) {
        fprintf(stderr, "backend has no coda_backend_get_api\n");
        dlclose(handle);
        return false;
    }

    const BackendApi *api = get_api();

    if (api == NULL || api->abi_version != CODA_BACKEND_ABI_VERSION || api->lir_version != CODA_LIR_ABI_VERSION) {
        fprintf(stderr, "incompatible Coda backend\n");
        dlclose(handle);
        return false;
    }

    loaded->handle = handle;
    loaded->backend.api = api;
    loaded->backend.data = NULL;

    return true;
}

void backend_unload(LoadedBackend *loaded) {
    if (loaded->backend.api != NULL && loaded->backend.api->destroy != NULL)
        loaded->backend.api->destroy(&loaded->backend);

    if (loaded->handle != NULL)
        dlclose(loaded->handle);

    *loaded = (LoadedBackend){0};
}