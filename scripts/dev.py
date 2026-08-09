#!/usr/bin/env python3
"""Small developer runner for configuring, building, and launching HoroEditor."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENV_FILE = REPOSITORY_ROOT / ".env.local"
DEFAULT_BUILD_DIRECTORY = REPOSITORY_ROOT / "build" / "dev-editor"
DEFAULT_OTLP_ENDPOINT = "http://127.0.0.1:4318"
DEFAULT_GRAFANA_URL = "http://127.0.0.1:3000"
GRAFANA_DASHBOARD_PATH = REPOSITORY_ROOT / "observability" / "grafana" / "dashboards" / "horo-editor.json"
GRAFANA_DASHBOARD_UID = "horo-editor-observability"
GRAFANA_SOURCE_TAG_PREFIX = "horo-source:"
_ENVIRONMENT_KEY = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


class ConfigurationError(ValueError):
    """Raised when the local developer configuration is invalid."""


@dataclass(frozen=True)
class Endpoint:
    """Validated OTLP base endpoint and its TCP reachability address."""

    url: str
    host: str
    port: int


@dataclass(frozen=True)
class DeveloperSettings:
    """Resolved settings consumed only by this developer runner."""

    otel_export_enabled: bool
    endpoint: Endpoint
    grafana_endpoint: Endpoint | None = None


def parse_dotenv(path: Path) -> dict[str, str]:
    """Parse the intentionally small, non-executable developer dotenv grammar."""
    if not path.exists():
        return {}

    values: dict[str, str] = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export ") or "=" not in line:
            raise ConfigurationError(f"{path}:{line_number}: expected KEY=value")
        key, raw_value = line.split("=", 1)
        if not _ENVIRONMENT_KEY.fullmatch(key) or key in values:
            raise ConfigurationError(f"{path}:{line_number}: invalid or duplicate environment key {key!r}")
        value = raw_value.strip()
        if "$(" in value or "${" in value or "`" in value:
            raise ConfigurationError(f"{path}:{line_number}: environment expansion and commands are not supported")
        if value.startswith(("\"", "'")):
            if len(value) < 2 or value[-1] != value[0]:
                raise ConfigurationError(f"{path}:{line_number}: unterminated quoted value")
            value = value[1:-1]
        elif any(character.isspace() for character in value):
            raise ConfigurationError(f"{path}:{line_number}: values containing whitespace must be quoted")
        values[key] = value
    return values


def parse_enabled(value: str, setting_name: str = "HORO_DEV_OTEL_EXPORT") -> bool:
    """Parse the explicit ON/OFF developer switch."""
    normalized = value.strip().upper()
    if normalized == "ON":
        return True
    if normalized == "OFF":
        return False
    raise ConfigurationError(f"{setting_name} must be ON or OFF")


def validate_endpoint(value: str) -> Endpoint:
    """Validate an OTLP HTTP base URL without sending protocol traffic.

    Left as explicit, hand-written checks on purpose: these encode security
    decisions (no plaintext http off localhost, no embedded credentials, no
    path/query/fragment smuggled into a "base" URL) that a generic URL
    parsing/validation library will not enforce for you. Swapping this for a
    dependency would trade a few lines for silently losing those checks.
    """
    if any(character.isspace() for character in value) or "?" in value or "#" in value:
        raise ConfigurationError("HORO_OTEL_ENDPOINT must not contain whitespace, a query, or a fragment")
    try:
        parsed = urlparse(value)
        port = parsed.port
    except ValueError as error:
        raise ConfigurationError(f"HORO_OTEL_ENDPOINT is invalid: {error}") from error

    if parsed.scheme not in {"http", "https"}:
        raise ConfigurationError("HORO_OTEL_ENDPOINT must use http or https")
    if parsed.hostname is None or port is None:
        raise ConfigurationError("HORO_OTEL_ENDPOINT must include a host and explicit port")
    if parsed.username is not None or parsed.password is not None:
        raise ConfigurationError("HORO_OTEL_ENDPOINT must not include credentials")
    if parsed.path not in {"", "/"} or parsed.params or parsed.query or parsed.fragment:
        raise ConfigurationError("HORO_OTEL_ENDPOINT must be a base URL without a path, query, or fragment")
    if parsed.scheme == "http" and parsed.hostname not in {"127.0.0.1", "localhost", "::1"}:
        raise ConfigurationError("plaintext HORO_OTEL_ENDPOINT is allowed only for localhost")

    return Endpoint(url=value.rstrip("/"), host=parsed.hostname, port=port)


def validate_grafana_url(value: str) -> Endpoint:
    """Validate a loopback-only Grafana base URL used for automatic imports."""
    if any(character.isspace() for character in value) or "?" in value or "#" in value:
        raise ConfigurationError("HORO_GRAFANA_URL must not contain whitespace, a query, or a fragment")
    try:
        parsed = urlparse(value)
        port = parsed.port
    except ValueError as error:
        raise ConfigurationError(f"HORO_GRAFANA_URL is invalid: {error}") from error

    if parsed.scheme not in {"http", "https"}:
        raise ConfigurationError("HORO_GRAFANA_URL must use http or https")
    if parsed.hostname not in {"127.0.0.1", "localhost", "::1"} or port is None:
        raise ConfigurationError("HORO_GRAFANA_URL must use a loopback host and explicit port")
    if parsed.username is not None or parsed.password is not None:
        raise ConfigurationError("HORO_GRAFANA_URL must not include credentials")
    if parsed.path not in {"", "/"} or parsed.params or parsed.query or parsed.fragment:
        raise ConfigurationError("HORO_GRAFANA_URL must be a base URL without a path, query, or fragment")
    return Endpoint(url=value.rstrip("/"), host=parsed.hostname, port=port)


def load_settings(path: Path = DEFAULT_ENV_FILE, environ: Mapping[str, str] | None = None) -> DeveloperSettings:
    """Resolve defaults, local configuration, then process-environment overrides."""
    process_environment = os.environ if environ is None else environ
    resolved = {
        "HORO_DEV_OTEL_EXPORT": "OFF",
        "HORO_OTEL_ENDPOINT": DEFAULT_OTLP_ENDPOINT,
        "HORO_DEV_GRAFANA_AUTO_IMPORT": "ON",
        "HORO_GRAFANA_URL": DEFAULT_GRAFANA_URL,
        **parse_dotenv(path),
        **process_environment,
    }
    export_enabled = parse_enabled(resolved["HORO_DEV_OTEL_EXPORT"])
    grafana_auto_import = parse_enabled(resolved["HORO_DEV_GRAFANA_AUTO_IMPORT"], "HORO_DEV_GRAFANA_AUTO_IMPORT")
    return DeveloperSettings(
        otel_export_enabled=export_enabled,
        endpoint=validate_endpoint(resolved["HORO_OTEL_ENDPOINT"] if export_enabled else DEFAULT_OTLP_ENDPOINT),
        grafana_endpoint=validate_grafana_url(resolved["HORO_GRAFANA_URL"]) if grafana_auto_import else None,
    )


def collector_is_reachable(endpoint: Endpoint, timeout_seconds: float = 0.5) -> bool:
    """Check only whether a TCP listener is reachable at the configured address."""
    try:
        with socket.create_connection((endpoint.host, endpoint.port), timeout=timeout_seconds):
            return True
    except OSError:
        return False


def _grafana_source_tag(path: Path) -> str:
    """Return the stable tag used to avoid importing an unchanged dashboard."""
    digest = hashlib.sha256(path.read_bytes()).hexdigest()[:16]
    return f"{GRAFANA_SOURCE_TAG_PREFIX}{digest}"


def _read_json(request: Request, timeout_seconds: float) -> dict[str, object]:
    """Execute one bounded Grafana API request and decode its JSON response."""
    with urlopen(request, timeout=timeout_seconds) as response:
        return json.loads(response.read().decode("utf-8"))


def sync_grafana_dashboard(endpoint: Endpoint, dashboard_path: Path = GRAFANA_DASHBOARD_PATH) -> str:
    """Import the versioned dashboard when local Grafana is available and stale."""
    try:
        _read_json(Request(f"{endpoint.url}/api/health"), 0.5)
    except (HTTPError, URLError, OSError, ValueError, json.JSONDecodeError):
        return "unavailable"

    try:
        source_tag = _grafana_source_tag(dashboard_path)
    except OSError as error:
        print(f"warning: Grafana dashboard source could not be read: {error}; continuing editor startup.", file=sys.stderr)
        return "unavailable"
    dashboard_exists = False
    try:
        remote = _read_json(Request(f"{endpoint.url}/api/dashboards/uid/{GRAFANA_DASHBOARD_UID}"), 1.0)
        dashboard_exists = True
        remote_dashboard = remote.get("dashboard", {})
        if isinstance(remote_dashboard, dict) and source_tag in remote_dashboard.get("tags", []):
            return "current"
    except HTTPError as error:
        if error.code != 404:
            print(f"warning: Grafana dashboard lookup failed with HTTP {error.code}; continuing without import.", file=sys.stderr)
            return "unavailable"
    except (URLError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"warning: Grafana dashboard lookup failed: {error}; continuing without import.", file=sys.stderr)
        return "unavailable"

    try:
        dashboard = json.loads(dashboard_path.read_text(encoding="utf-8"))
        tags = [tag for tag in dashboard.get("tags", []) if not tag.startswith(GRAFANA_SOURCE_TAG_PREFIX)]
        dashboard["tags"] = [*tags, source_tag]
        dashboard["id"] = None
        dashboard["version"] = 0
        body = json.dumps(
            {
                "dashboard": dashboard,
                "overwrite": True,
                "message": "Synchronize the repository HoroEditor observability dashboard",
            }
        ).encode("utf-8")
        request = Request(
            f"{endpoint.url}/api/dashboards/db",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        result = _read_json(request, 2.0)
        if result.get("status") != "success":
            raise ValueError("Grafana did not confirm the dashboard import")
        return "updated" if dashboard_exists else "imported"
    except HTTPError as error:
        print(f"warning: Grafana dashboard import failed with HTTP {error.code}; continuing editor startup.", file=sys.stderr)
    except (URLError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"warning: Grafana dashboard import failed: {error}; continuing editor startup.", file=sys.stderr)
    return "unavailable"


def configure_command(settings: DeveloperSettings, build_directory: Path = DEFAULT_BUILD_DIRECTORY) -> list[str]:
    """Build the always-run CMake configure command."""
    return [
        "cmake",
        "-S",
        str(REPOSITORY_ROOT),
        "-B",
        str(build_directory),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DHORO_BUILD_EDITOR_GUI=ON",
        "-DBUILD_TESTING=OFF",
        f"-DHORO_ENABLE_OPENTELEMETRY={'ON' if settings.otel_export_enabled else 'OFF'}",
    ]


def build_command(build_directory: Path = DEFAULT_BUILD_DIRECTORY) -> list[str]:
    """Build only the editor and its dependencies incrementally."""
    return ["cmake", "--build", str(build_directory), "--target", "HoroEditor", "--parallel"]


def editor_executable(build_directory: Path = DEFAULT_BUILD_DIRECTORY, platform: str = sys.platform) -> Path:
    """Return the directly executable editor path for the host platform."""
    if platform == "darwin":
        return build_directory / "apps" / "HoroEditor.app" / "Contents" / "MacOS" / "HoroEditor"
    if platform == "win32":
        return build_directory / "apps" / "HoroEditor.exe"
    return build_directory / "apps" / "HoroEditor"


def child_environment(settings: DeveloperSettings, environ: Mapping[str, str] | None = None) -> dict[str, str]:
    """Create the editor environment with an explicit export policy."""
    child = dict(os.environ if environ is None else environ)
    child.pop("HORO_OTEL_EXPORT_APPROVED", None)
    child.pop("HORO_OTEL_ENDPOINT", None)
    child.pop("HORO_DEV_OTEL_EXPORT", None)
    if settings.otel_export_enabled:
        child["HORO_OTEL_EXPORT_APPROVED"] = "1"
        child["HORO_OTEL_ENDPOINT"] = settings.endpoint.url
    return child


def print_summary(settings: DeveloperSettings, collector_status: str) -> None:
    """Print the effective non-secret developer configuration."""
    try:
        displayed_build_directory = DEFAULT_BUILD_DIRECTORY.relative_to(REPOSITORY_ROOT)
    except ValueError:
        displayed_build_directory = DEFAULT_BUILD_DIRECTORY
    print("Horo Developer Runner")
    print()
    print("Target:          HoroEditor")
    print(f"Build directory: {displayed_build_directory}")
    print("Build type:      Debug")
    print(f"OpenTelemetry:   {'enabled' if settings.otel_export_enabled else 'disabled'}")
    if settings.otel_export_enabled:
        print(f"OTLP endpoint:   {settings.endpoint.url}")
        print(f"Collector:       {collector_status}")
    else:
        print("Collector:       not checked")
    print()


def run_editor(settings: DeveloperSettings, editor_arguments: Sequence[str]) -> int:
    """Configure, build, and launch HoroEditor without invoking a shell."""
    collector_status = "not checked"
    if settings.otel_export_enabled:
        collector_status = "reachable" if collector_is_reachable(settings.endpoint) else "unreachable"
    print_summary(settings, collector_status)

    if settings.otel_export_enabled and collector_status == "unreachable":
        print(
            f"warning: OpenTelemetry endpoint {settings.endpoint.host}:{settings.endpoint.port} is not reachable.\n"
            "The editor will continue with local diagnostics only.",
            file=sys.stderr,
        )

    print("Configuring HoroEditor...", flush=True)
    try:
        configured = subprocess.run(configure_command(settings), cwd=REPOSITORY_ROOT, check=False)
    except OSError as error:
        print(f"error: could not run CMake configure: {error}", file=sys.stderr)
        return 127
    if configured.returncode != 0:
        return configured.returncode

    print("Building HoroEditor...", flush=True)
    try:
        built = subprocess.run(build_command(), cwd=REPOSITORY_ROOT, check=False)
    except OSError as error:
        print(f"error: could not run CMake build: {error}", file=sys.stderr)
        return 127
    if built.returncode != 0:
        return built.returncode

    if settings.grafana_endpoint is not None:
        grafana_status = sync_grafana_dashboard(settings.grafana_endpoint)
        print(f"Grafana dashboard: {grafana_status}", flush=True)
    else:
        print("Grafana dashboard: disabled", flush=True)

    executable = editor_executable()
    if not executable.is_file():
        print(f"error: HoroEditor executable was not produced at {executable}", file=sys.stderr)
        return 1

    print("Launching...", flush=True)
    try:
        launched = subprocess.run(
            [str(executable), *editor_arguments],
            cwd=REPOSITORY_ROOT,
            env=child_environment(settings),
            check=False,
        )
        return launched.returncode
    except OSError as error:
        print(f"error: could not launch HoroEditor: {error}", file=sys.stderr)
        return 126
    except KeyboardInterrupt:
        return 130


def create_argument_parser() -> argparse.ArgumentParser:
    """Create the intentionally narrow developer command parser."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="configure, build, and run a supported target")
    run.add_argument("target", choices=("editor",))
    run.add_argument("arguments", nargs=argparse.REMAINDER, help="arguments forwarded after --")
    return parser


def main(arguments: Sequence[str] | None = None) -> int:
    """Run the developer command-line interface."""
    parsed = create_argument_parser().parse_args(arguments)
    forwarded = list(parsed.arguments)
    if forwarded[:1] == ["--"]:
        forwarded.pop(0)

    try:
        settings = load_settings()
    except (ConfigurationError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    try:
        return run_editor(settings, forwarded)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
