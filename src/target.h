#ifndef TARGET_H
#define TARGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TARGET_ARCH_X86_64,
    TARGET_ARCH_X86,
    TARGET_ARCH_ARMV7,
    TARGET_ARCH_AARCH64,
    TARGET_ARCH_RISCV32,
    TARGET_ARCH_RISCV64,
    TARGET_ARCH_ORION,
} TargetArch;

typedef enum {
    TARGET_OS_NONE,
    TARGET_OS_LINUX,
    TARGET_OS_WINDOWS,
} TargetOs;

typedef enum {
    TARGET_ABI_NONE,
    TARGET_ABI_SYSV,
    TARGET_ABI_AAPCS32,
    TARGET_ABI_AAPCS32_HF,
    TARGET_ABI_AAPCS64,
    TARGET_ABI_ORION,
} TargetAbi;

typedef enum {
    TARGET_OBJECT_NONE,
    TARGET_OBJECT_ELF32,
    TARGET_OBJECT_ELF64,
    TARGET_OBJECT_COFF,
} TargetObjectFormat;

typedef enum {
    TARGET_ENDIAN_LITTLE,
    TARGET_ENDIAN_BIG,
} TargetEndian;

typedef enum {
    TARGET_FEATURE_SSE2,
    TARGET_FEATURE_AVX,
    TARGET_FEATURE_AVX2,
    TARGET_FEATURE_NEON,
    TARGET_FEATURE_RV32I,
    TARGET_FEATURE_RV64I,
} TargetFeature;

typedef struct {
    size_t size;
    size_t align;
} TargetLayout;

typedef struct {
    const char *name;

    TargetArch arch;
    TargetOs os;
    TargetAbi abi;
    TargetObjectFormat object;
    TargetEndian endian;

    TargetLayout pointer;

    uint64_t features;
} TargetInfo;

const TargetInfo *target_native(void);
const TargetInfo *target_builtin(const char *name);
bool target_has_feature(const TargetInfo *target, TargetFeature feature);

#endif