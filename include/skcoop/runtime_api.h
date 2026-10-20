#ifndef SKCOOP_RUNTIME_API_H
#define SKCOOP_RUNTIME_API_H

#include <stdint.h>

#if defined(_WIN32) && defined(SKCOOP_RUNTIME_BUILD)
#define SKCOOP_API __declspec(dllexport)
#else
#define SKCOOP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SKCOOP_RUNTIME_ABI_VERSION 1u
#define SKCOOP_RUNTIME_BUILD_ID "0.0.1-bootstrap"

enum skcoop_result {
    SKCOOP_OK = 0,
    SKCOOP_INVALID_ARGUMENT = 1,
    SKCOOP_ABI_MISMATCH = 2,
    SKCOOP_UNIMPLEMENTED = 3
};
enum skcoop_runtime_mode { SKCOOP_MODE_DIAGNOSTIC = 1, SKCOOP_MODE_GAMEPLAY = 2 };
enum skcoop_runtime_state { SKCOOP_STATE_LOADED = 1, SKCOOP_STATE_DIAGNOSTIC_READY = 2 };
enum skcoop_capability {
    SKCOOP_CAP_DIAGNOSTICS = 1u,
    SKCOOP_CAP_NATIVE_HOOKS = 2u,
    SKCOOP_CAP_SAVE_ISOLATION = 4u,
    SKCOOP_CAP_MULTIPLAYER = 8u
};
enum skcoop_implementation_status { SKCOOP_NOT_IMPLEMENTED = 0, SKCOOP_IMPLEMENTED = 1 };

typedef struct skcoop_init_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t mode;
    uint32_t reserved;
} skcoop_init_options;

typedef struct skcoop_health {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t state;
    uint32_t reserved;
} skcoop_health;

typedef struct skcoop_capabilities {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t mask;
    uint32_t native_hooks;
    uint32_t save_isolation;
    uint32_t multiplayer;
} skcoop_capabilities;

/* Call initialize explicitly after LoadLibrary returns, outside the loader lock.
 * ABI v1 requires exact struct_size; all reserved fields must be zero.
 * Health/capability output objects must have struct_size set by the caller.
 * Only diagnostic initialization is implemented. No game memory is touched. */
SKCOOP_API uint32_t skcoop_runtime_abi_version(void);
SKCOOP_API uint32_t skcoop_runtime_health(skcoop_health* output);
SKCOOP_API uint32_t skcoop_runtime_capabilities(skcoop_capabilities* output);
SKCOOP_API uint32_t skcoop_runtime_initialize(const skcoop_init_options* options);
SKCOOP_API uint32_t skcoop_runtime_shutdown(void);

typedef uint32_t (*skcoop_abi_version_fn)(void);
typedef uint32_t (*skcoop_health_fn)(skcoop_health*);
typedef uint32_t (*skcoop_capabilities_fn)(skcoop_capabilities*);
typedef uint32_t (*skcoop_initialize_fn)(const skcoop_init_options*);
typedef uint32_t (*skcoop_shutdown_fn)(void);

#ifdef __cplusplus
}
#endif
#endif
