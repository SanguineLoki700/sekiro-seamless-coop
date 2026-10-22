# Research baseline

Research is preparation. None of the observations below proves playable
multiplayer in this repository.

## Target identified locally on 2026-10-20

| Property | Observed value |
| --- | --- |
| Game | Sekiro: Shadows Die Twice, Steam |
| Steam application | 814380 |
| Steam build | 5794815 |
| Executable file/product version | 1.6.0.0 |
| Architecture | PE32+, AMD64 |
| Executable size | 68,005,144 bytes |
| SHA-256 | `637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856` |

This identity is a research candidate, **not an enabled gameplay profile**.
The local installation also contains graphics modifications; compatibility
with those modifications has not been tested. No game file was modified.
The engine heuristic did not identify FromSoftware's proprietary engine;
that heuristic result is not evidence against the known target identity.

## Seamless package comparison

The supplied DS1 0.0.4, DS2 0.0.5, and DS3 0.1.1 packages contain a native
x64 launcher, native DLL, INI settings, localization, and crash reporting.
They do not distribute me3 as a dependency. DS1/DS3 also expose a named
Mod Engine initialization export; that does not by itself establish me3
compatibility or the internal multiplayer architecture.

| Supplied package | SHA-256 |
| --- | --- |
| DS1 Seamless 0.0.4 | `5922a14bc958bf0a899167460ec38c79acb904640dcd2d41a0504342ce53e144` |
| DS2 Seamless 0.0.5 | `4e16ee411ca18e876e4d609ecb1d7ca60c433741272f2956d0a530ccac129732` |
| DS3 Seamless 0.1.1 | `da7d7d423a0f5e0133b0ed3bbd282049d41f02ce657b36c937febc11c72c8580` |

REA/Ghidra static analysis of the DS3 launcher observed process creation and
DLL loading. This was not a runtime test of Dark Souls III. References:

- inventory: `ev_46eedc616b9f6f2a11caa0e49f45ca4e401eef6ded6aa636286436d58408eb48`
- initialization: `ev_dab08e7ae120bf75a7ff7039fb6a5b709dc8f3c939193416f8a818c7710fed7a`
- loader: `ev_e0271524e6db8077436a344d1134266b6a9a47b36d432421ecc0d47c9ae55b35`

No proprietary source, reconstructed listings, mod binaries, or assets are
included in this repository. C++20/CMake/MSVC are this project's own choices,
not a claim about Yui's original build configuration.

## Sekiro Online 2.05 inspected on 2026-10-20

The supplied archive was recovered and inspected with REA; its SHA-256 is
`be6ef9f61b73d7faa8c2e78ecf35808fde45ebaf1fc2a5240198dbf99ad311cc`.
Its native `dinput8.dll` has SHA-256
`b40dd9c34b4192f546e65b2306959ffe276af2991d551b2fabc46cc0bda7dad1`.
Ghidra 12.1.4 completed analysis and inventoried 1,162 procedures and 479
defined strings. This is static coverage, not a successful execution test.

Observed references include the DirectInput proxy export, Steam callbacks,
`WorldChrMan`, `NetPlayerMemoryManager`, and remote-player type metadata.
Steam interface strings indicate investigation candidates; they do not prove
that the new runtime supports those interfaces or multiplayer.

A `WorldChrMan` string reference leads to a native access to preferred image
address `0x143d7a1e0`. Relative to the game's preferred base `0x140000000`,
the resulting research candidate is RVA `0x03d7a1e0`. Inspection of the exact
target PE places that address inside `.data` without corresponding raw file
bytes. It therefore requires an explicitly read-only live probe; an on-disk
zero is not evidence that the manager is absent. The source of this candidate
is this supplied binary and its recorded REA evidence.

Native hook signatures, actor creation/control, timing, save isolation, and
combat remain unverified. No gameplay profile is enabled by this finding.

REA references:

- procedure inventory: `ev_37e67fe03ea26a89bf4ac1337cd24840f8c0a7eee027c15db7667aac0e03892f`
- strings: `ev_24fcc673aaafefc556f1caff365466257acdff6ca6b2195db8d89d5e8bb80271`
- manager reference: `ev_bc3f63001812ff221fda9b750904338d94fa12a08954697699ec11a443d6d93d`
- containing procedure: `ev_956dbc87d51c7a272331f8961a302c83985623127fc8710186c6b35f4fb23186`

### Limited live observation

The compiled `skcoop_probe` was run against the already-running target on
2026-10-20. Exact disk hash, native AMD64 process, main module identity,
expected image size, slot bounds, and the complete eight-byte slot read
passed. The slot was nonzero; Windows reported its pointed-to region as
committed and read-permitting.

A second, explicitly bounded RTTI observation reported software type name
`.?AVWorldChrManImp@NS_SPRJ@@`. This is consistent with the manager candidate
found in the supplied binary. The probe checked the main-image vtable and
MSVC x64 complete-object-locator metadata before emitting the type name.

These observations do not validate object layout, lifetime, actor fields,
native calls, or the identity of loaded executable bytes. They do not
demonstrate actor creation. The probes did not write process memory, install
hooks, inject a DLL, launch the game, or access saves. Process IDs and heap
addresses are omitted from this public record. No functional progress is
credited.

## Reinstalled baseline checked on 2026-10-21

**Observed:** the owner reported reinstalling Sekiro. The current executable has
the same SHA-256, byte size, AMD64 architecture and file/product version 1.6.0.0
listed above. The Steam manifest still reports build 5794815. Co-op Anything's
identity query and REA's PE-resource parser independently agreed on the file
digest. REA evidence: `ev_5f2d2d495a05dc21bf4e1217394e5476b8624b08c97a45cf4c079475a8a4c47d`.

The current root inventory did not contain the checked graphics/proxy/mod
markers, including `dinput8.dll`, `dxgi.dll`, `d3d11.dll`, `ReShade.ini`,
`reshade-shaders`, and `mods`. This is a bounded inventory, not verification
of every file against the Steam depot manifests. A separate private copy of
the executable was made for static analysis and verified against the digest;
it is not a complete runnable test installation.

**Derived:** static research for the identical disk executable remains a
candidate basis for further investigation. The 2026-10-20 live observations
describe the earlier installation/session, which contained graphics changes.
They have not been repeated against this reinstalled setup.

**Unknown:** current runtime object state, actor layout/lifetime, native
control functions, save isolation, and full-depot integrity. The game was
closed and no process named Sekiro was observed. This review did not launch
the game, access saves, inject code or modify game files. No gameplay profile
was enabled and no functional progress was credited.

The adopted research and QA practices are documented in [WORKFLOW.md](WORKFLOW.md).

## Save integration investigation, later on 2026-10-21

### Supplied Seamless settings

The supplied DS1 and DS2 archives were rechecked without executing their
binaries. `SeamlessCoop/ds1sc_settings.ini` lines 40-43 and
`SeamlessCoop/ds2sc_settings.ini` lines 62-65 define
`[SAVE] save_file_extension=co2`; their comments identify `.sl2` as the normal
extension and allow alphanumeric extensions up to 120 characters. The INI
SHA-256 values are `734845bc1b95a14354ff931116adab1077522064035f4ef5b1f6709cb21c36da`
and `d454bcb143274d7bd9aeca3bf39e9e9bdc8319d6b5822db18d725bb67e73f31f`.

This establishes a configuration contract, not the proprietary interception,
backup, Cloud or profile implementation. The DS3 archive was not available
for this repeat inspection; earlier DS3 launcher observations remain separate.
The original save-policy module adopts `.co2` as its default namespace, with
no automatic solo import. See [SAVES.md](SAVES.md).

### Loaded-code observations

The original game was started through Steam without installing a mod. A
separate read-only helper checked the exact disk identity and selected image
path, then read bounded main-image code and exception-table regions with
`PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`. Only selected instruction
excerpts and metadata were retained privately, not a complete memory image.

The disk `.text` region has approximately 8.0 bits/byte entropy; its bytes
differed from the mapped `.text` region. Direct disk-code analysis was therefore
not used to validate a save binding. The owned headless request was cancelled
and its REA target closed. No DRM modification or alternate executable was made.

Capstone 5.0.7 decoded loaded-code excerpts selected by RIP-relative string
references and exception/unwind ranges. These are code observations, **not
traces showing that each path executed**. The ranges do not prove complete
function/CFG coverage.

| Candidate RVA | Observation in loaded instructions / matching metadata |
|---|---|
| `0x1151c60` | References `%s/Sekiro/%s/`; calls the `SHGetFolderPathW` IAT slot with `CSIDL_APPDATA`. The account-component helper is unresolved. |
| `0x1151dc0` | References `%s/Sekiro/` and the same AppData query. Both folder routines contain a `L"."` base-path fallback on query failure. |
| `0x1cbb910` | Constructor-like range writes a vtable identified by MSVC RTTI as `SaveLoad2::SLContentFormat`, then passes `.sl2` and length 4 to a string helper for a member at offset `0xd8`. |
| `0x1cc3760` | References `%s.bak` and calls `CopyFileW` with fail-if-exists set. An earlier virtual helper remains unresolved, so backup preservation/overwrite policy is unknown. |
| `0x1cc3c00`, `0x1cc4370` | Reference `%s\\%s%s.bak` and forward paths to internal wrappers whose full I/O behavior is unresolved. |

The `.sl2` literal is at RVA `0x2e4eee0` in the matching `.rdata`. The
`SLContentFormat` vtable candidate is at `0x2e4f1c8`; its MSVC locator has
signature 1 and a matching self-RVA. These facts identify research candidates,
not a safe hook timing, callable ABI, or sufficient set of redirected operations.

The observed folder and filename formats can produce mixed `/` and `\\`
separators. That finding corrected the original path policy before publication;
native-format and repeated-separator regression cases were added.

The compiled `skcoop_probe --rtti` was also repeated on the reinstalled running
game. Its exact-identity checks passed and it again reported
`.?AVWorldChrManImp@NS_SPRJ@@`. Object layout, lifetime and actor control remain
unverified.

The observation window included an owner-loaded save. Before/after hashes of
the normal save and backup differed; that window is excluded from preservation
validation. No mod was deployed, no save was restored or rewritten by our tools,
and no functional criterion is accepted from these observations.

## Save boundary follow-up, 2026-10-21

A subsequent original-game Steam launch supported four focused read-only
queries against the same exact executable hash. Each rechecked a retained
349-byte code anchor before interpreting new instructions. The new queries
read 3,239 bytes of selected code in total, plus bounded exception metadata
and repeated anchors; no whole-text rescan, heap/save-content inspection,
native calls, injection or hook installation was performed by those queries.
Private save backups were prepared separately before the original launch;
they are not evidence of mod save isolation or an in-game save test.

| Candidate RVA | Refined observation |
|---|---|
| `0x11b2b30` | Calls the `SteamUser` import at IAT RVA `0x292a140`, dispatches virtual offset `0x10`, and formats an eight-byte result. The matching static UTF-16 format at RVA `0x2bd0610` is `%I64d`. This supports Steam-interface-derived account formatting, not authenticated profile binding. |
| `0x1cc85b0` | A 48-byte explicit window exposes an argument-shuffling tail jump to `0x1cc8100`; absence from the unwind inventory did not mean absence of code. The complete window includes neighboring stubs/padding. |
| `0x1cc8100` | Opens a stream, requires an exact 64-byte header read, parses it, then seeks and reads a larger blob. A later call to `0x1a07620` has unresolved effects: the entire routine must not be assumed read-only or called experimentally from this evidence. |
| `0x1cbc180` | The observed leaf instructions obtain string data from an object at `this + 0xe0`. The retained 48-byte window ends inside a neighboring instruction, so it was excluded from the complete-byte replay. This is not a callable ABI contract. |
| `0x1a0e200`, `0x1a0e2a0` | Stream construction and opening. The open routine delegates through `0x1a0cf40` to a backend virtual slot at offset `0xa0`; it does not directly establish a `CreateFileW` boundary. |
| `0x2dcc620` | Static stream vtable whose MSVC locator self-RVA validates and whose type name is `.?AVDLFileInputStream@DLIO@@`. Slots `0x18`, `0x38`, `0x68` point to RVAs `0x1a0e3a0`, `0x1a0e570`, `0x1a0e600`, respectively. The concrete lower backend remains separate. |

The later path excerpts refine that separation: `0x1a0cf40` selects a pointer
from the path object, while `0x1a0d5e0` prepares a UTF-16 path and calls
`0x1a0cc30`. Its eventual true return is not evidence that a file was opened
successfully. The stream RTTI must not be substituted for the unknown backend
type or used to invent the latter's callable ABI.

### Focused Ghidra replay

REA/Ghidra 12.1.4 analyzed a private **synthetic analysis PE**, SHA-256
`7eacd6b54716237b4b8030a21c99b43271fb97dd2a41fda34e11dd1a9ceaafb2`.
It contains 23 complete, hash-checked retained code intervals at their original
RVAs and explicitly synthetic padding/export metadata. Incomplete or absent
intervals were excluded. It has no original imports, unwind, `.rdata` or whole
game image and was never executed. The final path-resolution reads happened
after its construction and are not part of this replay.

Retained dossier IDs: account formatter
`ev_90ce98bcd9c951eb58b82a6b916011de81aaf967f8566a31a673c59c491dfdfe`,
read-path candidate
`ev_a0622812c4bf252b9225ab0aaef40b585252f4cb0b4a397df43969b86c391ecc`,
and stream-open candidate
`ev_a75ca4f06e6373bfe5b9f96fc2a0e2979871fba6b006200ecf9c79ad0403d737`.
These are derived historical-code observations in a partial reconstructed
image, not original source or traces of execution. Inferred signatures,
missing callees, indirect dispatch and function coverage remain unresolved;
the read-path and stream-open dossiers report 35 and 7 unresolved calls.
The Evidence and analysis snapshot were retained privately and the owned
Ghidra session was closed.

The next observation at that point required actual save I/O and initialization
timing. The subsequent trace below supplies evidence for the former;
initialization timing remains unresolved. Normal-game tracing and controlled
fixtures remain separate from a modified-game preservation test.

## Executed save I/O observation, 2026-10-21

The original Steam game was observed for 55 seconds with REA 6.7.0 and
Frida 17.23.2, after independent process-path, lifetime, disk-hash and mapped
image-size checks. The executable SHA-256 remained
`637aca527538c0ec6e1f136c8ed66046e95dfbdbb1f51926e134d9916398b856`.
The owner confirmed one rest at an Idol during the active observation window.
That action does not identify an exact cause for every operation in the window.

An earlier ETW attempt was rejected as incomplete because process filtering
did not hold and events were lost; it supplies no save evidence. The replacement
Frida route was first verified against an owned process using synthetic files.
After repairing the missing official native Frida addon, fresh REA CLI and
programmatic sessions worked. The existing MCP process still cached the failed
import; a working CLI/provider session is not proof that this MCP connection
had recovered.

Temporary `Interceptor.attach` listeners observed KernelBase file APIs. They
did not modify arguments/results, invoke game functions or inspect save-content
buffers. Frida does install interception machinery in process memory; this was
instrumented research, not a memory-write-free query or an installed mod.
Only selected save basenames, operation metadata and main-image stack RVAs
were retained. Basenames do not attest the complete directory or account
profile. Sequence numbers record `onLeave` completion order; the trace has no
per-event thread ID or timestamp to establish cross-thread call ordering.
The host received `trace_ready` before requesting the owner's action.

The retained trace contains **162 operations**, each with a main-image stack:
60 `CreateFileW`, 60 `CloseHandle`, 24 `ReadFile`, six `WriteFile`, six
`DeleteFileW` and six `CopyFileW`. All observed API returns succeeded; the
read/write calls were synchronous. The observer ended at its duration limit
with zero callback/path-read errors, no message truncation and its own listeners
detached. Separate awaited REA cleanup reported no errors and released the
session. The game process remained running.

| Executed path | Evidence and scope |
|---|---|
| Read | Calls returning through `0x1cc822d` and `0x1cc830b` traversed the previously observed stream layer and read 64 and 768 bytes, respectively. These sizes do not establish the complete save format or mean that the entire save was read. |
| Write | Calls returning through `0x1cc744e` reached `WriteFile` via `0x1a147b7` / `0x1aead84`. Six writes reported either 1,048,592 or 393,232 bytes, three of each. The associated opens used `GENERIC_WRITE`, no sharing and `OPEN_ALWAYS`. File offsets, seeks and truncation were not observed. |
| Backup | In six completion groups, successful deletion of `S0000.sl2.bak` returned through `0x1cc38a5`, followed by a successful copy of `S0000.sl2` to the backup through `0x1cc38e6`. Write completion preceded deletion/copy in each group. This observes deletion and copying on an executed backup path; it does not establish all overwrite policies, crash-safe replacement or preservation of the version preceding the write. The trace did not capture `CopyFileW`'s third argument. |

Save Evidence:
`ev_632fa98d5462672ebdaf8c7d43252fa4cda0c5e45e900365f4865e7f46cd9b0e`.
Owned fixture Evidence for the same live-ready/cleanup path:
`ev_25a886294ed0826432cf0f2fb4598a92e21dc7b58a5d527099f21d51a5c78a4d`.
Raw artifacts and proprietary data remain private. Independent identity
receipts supplement the REA observation, which does not itself attest the
artifact hash in its Evidence subject.

Coverage is partial: only handles opened after attachment were tracked;
other API routes, pre-existing handles, offsets, asynchronous completions,
startup and alternate failure/restore paths remain unverified. `MoveFileW`
was unavailable at the selected module; no assumption of complete move/rename
coverage is made. A preceding idle trace with zero events proves neither
absence of save I/O nor isolation.

The next native-adapter gate is to verify initialization before any save use
and redirect existence/open/delete/copy/backup paths consistently while
preserving native codec, offsets and I/O semantics. An individual `WriteFile`
payload must not be treated as a complete save snapshot merely because our
independent storage accepts opaque complete blobs. The captured write sizes
alone do not prove an in-place update layout either.

No `.co2` interception, original-save preservation with the mod, Steam Cloud
behavior or gameplay criterion was verified. Functional progress stays **0%**.
See the [validation record](validation/native-save-observation-2026-10-21.md).

## Native save integration — 2026-10-22

The Windows adapter now returns real private file handles and redirects the observed metadata/open/delete/copy paths. An initial modified-game test failed with `FDP_System_Message(401171)`: scoped REA observation showed access-zero directory metadata opens refused with error 50. Supporting only these pinned directory metadata handles resolved the observed failure; data/write directory handles remain refused.

The owner then confirmed loading a copied private profile, resting, reloading a recovered game-produced backup, creating a fresh profile and returning to the original game after removing the temporary launcher option. Whole-file checks confirmed changed cooperative data, matching cooperative backups and unchanged personal saves before the final unmodified-game load. These tests used an independent installation and external read-only/nonsharing handles on the original saves. The public diagnostic launcher remains disabled for gameplay.

See [native I/O contracts](NATIVE_SAVE_IO.md) and the [acceptance record](validation/native-save-isolation-2026-10-22.md). That record accepts only the four-point save criterion; it does not establish multiplayer, Steam Cloud or an equivalent run without the external protection. Earlier observations above retain their historical scope.

## Primary references

- [REA](https://github.com/morluto/rea)
- [Co-op Anything](https://github.com/cranebeaker/coop-anything)
- [DS1 Seamless](https://www.nexusmods.com/darksoulsremastered/mods/899)
- [DS2 Seamless](https://www.nexusmods.com/darksouls2/mods/1468)
- [DS3 Seamless](https://www.nexusmods.com/darksouls3/mods/1895)
- [Sekiro Online](https://www.nexusmods.com/sekiro/mods/577)

These references guide independent investigation. They do not establish a
shared reusable engine or authorize redistribution of another mod's files.
