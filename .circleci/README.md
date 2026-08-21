# CircleCI setup

This repository uses `.circleci/config.yml` only for SonarCloud analysis.

Linux build/test, Windows/macOS platform tests, UI automation, and release
packaging run from GitHub Actions.

1. Connect `abdullahbodur/horo-engine` in CircleCI using the GitHub integration.
2. UI automation, release, and platform workflows are configured under
   `.github/workflows`.
3. macOS validation is retained but disabled by default because it requires a
   CircleCI plan with macOS credits. Trigger it with `run_macos: true` after
   enabling that plan.
4. Release uploads use GitHub Actions credentials; keep their permissions
   restricted to the repository.

CircleCI's macOS and Windows executors require a plan that enables those
resource classes.
