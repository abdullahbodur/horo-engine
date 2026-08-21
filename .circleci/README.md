# CircleCI setup

This repository uses `.circleci/config.yml` for the migrated validation and UI
workflows from `develop`.

1. Connect `abdullahbodur/horo-engine` in CircleCI using the GitHub integration.
2. Configure a scheduled trigger every six hours with pipeline parameter
   `run_ui: true` to run the UI workflow.
3. Create a restricted CircleCI context containing `GH_TOKEN` with permission
   to upload release assets. Trigger a release with `release_tag: vX.Y.Z` only
   after that GitHub release already exists.
4. Keep the CircleCI context out of fork and untrusted pull-request jobs.

CircleCI's macOS and Windows executors require a plan that enables those
resource classes.
