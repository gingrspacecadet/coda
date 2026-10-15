#include <string.h>

#include "target.h"

static const TargetInfo target_x86_64_linux = {
    .name = "x86_64-linux",
    .arch = TARGET_ARCH_X86_64,
    .os = TARGET_OS_LINUX,
    .abi = TARGET_ABI_SYSV,
    .object = TARGET_OBJECT_ELF64,
    .endian = TARGET_ENDIAN_LITTLE,
    .pointer = {
        .size = 8,
        .align = 8,
    },
    .features = UINT64_C(1) << TARGET_FEATURE_SSE2,
};

static const TargetInfo target_aarch64_linux = {
    .name = "aarch64-linux",
    .arch = TARGET_ARCH_AARCH64,
    .os = TARGET_OS_LINUX,
    .abi = TARGET_ABI_AAPCS64,
    .object = TARGET_OBJECT_ELF64,
    .endian = TARGET_ENDIAN_LITTLE,
    .pointer = {
        .size = 8,
        .align = 8,
    },
    .features = UINT64_C(1) << TARGET_FEATURE_NEON,
};

static const TargetInfo target_armhf_linux = {
    .name = "armhf-linux",
    .arch = TARGET_ARCH_ARMV7,
    .os = TARGET_OS_LINUX,
    .abi = TARGET_ABI_AAPCS32_HF,
    .object = TARGET_OBJECT_ELF32,
    .endian = TARGET_ENDIAN_LITTLE,
    .pointer = {
        .size = 4,
        .align = 4,
    },
};

static const TargetInfo target_riscv64_linux = {
    .name = "riscv64-linux",
    .arch = TARGET_ARCH_RISCV64,
    .os = TARGET_OS_LINUX,
    .abi = TARGET_ABI_SYSV,
    .object = TARGET_OBJECT_ELF64,
    .endian = TARGET_ENDIAN_LITTLE,
    .pointer = {
        .size = 8,
        .align = 8,
    },
    .features = UINT64_C(1) << TARGET_FEATURE_RV64I,
};

static const TargetInfo *targets[] = {
    &target_x86_64_linux,
    &target_aarch64_linux,
    &target_armhf_linux,
    &target_riscv64_linux,
};

const TargetInfo *target_native(void) {
#if defined(__x86_64__) && defined(__linux__)
    return &target_x86_64_linux;
#elif defined(__aarch64__) && defined(__linux__)
    return &target_aarch64_linux;
#elif defined(__arm__) && defined(__linux__)
    return &target_armhf_linux;
#elif defined(__riscv) && __riscv_xlen == 64 && defined(__linux__)
    return &target_riscv64_linux;
#else
    return NULL;
#endif
}

const TargetInfo *target_builtin(const char *name) {
    for (size_t i = 0; i < sizeof(targets) / sizeof(*targets); i++) {
        if (strcmp(targets[i]->name, name) == 0)
            return targets[i];
    }

    return NULL;
}

bool target_has_feature(const TargetInfo *target, TargetFeature feature) {
    return (target->features & (UINT64_C(1) << feature)) != 0;
}