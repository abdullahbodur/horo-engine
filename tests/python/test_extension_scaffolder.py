import json
import subprocess  # nosec B404 -- required to exercise the shipped CLI as a separate process.
import sys
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "scripts" / "scaffold_extension.py"


def run_scaffolder(output: Path, shape: str, package_id: str = "com.example.tools"):
    # The executable and script are fixed, shell=False is implicit, and every value is one argv element.
    # nosec B603
    return subprocess.run(  # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit
        [sys.executable, str(SCRIPT), "--shape", shape, "--id", package_id,
         "--name", "Example Tools", "--output", str(output)],  # nosec B603
        check=False,
        capture_output=True,
        text=True,
    )


def test_each_shape_is_portable_and_complete(tmp_path):
    expected_modules = {"gui": 1, "backend": 1, "script": 1, "hybrid": 3}
    for shape, module_count in expected_modules.items():
        output = tmp_path / shape
        result = run_scaffolder(output, shape)
        assert result.returncode == 0, result.stdout + result.stderr
        manifest = json.loads((output / "extension.json").read_text(encoding="utf-8"))
        assert len(manifest["modules"]) == module_count
        assert len(list((output / "src").glob("*.c"))) == module_count
        assert len(list((output / "tests").glob("*.c"))) == module_count
        generated = "\n".join(
            path.read_text(encoding="utf-8") for path in output.rglob("*") if path.is_file()
        )
        assert str(SCRIPT.parents[1]) not in generated
        assert "HoroEngine::ExtensionSdk" in generated
        assert "include(CPack)" in generated
        assert "Horo/Extensions/ExtensionAbi.h" in generated


def test_hybrid_keeps_backend_authority_separate(tmp_path):
    output = tmp_path / "hybrid"
    assert run_scaffolder(output, "hybrid").returncode == 0
    modules = json.loads((output / "extension.json").read_text(encoding="utf-8"))["modules"]
    by_id = {module["id"]: module for module in modules}
    backend = by_id["com.example.tools.backend"]
    assert backend["roles"] == ["backend-capability", "headless-tooling"]
    assert "exports" in backend
    for suffix in ("editor", "script"):
        adapter = by_id[f"com.example.tools.{suffix}"]
        assert adapter["dependencies"] == [backend["id"]]
        assert adapter["imports"][0]["service"] == backend["exports"][0]["id"]
        assert "backend-capability" not in adapter["roles"]


def test_invalid_identity_does_not_publish_partial_output(tmp_path):
    output = tmp_path / "invalid"
    result = run_scaffolder(output, "backend", "Invalid Id")
    assert result.returncode == 2
    assert not output.exists()
