from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPOSITORY_ROOT / "scripts" / "generate_gameplay_behavior_bundle.py"


def generate(output: Path, revision: Path, sources: list[Path]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(GENERATOR),
            "--output",
            str(output),
            "--revision-output",
            str(revision),
            "--module-id",
            "game.tests",
            "--fingerprint",
            "test-fingerprint",
            *(str(source) for source in sources),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def test_generation_is_deterministic_across_source_order(tmp_path: Path) -> None:
    first = tmp_path / "First.cpp"
    second = tmp_path / "Second.cpp"
    first.write_text('HORO_BEHAVIOR(FirstBehavior, "game.tests.first")\n', encoding="utf-8")
    second.write_text('HORO_BEHAVIOR(SecondBehavior, "game.tests.second")\n', encoding="utf-8")

    first_output = tmp_path / "bundle-first.cpp"
    first_revision = tmp_path / "first.revision"
    second_output = tmp_path / "bundle-second.cpp"
    second_revision = tmp_path / "second.revision"
    assert generate(first_output, first_revision, [second, first]).returncode == 0
    assert generate(second_output, second_revision, [first, second]).returncode == 0

    assert first_output.read_bytes() == second_output.read_bytes()
    assert first_revision.read_bytes() == second_revision.read_bytes()
    assert int(first_revision.read_text(encoding="utf-8")) > 0


def test_generation_ignores_annotation_text_in_comments_and_strings(tmp_path: Path) -> None:
    source = tmp_path / "Behavior.cpp"
    source.write_text(
        '''
        // HORO_BEHAVIOR(CommentBehavior, "game.tests.comment")
        const char *example = "HORO_BEHAVIOR(StringBehavior, game.tests.string)";
        HORO_BEHAVIOR(RealBehavior, "game.tests.real")
        ''',
        encoding="utf-8",
    )
    output = tmp_path / "bundle.cpp"
    result = generate(output, tmp_path / "bundle.revision", [source])

    assert result.returncode == 0, result.stderr
    generated = output.read_text(encoding="utf-8")
    assert "HoroGeneratedBehaviorDescriptor_RealBehavior" in generated
    assert "CommentBehavior" not in generated
    assert "StringBehavior" not in generated


def test_generation_rejects_duplicate_stable_type_ids(tmp_path: Path) -> None:
    source = tmp_path / "Duplicate.cpp"
    source.write_text(
        '''
        HORO_BEHAVIOR(FirstBehavior, "game.tests.duplicate")
        HORO_BEHAVIOR(SecondBehavior, "game.tests.duplicate")
        ''',
        encoding="utf-8",
    )
    result = generate(tmp_path / "bundle.cpp", tmp_path / "bundle.revision", [source])

    assert result.returncode == 1
    assert "duplicate native behavior type ID" in result.stderr
    assert not (tmp_path / "bundle.cpp").exists()
