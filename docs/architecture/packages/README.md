# Packages

This directory defines the Horo Package System: how assets, game libraries,
hybrid content packs, templates, package sources, dependency resolution,
restore, lifecycle, trust, cache, and release integration work.

Recommended reading order:

1. [Package System](./package-system.md)
2. [Package Restore](./package-restore.md)
3. [Package Lifecycle](./package-lifecycle.md)
4. [Package Release Integration](./package-release-integration.md)

Core rule: package restore/install never executes package code.

Documents:

- [Package System](./package-system.md): core model, package kinds,
  contribution kinds, manifest format, source types, resolver, lockfile, cache,
  trust levels, validation, and service boundaries.
- [Package Restore](./package-restore.md): clean-machine restore, bootstrap,
  CI, offline restore, dev overrides, package URLs, and non-interactive policy.
- [Package Lifecycle](./package-lifecycle.md): install, trust, enable,
  activation, update, uninstall, ownership states, migration, conflict policy,
  and transactional staging.
- [Package Release Integration](./package-release-integration.md): lockfile
  freeze, release validation, `assets.horo`, chunks, DLC, editor-only exclusion,
  license/notices, and deterministic release.

The package system is separate from the [Extension System](../extensions/plugin-system.md),
which governs host-owned editor and engine extensions.
