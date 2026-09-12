import importlib.util
import json
import sys
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "scripts" / "scaffold_extension.py"
SPEC = importlib.util.spec_from_file_location("horo_extension_scaffolder", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
SCAFFOLDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = SCAFFOLDER
SPEC.loader.exec_module(SCAFFOLDER)


def run_scaffolder(monkeypatch, output: Path, shape: str, package_id: str = "com.example.tools"):
    monkeypatch.setattr(
        sys,
        "argv",
        [str(SCRIPT), "--shape", shape, "--id", package_id,
         "--name", "Example Tools", "--output", str(output)],
    )
    return SCAFFOLDER.main()


def test_each_shape_is_portable_and_complete(monkeypatch, tmp_path):
    expected_modules = {"gui": 1, "backend": 1, "script": 1, "hybrid": 3}
    for shape, module_count in expected_modules.items():
        output = tmp_path / shape
        assert run_scaffolder(monkeypatch, output, shape) == 0
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


def test_hybrid_keeps_backend_authority_separate(monkeypatch, tmp_path):
    output = tmp_path / "hybrid"
    assert run_scaffolder(monkeypatch, output, "hybrid") == 0
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


def test_invalid_identity_does_not_publish_partial_output(monkeypatch, tmp_path):
    output = tmp_path / "invalid"
    assert run_scaffolder(monkeypatch, output, "backend", "Invalid Id") == 2
    assert not output.exists()
