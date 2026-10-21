# Windows diagnostic build

Version `0.0.1-bootstrap` is a diagnostic scaffold, not a playable co-op release. It does not start or
inject into Sekiro, intercept a save, install native game hooks, or implement
multiplayer. `--launch` always refuses. The launcher and runtime are our own code;
no DLL or launcher from another mod is bundled.

## Requirements and build

- Windows x64, Visual Studio 2022 Build Tools with the C++ workload and Windows SDK.
- CMake 3.25 or newer for the version-6 presets (the project itself needs 3.24).
- Run in this repository. No changes to a game installation are needed.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Release equivalents are `windows-release` for build/test. Binaries are under
`build/msvc/bin/Debug` or `Release`. MSVC's runtime library is linked statically.
The core tests register themselves through `src/core`. CTest also registers
Windows runtime smoke tests, probe policy tests and save-path policy tests.
The save policy runs only against controlled temporary fixtures; it does not
install a game hook. See [SAVES.md](SAVES.md).

The build copies `config/save_profile.ini` beside `game_profiles.ini`. Its
`[SAVE] save_file_extension=co2` records the intended namespace; an INI reader
and native save interception are not implemented by that file. Gameplay flags
remain disabled. Wire protocol 2 now requires an explicit nonzero session ID
for every encoded or decoded envelope; see [PROTOCOL.md](PROTOCOL.md).

## Available commands

```powershell
.\build\msvc\bin\Debug\skcoop_launcher.exe --self-test
.\build\msvc\bin\Debug\skcoop_launcher.exe --diagnose
.\build\msvc\bin\Debug\skcoop_launcher.exe --inspect "C:\path\to\sekiro.exe"
```

`--self-test` starts only our own smoke-test executable. That test uses an owned
temporary directory, synthetic PE fixtures, and a controlled child that loads our
runtime DLL from a path with spaces and Unicode. It checks the SHA-256 known
vector, malformed/32-bit/managed/DLL rejection, strict profile parsing, launch
refusal even if profile flags are set, and explicit ABI initialization/shutdown.
The temporary fixtures are never executed. Only our diagnostic programs run.
If built with `BUILD_TESTING=OFF`, `--self-test` is unavailable because its helper
is not built; use `--diagnose` instead.

`--inspect` opens the selected file read-only, checks native AMD64 PE32+ headers,
and hashes the full file using Windows BCrypt. Header inspection and hashing use
the same open handle, which disallows concurrent writers/deletion. It reports
the exact digest and whether a verified profile matches. This is header triage,
not proof that an executable is safe or that Windows can run it. It loads no DLL,
does not start the game, and does not infer compatibility from a filename/version.

`--diagnose` loads our sibling `skcoop_runtime.dll` into the launcher itself and
explicitly initializes diagnostic mode after `LoadLibraryExW` returns. An explicit
`--runtime <absolute-or-relative-dll>` may select a different build of our runtime.
This executes the selected library; use this option only for a trusted local
build. It is not an arbitrary-game inspection option. DLL dependencies use the
DLL directory and default Windows loader directories, not the current directory.

## Profiles and refusal behavior

The bundled `config/game_profiles.ini` is an allowlist. Its measured Steam
build-5794815 candidate has both verification flags **false**. There are no
approved builds. Only `[meta] format_version=1` and `[profile:<id>]` sections with
exactly these four keys are accepted:

```ini
sha256=<exact 64-character lowercase SHA-256 measured from the executable>
pe_machine=amd64
native_hooks_verified=false
saves_verified=false
```

The example is documentation, not a usable profile. Wildcards, missing/unknown
keys, duplicate keys/sections/hashes, and malformed Boolean values are rejected.
An executable matches only its exact hash with **both** flags true. Those flags
are evidence recorded by a developer, not capabilities created by configuration.
Changing them cannot enable gameplay in this scaffold.
The `skcoop_prepare_config` build target copies this configuration with
`copy_if_different` on every build, including builds of `skcoop_launcher` alone;
editing only the INI does not require relinking the launcher to update its copy.

`--inspect <exe> --profiles <ini>` selects a research allowlist explicitly.
`--launch <exe> [--profiles <ini>]` performs the same read-only preflight, then
returns 20 if no verified profile matches, or 21 if one matches but the gameplay
bootstrap is still unimplemented. It never calls a game launcher or creates a
game process. Exit 0 means successful diagnostics/inspection, 1 is an operational
or input error, and 2 is an unknown command. No DRM/Arxan bypass or AppID spoofing
is implemented.

## Runtime ABI v1

`include/skcoop/runtime_api.h` defines five C exports: ABI version, health,
capabilities, explicit initialization, and shutdown. Callers set exact
`struct_size`, the matching ABI version, and zero reserved fields. Initialization
in diagnostic mode and shutdown are idempotent. Gameplay initialization returns
`SKCOOP_UNIMPLEMENTED` without changing runtime state. Capabilities report only
diagnostics: native hooks, save isolation, and multiplayer are all unimplemented.

`DllMain` only records its module handle. It does
not start threads, open files, initialize networking, install hooks, or touch
game memory. The future gameplay bootstrap must remain separate from this entry
point and verify native hooks plus save isolation before any game thread resumes.
Remote injection, save redirection, game native bindings and a playable remote
actor remain future work, requiring separate evidence and real two-PC tests.

## Separate read-only research probe

`skcoop_probe` is independent of the runtime DLL and `--launch`. It does not
initialize gameplay or change its capabilities. Use it only against the owner's
already-running Steam Sekiro executable:

```powershell
.\build\msvc\bin\Debug\skcoop_probe.exe --exe "C:\path\to\sekiro.exe"
```

Admission is compiled to the exact measured AMD64 executable: 68,005,144 bytes,
SHA-256 `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
The allowlist flags cannot broaden this research target. Wrong executables are
refused before process enumeration. The selected process must have the same
resolved image path; ambiguous or inaccessible candidate identities are refused
without elevation. Lifetime and native AMD64 architecture are rechecked before
reading. Only the selected process receives `PROCESS_QUERY_INFORMATION |
PROCESS_VM_READ` access; candidate discovery uses limited query permission.

The research candidate comes from a `WorldChrMan` cross-reference in the
owner-provided Sekiro Online binary, REA evidence
`ev_956dbc87d51c7a272331f8961a302c83985623127fc8710186c6b35f4fb23186`:
observed global VA `0x143d7a1e0` minus game preferred base `0x140000000` gives
RVA `0x3d7a1e0`. This is a hypothesis about the game, not a validated native
binding. Root static inspection located it in `.data` inside image size
`0x42d2000`, without file-backed bytes. No other project's pointer chain is used.

The probe verifies the main module path and exact expected image size, applies
the runtime module base to that fixed RVA, checks that all eight bytes fit, and
performs **one explicit eight-byte `ReadProcessMemory` call by default**. Default
mode does not follow the resulting value. If nonzero, `VirtualQueryEx` reports only whether that
address belongs to a committed region with read-permitting page flags. In default
mode, no bytes from that pointed-to region are read. Module inventory APIs
retrieve Windows module metadata; no general memory scanner or unbounded
pointer-chain traversal exists.

Output stays on the local console: selected PID and Boolean observations, with
no heap pointer, contents, player data, save access, or telemetry file. A permitted
page flag is metadata, not proof of a successful read of that region. The disk
hash is not a hash of loaded memory; a successful slot read neither validates
the structure's meaning/lifetime nor creates a remote actor. Hooks, save isolation
and multiplayer remain unimplemented. A zero slot is a valid limited observation,
not a reason to scan or try another address.

An optional `--rtti` suffix enables a strictly bounded MSVC x64 RTTI observation:

```powershell
.\build\msvc\bin\Debug\skcoop_probe.exe --exe "C:\path\to\sekiro.exe" --rtti
```

Including the original slot, this mode attempts at most **five explicit reads**:
8 bytes for the candidate slot, 8 for the object's first field, 8 for the vtable's
locator pointer, 24 for the Complete Object Locator (COL), and 144 for the type
descriptor's 16-byte header plus a 128-byte name window. There are no scans,
retries at alternative addresses, function calls into the game, or player-data
reads. The object field must fit completely in the single committed/readable
region already queried. Both the vtable and its preceding locator slot must lie
inside the main image; the COL and full type-descriptor/name window must also
fit. The COL must have signature 1 and an image-relative self field that resolves
exactly back to its own address. Every read must return its complete requested
size; a structural failure stops RTTI observation with `rtti_unavailable: true`.

Only a NUL-terminated printable ASCII name within the 128-byte window, beginning
with `.?AV` or `.?AU`, is printed as `rtti_type_name`. No heap pointer or other
contents are displayed. This is software type metadata, not confirmation of a
specific manager, object layout, object lifetime, hook, or remote actor. The
default mode and all gameplay capabilities are unchanged. The optional result
does not change the exit code of an otherwise complete candidate-slot read.

Probe exit codes: 0 observation complete (possibly zero/unreadable pointed region),
2 usage, 10 rejected executable, 11 no matching process, 12 ambiguous process,
13 access/identity unavailable, 14 module/architecture/bounds mismatch, 15 incomplete
slot read, and 1 other local observation failure. It never requests write,
memory-operation, thread, debug or termination rights; never launches/injects into
the game; and never retries with broader access.

`probe_policy_tests` tests exact hash admission, RVA boundaries/overflow, page
flags, COL signature/self/RVA limits, vendor-name prefix/termination, and refusal
of the probe's own executable in both modes. It never opens Sekiro. These
tests do not claim a fake process validates game internals; real observations,
when authorized separately, retain the limitations above.
