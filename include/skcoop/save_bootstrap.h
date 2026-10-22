#pragma once

#include <Windows.h>
#include <stdint.h>

#define SKCOOP_BOOTSTRAP_VERSION 2u
#define SKCOOP_BOOTSTRAP_ROOT_CHARS 1024u
#define SKCOOP_BOOTSTRAP_ACCOUNT_CHARS 32u
#define SKCOOP_BOOTSTRAP_FIXTURE_MAGIC UINT64_C(0x534b434f4f504658)
#define SKCOOP_BOOTSTRAP_DLL_NAME L"skcoop_save_bootstrap.dll"
#define SKCOOP_BOOTSTRAP_FIXTURE_NAME L"skcoop_save_bootstrap_fixture.exe"

static const GUID SKCOOP_BOOTSTRAP_PAYLOAD_GUID =
    {0xc748cfe2, 0xc124, 0x45e0, {0xb3, 0xba, 0x13, 0x41, 0x9b, 0x11, 0x85, 0x4c}};

enum skcoop_bootstrap_phase {
    SKCOOP_BOOTSTRAP_COLD = 0,
    SKCOOP_BOOTSTRAP_ATTACHING = 1,
    SKCOOP_BOOTSTRAP_READY = 2,
    SKCOOP_BOOTSTRAP_FAILED = 3
};
enum skcoop_bootstrap_checkpoint {
    SKCOOP_BOOTSTRAP_TLS = 1,
    SKCOOP_BOOTSTRAP_GLOBAL = 2,
    SKCOOP_BOOTSTRAP_MAIN = 4,
    SKCOOP_BOOTSTRAP_IO_COMPLETE = 8
};
enum skcoop_bootstrap_profile_mode {
    SKCOOP_BOOTSTRAP_DENY_ONLY = 0,
    SKCOOP_BOOTSTRAP_NATIVE_PROFILE = 1
};
enum skcoop_bootstrap_test_flags {
    SKCOOP_BOOTSTRAP_TEST_NORMAL = 0,
    SKCOOP_BOOTSTRAP_TEST_NO_PAYLOAD = 1,
    SKCOOP_BOOTSTRAP_TEST_TRUNCATED_PAYLOAD = 2,
    SKCOOP_BOOTSTRAP_TEST_BAD_VERSION = 4,
    SKCOOP_BOOTSTRAP_TEST_ABORT_TRANSACTION = 8,
    SKCOOP_BOOTSTRAP_TEST_BAD_NONCE = 16,
    SKCOOP_BOOTSTRAP_TEST_NATIVE_PROFILE = 32
};

// POD wire format copied into a suspended, owned fixture. No process-local
// pointers: HANDLE values refer to explicitly duplicated child handles.
typedef struct skcoop_bootstrap_payload {
    uint32_t struct_size;
    uint32_t version;
    uint32_t process_id;
    uint32_t test_flags;
    uint32_t profile_mode;
    uint8_t nonce[16];
    uint8_t executable_sha256[32];
    // AppData root, not the Sekiro/account directory; coop_root mirrors it.
    WCHAR protected_root[SKCOOP_BOOTSTRAP_ROOT_CHARS];
    WCHAR coop_root[SKCOOP_BOOTSTRAP_ROOT_CHARS];
    WCHAR account[SKCOOP_BOOTSTRAP_ACCOUNT_CHARS];
    HANDLE status_mapping;
    HANDLE ready_event;
} skcoop_bootstrap_payload;

// Published by Interlocked operations; read by the host only after ready and
// process termination. The sequence values make initialization order explicit.
typedef struct skcoop_bootstrap_status {
    uint32_t struct_size;
    uint32_t version;
    uint32_t process_id;
    uint32_t profile_mode;
    uint8_t nonce[16];
    volatile LONG phase;
    volatile LONG system_error;
    volatile LONG installed_hooks;
    volatile LONG denied_opens;
    volatile LONG unexpected_results;
    volatile LONG checkpoint_mask;
    volatile LONG sequence;
    volatile LONG ready_sequence;
    volatile LONG tls_sequence;
    volatile LONG global_sequence;
    volatile LONG main_sequence;
    volatile LONG transaction_aborted;
    volatile LONG io_sequence;
    volatile LONG native_redirected;
    volatile LONG native_denied;
    volatile LONG native_passthrough;
    volatile LONG native_internal_bypass;
    volatile LONG native_appdata_redirects;
    volatile LONG native_directory_queries;
    volatile LONG native_copy_failures;
    volatile LONG native_destination_may_be_modified;
} skcoop_bootstrap_status;

typedef struct skcoop_bootstrap_run_result {
    uint32_t struct_size;
    DWORD host_error;
    DWORD process_id;
    DWORD process_exit_code;
    BOOL ready_signaled;
    BOOL process_observed;
    BOOL timed_out;
    skcoop_bootstrap_status status;
} skcoop_bootstrap_run_result;

#if defined(SKCOOP_BOOTSTRAP_BUILD)
#define SKCOOP_BOOTSTRAP_API __declspec(dllexport)
#else
#define SKCOOP_BOOTSTRAP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Test-host library entry only; no executable/CLI or gameplay mode. The EXE
// basename and the DLL's fixture marker check reject arbitrary target programs.
BOOL skcoop_run_bootstrap_fixture(const WCHAR* fixture, const WCHAR* bootstrap,
    const WCHAR* controlled_root, uint32_t test_flags, DWORD timeout_ms,
    skcoop_bootstrap_run_result* output);

SKCOOP_BOOTSTRAP_API BOOL skcoop_bootstrap_fixture_path(WCHAR* output, DWORD capacity);
SKCOOP_BOOTSTRAP_API BOOL skcoop_bootstrap_fixture_checkpoint(DWORD checkpoint, DWORD observed_error);
SKCOOP_BOOTSTRAP_API DWORD skcoop_bootstrap_fixture_mode(void);

typedef BOOL (*skcoop_bootstrap_fixture_path_fn)(WCHAR*, DWORD);
typedef BOOL (*skcoop_bootstrap_fixture_checkpoint_fn)(DWORD, DWORD);
typedef DWORD (*skcoop_bootstrap_fixture_mode_fn)(void);

#ifdef __cplusplus
}
#endif
