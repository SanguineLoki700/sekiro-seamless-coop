# Project instructions

This repository develops original four-player seamless co-op for Sekiro on
Windows/Steam. Follow the accepted roadmap and public acceptance criteria.

## Evidence and scope

- Do not describe this bootstrap as a playable multiplayer mod.
- Do not credit installed tools, scaffolding, simulations, commits, or closed
  issues as verified in-game functionality.
- Never invent native addresses, AOB signatures, supported game hashes,
  actor control, save redirection, or Steam networking support.
- Keep game binaries, saves, proprietary mod files, decompiled listings,
  credentials, local paths, and unsanitized logs under ignored `local/`.
- Research closed mods as behavior/interface references; do not copy their
  implementation or assets into this MIT project.
- Native bindings require independent evidence from the selected target;
  unrelated prototype code or offsets are not a supported source.
- Do not disable DRM, alter Steam DLLs, or substitute an unrelated AppID.
- Gameplay launch remains disabled until native hooks and isolated saves are
  independently implemented and verified for the exact executable hash.

## Development

- C++20, MSVC x64, CMake, no mandatory external mod loader.
- Preserve the separation between launcher, injected runtime, portable core,
  platform transport, and campaign storage.
- The host owns enemies, durable world events, and combat consequences.
- Declared Steam IDs in tests are identifiers, not proof of authentication.
- Never run or install a newly compiled mod into the user's game directory
  merely to make a diagnostic pass.
- Use focused tests. Build with CMake and run CTest; run Python progress tests
  and `python tools/progress.py --check` before a pull request.
- Progress changes require an evidence record and regenerated documentation
  in the same pull request. Regressions revoke credit; scope weights total 100.
- A single analyst owns an active REA target/debugger session at a time.
