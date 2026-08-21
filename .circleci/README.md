# CircleCI setup

This repository uses `.circleci/config.yml` for the Linux validation workflow
migrated from `develop`.

Windows and macOS build/test jobs run from `.github/workflows/platform.yml`,
because those native runners are not part of the project's CircleCI free
allocation. CircleCI keeps the Linux path as the primary migration target.

UI automation, release packaging, and SonarCloud remain in GitHub Actions.

1. Connect `abdullahbodur/horo-engine` in CircleCI using the GitHub integration.
2. UI automation and release workflows are configured under `.github/workflows`.
3. macOS validation is retained but disabled by default because it requires a
   CircleCI plan with macOS credits. Trigger it with `run_macos: true` after
   enabling that plan.
4. Release uploads use GitHub Actions credentials; keep their permissions
   restricted to the repository.

CircleCI's macOS and Windows executors require a plan that enables those
resource classes.
