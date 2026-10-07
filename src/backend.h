#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "lir.h"
#include "target.h"

#define CODA_BACKEND_ABI_VERSION 1
#define CODA_LIR_ABI_VERSION 1

typedef struct Backend Backend;

typedef struct {
    uint32_t abi_version;
    uint32_t lir_version;

    const char *name;

    bool (*supports)(const TargetInfo *target);
    bool (*emit)(Backend *backend, const TargetInfo *target, const LirModule *module, FILE *output);
    void (*destroy)(Backend *backend);
} BackendApi;

struct Backend {
    const BackendApi *api;
    void *data;
};

typedef const BackendApi *(*BackendGetApi)(void);

#endif