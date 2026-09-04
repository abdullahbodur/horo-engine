# CircleCI fallback (temporarily disabled)

SonarCloud analysis now runs in `.github/workflows/sonar.yml` on pushes to
`main`, same-repository pull requests, and manual dispatch. Fork pull requests
are skipped because they cannot access `SONAR_TOKEN`. Configure that secret in
GitHub Actions; the workflow checks it before installing or building anything.

CircleCI retains the previous job for rollback. Its workflow defaults to off
through the boolean pipeline parameter `run-circleci-sonar`. To run it manually,
set that parameter to `true` and retain the `horo-ci-quality` context with its
`SONAR_TOKEN`. To restore automatic CircleCI operation, change the parameter's
default to `true` and disable the GitHub Sonar workflow in the same change to
avoid duplicate analyses. Existing CircleCI runs are not canceled by this file.
Any branch protection rule requiring a CircleCI status must be updated when this
configuration lands; the GitHub job is named `SonarCloud`.

## Build and analysis performance

GitHub retains the CircleCI Ninja/GCC coverage build, editor, GPU smoke,
telemetry and OpenTelemetry options, Xvfb test selection, C++ and Python coverage,
compilation database, and blocking Quality Gate. Analysis runs immediately in
its own job, without waiting for the other CI jobs or transferring a build
artifact. Superseded runs on the same ref are canceled.

Three caches preserve FetchContent dependencies, ccache output (bounded to 2 GiB),
and CFamily analysis plus scanner downloads. Compiler/analysis cache keys are
unique per commit, restore the current ref first, and fall back to `main`.
Re-running the same commit reuses its immutable cache without another upload;
a branch-only key would freeze the cache at its first successful save.
Dependency keys cover all CMake lists and the `cmake/` directory. Build output
and coverage counters are not restored, so coverage always describes this run.
Caches are saved after successful compilation even when tests or the Quality
Gate fail; available analysis caches also survive a failed scan.

The standard public-repository Ubuntu runner has 4 CPUs and 16 GB RAM. Runner
hardware differs from CircleCI `large.gen2`; equal wall time is not guaranteed.
Measure a cold main run, a warm main run, and a small same-repository PR. Compare
installation, configure/build, tests/coverage, scan, and cache transfer durations
in the Actions job steps; inspect `ccache --show-stats` and scanner cache logs.
Record commit, runner, cache state and Quality Gate wait time for each comparison.

References: [GitHub runner specifications](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job),
[Sonar CFamily cache](https://docs.sonarsource.com/sonarqube-cloud/advanced-setup/languages/c-family/customizing-the-analysis),
[CircleCI workflow conditions](https://circleci.com/docs/guides/orchestrate/workflows/).
