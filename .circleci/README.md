# CircleCI setup

This repository uses `.circleci/config.yml` only for SonarCloud analysis.

Linux build/test, Windows/macOS platform tests, UI automation, and release
packaging run from GitHub Actions.

1. Connect `abdullahbodur/horo-engine` in CircleCI using the GitHub integration.
2. Add `SONAR_TOKEN` to the `horo-ci-quality` CircleCI context.
3. Grant this project access to that context.
4. Keep build/test, UI automation, and release workflows under
   `.github/workflows`.

The Sonar job caches FetchContent dependencies, compiler output, and the CFamily
analysis cache between runs.
