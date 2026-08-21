# CircleCI setup

This repository uses `.circleci/config.yml` for the migrated validation and UI
workflows from `develop`.

Windows and macOS build/test jobs run from `.github/workflows/platform.yml`,
because those native runners are not part of the project's CircleCI free
allocation. CircleCI keeps the Linux path as the primary migration target.

1. Connect `abdullahbodur/horo-engine` in CircleCI using the GitHub integration.
2. Configure a scheduled trigger every six hours with pipeline parameter
   `run_ui: true` to run the UI workflow.
3. macOS validation is retained but disabled by default because it requires a
   CircleCI plan with macOS credits. Trigger it with `run_macos: true` after
   enabling that plan.
4. Create a restricted CircleCI context containing `GH_TOKEN` with permission
   to upload release assets. Trigger a release with `release_tag: vX.Y.Z` only
   after that GitHub release already exists.
5. Keep the CircleCI context out of fork and untrusted pull-request jobs.

CircleCI's macOS and Windows executors require a plan that enables those
resource classes.
