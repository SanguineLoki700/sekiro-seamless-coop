"""Build a clean owner test ZIP from an explicit allowlist, never private state."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def package(binary_dir: Path, output: Path) -> None:
    entries = {
        "SekiroCoopTeste.exe": binary_dir / "skcoop_lan_check.exe",
        "SeamlessCoop/skcoop_launcher.exe": binary_dir / "skcoop_launcher.exe",
        "SeamlessCoop/skcoop_runtime.dll": binary_dir / "skcoop_runtime.dll",
        "LEIA-ME.txt": ROOT / "docs" / "LAN_KIT.txt",
        "LICENSE.txt": ROOT / "LICENSE",
        "SeamlessCoop/licenses/Detours.txt": ROOT / "third_party" / "detours" / "LICENSE.md",
    }
    contents = {name: path.read_bytes() for name, path in entries.items()}
    manifest = {
        "kind": "owner-lan-movement-prototype",
        "playable_coop_release": False,
        "launch_protocol": 9,
        "lan_protocol": 1,
        "game": "Sekiro Steam build 5794815 / 1.06",
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "files": {name: hashlib.sha256(data).hexdigest() for name, data in contents.items()},
    }
    contents["SeamlessCoop/manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in contents.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(output) as archive:
        assert set(archive.namelist()) == set(contents)
        assert archive.testzip() is None
    print(f"Created {output.name}: {len(contents)} allowlisted files; no saves, room keys or local configs.")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bin", type=Path, default=ROOT / "build/msvc/bin/Release")
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    package(args.bin, args.out)
