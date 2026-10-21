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

## Primary references

- [REA](https://github.com/morluto/rea)
- [Co-op Anything](https://github.com/cranebeaker/coop-anything)
- [DS1 Seamless](https://www.nexusmods.com/darksoulsremastered/mods/899)
- [DS2 Seamless](https://www.nexusmods.com/darksouls2/mods/1468)
- [DS3 Seamless](https://www.nexusmods.com/darksouls3/mods/1895)
- [Sekiro Online](https://www.nexusmods.com/sekiro/mods/577)

These references guide independent investigation. They do not establish a
shared reusable engine or authorize redistribution of another mod's files.
