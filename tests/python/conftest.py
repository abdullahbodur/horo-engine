"""Shared pytest fixtures for repository-owned Python tooling tests."""

from __future__ import annotations

import pytest
from pathlib import Path
@pytest.fixture
def repository_root() -> Path:
    """Return the canonical repository root containing the active scripts."""
    return Path(__file__).resolve().parents[2]
@pytest.fixture
def compatibility_generator(repository_root: Path) -> Path:
    """Return the project compatibility catalog generator entry point."""
    return repository_root / "scripts" / "generate_project_compatibility.py"

@pytest.fixture
def migration_generator(repository_root: Path) -> Path:
    """Return the project migration catalog generator entry point."""
    return repository_root / "scripts" / "generate_project_migration_catalog.py"
