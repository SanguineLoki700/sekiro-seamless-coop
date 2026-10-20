#!/usr/bin/env python3
"""Validate the v1 acceptance ledger and render its public views (stdlib only)."""

from __future__ import annotations

import argparse
import datetime
import html
import json
import re
import sys
from pathlib import Path
from urllib.parse import quote


START = "<!-- progress:start -->"
END = "<!-- progress:end -->"
BASELINE_ID = "v1-campanha-4-jogadores"
GROUP_WEIGHTS = {
    "core": 10, "actors": 20, "combat": 25, "steam": 10,
    "continuity": 15, "campaign": 15, "release": 5,
}
# Versioned acceptance contract, independent of the editable ledger. Tuples are
# (weight, minimum real players, minimum distinct machines). A new baseline must
# deliberately revise this validator/schema contract and its documented scope.
BASELINE_CRITERIA = {
    "core": {
        "core-launcher": (3, 1, 1),
        "core-native-runtime": (3, 1, 1),
        "core-save-isolation": (4, 1, 1),
    },
    "actors": {
        "actors-spawn": (6, 2, 2),
        "actors-movement": (6, 2, 2),
        "actors-traversal": (4, 2, 2),
        "actors-four-players": (4, 4, 4),
    },
    "combat": {
        "combat-damage-ai": (7, 2, 2),
        "combat-posture-deflect": (6, 2, 2),
        "combat-executions": (6, 2, 2),
        "combat-boss-phases": (6, 4, 4),
    },
    "steam": {
        "steam-lobby-invites": (4, 2, 2),
        "steam-transport": (3, 2, 2),
        "steam-reconnect": (3, 4, 4),
    },
    "continuity": {
        "continuity-death": (5, 4, 4),
        "continuity-travel": (5, 4, 4),
        "continuity-checkpoints": (5, 2, 2),
    },
    "campaign": {
        "campaign-progression": (5, 4, 4),
        "campaign-branches": (5, 4, 4),
        "campaign-special-bosses": (5, 4, 4),
    },
    "release": {
        "release-endurance": (3, 4, 4),
        "release-package": (2, 1, 1),
    },
}
STATES = {"planned", "in_progress", "blocked", "verified"}
PREPARATION_STATES = {"planned", "in_progress", "blocked", "ready"}
STATE_LABELS = {
    "planned": "Planejado", "in_progress": "Em andamento",
    "blocked": "Bloqueado", "verified": "Verificado", "ready": "Pronto",
}


class ProgressError(ValueError):
    """A ledger or generated view cannot be accepted."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProgressError(message)


def nonempty_string(value: object) -> bool:
    return isinstance(value, str) and bool(value.strip())


def positive_integer(value: object) -> bool:
    return type(value) is int and value > 0


def valid_hash(value: object, lengths: tuple[int, ...]) -> bool:
    return (
        isinstance(value, str)
        and len(value) in lengths
        and re.fullmatch(r"[0-9a-fA-F]+", value) is not None
        and any(char != "0" for char in value)
    )


def validate_report(root: Path, report: object, criterion_id: str) -> None:
    require(nonempty_string(report), f"{criterion_id}: report must name a local report")
    # Public evidence is committed beside the ledger, not an ephemeral URL or user path.
    require("\\" not in report and ":" not in report, f"{criterion_id}: report must be a repository-relative path")
    relative = Path(report)
    require(not relative.is_absolute() and ".." not in relative.parts,
            f"{criterion_id}: report escapes the repository")
    root = root.resolve()
    resolved = (root / relative).resolve()
    require(resolved.is_relative_to(root), f"{criterion_id}: report escapes the repository")
    require(resolved.is_file(), f"{criterion_id}: report does not exist: {report}")
    require(resolved.stat().st_size > 0, f"{criterion_id}: report is empty")


def validate_evidence(root: Path, criterion: dict) -> None:
    criterion_id = criterion["id"]
    evidence = criterion.get("evidence")
    require(isinstance(evidence, dict), f"{criterion_id}: verified requires evidence")
    require(valid_hash(evidence.get("commit"), (40, 64)),
            f"{criterion_id}: evidence requires a full Git commit hash")
    require(valid_hash(evidence.get("game_executable_sha256"), (64,)),
            f"{criterion_id}: evidence requires game executable SHA-256")
    for field in ("game_version", "scenario", "tested_on", "tested_by"):
        require(nonempty_string(evidence.get(field)), f"{criterion_id}: evidence requires {field}")
    require(re.fullmatch(r"\d{4}-\d{2}-\d{2}", evidence["tested_on"]) is not None,
            f"{criterion_id}: tested_on must use YYYY-MM-DD")
    try:
        datetime.date.fromisoformat(evidence["tested_on"])
    except ValueError as error:
        raise ProgressError(f"{criterion_id}: tested_on is not a valid date") from error
    participants = evidence.get("participants")
    require(isinstance(participants, dict), f"{criterion_id}: evidence requires participants")
    for field, minimum in (("players", criterion["min_players"]),
                           ("machines", criterion["min_machines"])):
        value = participants.get(field)
        require(positive_integer(value) and value >= minimum,
                f"{criterion_id}: participants.{field} must be at least {minimum}")
    require(participants["machines"] <= participants["players"],
            f"{criterion_id}: machines cannot exceed players")
    require(evidence.get("result") == "passed", f"{criterion_id}: only passed evidence can be verified")
    validate_report(root, evidence.get("report"), criterion_id)


def validate(ledger: object, root: Path) -> dict:
    require(isinstance(ledger, dict), "progress.json must contain an object")
    require(type(ledger.get("schema_version")) is int and ledger["schema_version"] == 1,
            "unsupported schema_version")
    require(ledger.get("baseline") == BASELINE_ID,
            f"unsupported baseline; schema version 1 requires {BASELINE_ID}")
    groups = ledger.get("groups")
    require(isinstance(groups, list) and len(groups) == len(GROUP_WEIGHTS),
            "the baseline requires exactly seven groups")
    identifiers: set[str] = set()

    def identify(item: object) -> None:
        require(isinstance(item, dict), "each record must contain an object")
        identifier = item.get("id")
        require(isinstance(identifier, str) and re.fullmatch(r"[a-z][a-z0-9-]*", identifier) is not None,
                "IDs must use lowercase letters, digits and hyphens")
        require(identifier not in identifiers, f"duplicate ID: {identifier}")
        identifiers.add(identifier)
        require(nonempty_string(item.get("label")), f"{identifier}: label is required")

    seen_groups: set[str] = set()
    grand_total = 0
    for group in groups:
        identify(group)
        group_id = group["id"]
        require(group_id in GROUP_WEIGHTS, f"unknown baseline group: {group_id}")
        seen_groups.add(group_id)
        require(type(group.get("weight")) is int and group["weight"] == GROUP_WEIGHTS[group_id],
                f"{group_id}: group weight must remain {GROUP_WEIGHTS[group_id]}")
        criteria = group.get("criteria")
        require(isinstance(criteria, list) and bool(criteria), f"{group_id}: criteria are required")
        expected_criteria = BASELINE_CRITERIA[group_id]
        seen_criteria: set[str] = set()
        group_total = 0
        for criterion in criteria:
            identify(criterion)
            criterion_id = criterion["id"]
            require(criterion_id in expected_criteria,
                    f"{group_id}: criterion ID {criterion_id} is not in this baseline group")
            seen_criteria.add(criterion_id)
            require(positive_integer(criterion.get("weight")), f"{criterion_id}: weight must be a positive integer")
            require(isinstance(criterion.get("status"), str) and criterion["status"] in STATES,
                    f"{criterion_id}: invalid functional status")
            require(nonempty_string(criterion.get("acceptance")), f"{criterion_id}: acceptance is required")
            require(positive_integer(criterion.get("min_players"))
                    and positive_integer(criterion.get("min_machines")),
                    f"{criterion_id}: minimum players and machines are required")
            actual_contract = (criterion["weight"], criterion["min_players"], criterion["min_machines"])
            require(actual_contract == expected_criteria[criterion_id],
                    f"{criterion_id}: baseline values must remain weight/min_players/min_machines="
                    f"{expected_criteria[criterion_id]}")
            require(criterion["min_machines"] <= criterion["min_players"],
                    f"{criterion_id}: minimum machines cannot exceed players")
            if criterion["status"] == "verified":
                validate_evidence(root, criterion)
            group_total += criterion["weight"]
        require(seen_criteria == set(expected_criteria),
                f"{group_id}: the complete baseline criterion ID set is required")
        require(group_total == group["weight"], f"{group_id}: criterion weights must sum to {group['weight']}")
        grand_total += group_total
    require(seen_groups == set(GROUP_WEIGHTS), "baseline group missing")
    require(grand_total == 100, "functional weights must sum to 100")
    preparation = ledger.get("preparation")
    require(isinstance(preparation, list), "preparation must be a list")
    for item in preparation:
        identify(item)
        require(isinstance(item.get("status"), str) and item["status"] in PREPARATION_STATES,
                f"{item['id']}: invalid preparation status")
        require(nonempty_string(item.get("note")), f"{item['id']}: preparation requires a factual note")
        require("weight" not in item, f"{item['id']}: preparation cannot carry functional weight")
    return ledger


def load(root: Path) -> dict:
    try:
        ledger = json.loads((root / "progress.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProgressError(f"cannot read progress.json: {error}") from error
    return validate(ledger, root)


def verified_points(group: dict) -> int:
    return sum(item["weight"] for item in group["criteria"] if item["status"] == "verified")


def percent(ledger: dict) -> int:
    return sum(verified_points(group) for group in ledger["groups"])


def cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\r", " ").replace("\n", " ")


def summary_table(ledger: dict) -> str:
    rows = ["| Funcionalidade | Verificado | Falta |", "|---|---:|---:|"]
    for group in ledger["groups"]:
        earned = verified_points(group)
        rows.append(f"| {cell(group['label'])} | {earned}/{group['weight']} pontos | {group['weight'] - earned} pontos |")
    rows.append(f"| **Total da v1** | **{percent(ledger)}%** | **{100 - percent(ledger)}%** |")
    return "\n".join(rows)


def readme_block(ledger: dict) -> str:
    return "\n".join([
        START,
        "![Progresso funcional verificado da v1](docs/progress.svg)", "",
        f"**{percent(ledger)}% verificado; {100 - percent(ledger)}% ainda não verificado.** "
        "Este número mede critérios aceitos da v1, não tempo ou esforço restante.", "",
        summary_table(ledger), "",
        "[Critérios, preparação e evidências](PROGRESS.md) · [Marcos e regras](docs/ROADMAP.md)",
        END,
    ])


def render_readme(root: Path, ledger: dict) -> str:
    try:
        original = (root / "README.md").read_text(encoding="utf-8")
    except OSError as error:
        raise ProgressError(f"cannot read README.md: {error}") from error
    require(original.count(START) == 1 and original.count(END) == 1,
            "README.md requires exactly one progress marker pair")
    start = original.index(START)
    end = original.index(END)
    require(start < end, "README.md progress markers are reversed")
    return original[:start] + readme_block(ledger) + original[end + len(END):]


def render_progress(ledger: dict) -> str:
    lines = [
        "# Progresso funcional da v1", "",
        "> Gerado de `progress.json` por `python tools/progress.py`. Não editar este arquivo à mão.", "",
        f"Baseline: **{cell(ledger['baseline'])}**. **{percent(ledger)}% verificado / {100 - percent(ledger)}% restante.**", "",
        "Somente critérios `verified` somam seus pontos. Trabalho em andamento e configuração de ferramentas "
        "não concedem pontos parciais. O percentual não estima prazo ou esforço.", "",
        "A label GitHub `status:validation` significa que o trabalho aguarda validação; no ledger permanece "
        "`in_progress`, com zero pontos até a aceitação como `verified`.", "",
        summary_table(ledger), "", "## Preparação (fora dos 100 pontos)", "",
        "| Preparação | Estado | Constatação / pendência |", "|---|---|---|",
    ]
    for item in ledger["preparation"]:
        lines.append(f"| {cell(item['label'])} | {STATE_LABELS[item['status']]} | {cell(item['note'])} |")
    lines.extend(["", "## Critérios e trabalho restante", ""])
    for group in ledger["groups"]:
        lines.extend([f"### {group['label']} — {verified_points(group)}/{group['weight']} pontos", ""])
        for item in group["criteria"]:
            checked = "x" if item["status"] == "verified" else " "
            lines.append(f"- [{checked}] **{item['id']}** — {item['label']} "
                         f"({item['weight']} pontos; {STATE_LABELS[item['status']]})")
            lines.append(f"  - Aceitação: {item['acceptance']}")
            lines.append(f"  - Validação mínima: {item['min_players']} jogador(es), {item['min_machines']} máquina(s).")
            if item["status"] == "verified":
                evidence = item["evidence"]
                lines.append(f"  - Evidência: [relatório]({quote(evidence['report'], safe='/')}); "
                             f"commit `{evidence['commit']}`; jogo {cell(evidence['game_version'])}; "
                             f"{cell(evidence['tested_on'])}; resultado `passed`.")
        lines.append("")
    lines.extend([
        "## Como aceitar ou reabrir um critério", "",
        "A alteração de estado entra por PR com evidência em arquivo versionado: commit completo, SHA-256 do "
        "executável do jogo, versão do jogo, cenário reproduzível, data, responsável, quantidade real de jogadores "
        "e máquinas e resultado `passed`. O relatório inclui condições de rede, passos e limitações.", "",
        "O validador verifica formato, quantidades mínimas e existência do relatório. Ele não comprova "
        "sozinho que uma sessão de Sekiro aconteceu; essa aceitação exige revisão humana da evidência.", "",
        "Issues fechadas, duplicadas ou canceladas não alteram o percentual. Regressões voltam a `blocked` e "
        "retiram os pontos. Mudança do escopo ou dos pesos requer revisão explícita da baseline; nunca retirar "
        "um requisito silenciosamente para aumentar a porcentagem.", "",
        "O validador fixa os IDs por grupo, o peso e os mínimos de jogadores e máquinas de cada critério da "
        "baseline v1. Alterar somente `progress.json` não redefine esses requisitos. Uma nova baseline exige "
        "revisão deliberada do contrato do validador/esquema e da documentação.", "",
        "Testes do jogo e de convites Steam ocorrem nas instalações dos participantes. CI pública compila o "
        "código e verifica testes independentes, mas não executa Sekiro nem recebe seus arquivos proprietários.", "",
    ])
    return "\n".join(lines)


def render_svg(ledger: dict) -> str:
    value = percent(ledger)
    label = html.escape(f"v1 verificada: {value}%")
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="208" height="24" role="img" '
        f'aria-label="{label}">\n'
        f'  <title>{label}</title>\n'
        '  <rect width="208" height="24" rx="4" fill="#273244"/>\n'
        '  <path d="M147 0h57a4 4 0 0 1 4 4v16a4 4 0 0 1-4 4h-57z" fill="#256f91"/>\n'
        '  <g fill="#fff" font-family="Verdana,Arial,sans-serif" font-size="11" text-anchor="middle">\n'
        '    <text x="73" y="16">v1 verificada</text>\n'
        f'    <text x="177" y="16">{value}%</text>\n'
        '  </g>\n'
        '</svg>\n'
    )


def generated_files(root: Path, ledger: dict) -> dict[Path, str]:
    return {
        root / "README.md": render_readme(root, ledger),
        root / "PROGRESS.md": render_progress(ledger),
        root / "docs" / "progress.svg": render_svg(ledger),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root (also useful for isolated tests)")
    parser.add_argument("--check", action="store_true", help="validate without writing anything")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        ledger = load(root)
        outputs = generated_files(root, ledger)
        stale = [path for path, expected in outputs.items()
                 if not path.is_file() or path.read_text(encoding="utf-8") != expected]
        if args.check:
            if stale:
                names = ", ".join(str(path.relative_to(root)) for path in stale)
                raise ProgressError(f"generated views are stale: {names}; run python tools/progress.py")
        else:
            for path, content in outputs.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8", newline="\n")
        print(f"Progress valid: {percent(ledger)}% verified; {100 - percent(ledger)}% remaining.")
        return 0
    except (ProgressError, OSError) as error:
        print(f"Progress error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
