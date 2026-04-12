# GI/Reflection Regression Scene Pack

This document defines the deterministic regression validation path introduced for issue `#174`.

## Files

- `assets/regression/gi_reflection/scene_pack.json`
- `assets/regression/gi_reflection/baseline_expectations.json`
- `scripts/ci/check_gi_reflection_regression.py`
- `tests/test_gi_reflection_regression.cpp`

## What Is Gated

1. **Tier contract baselines**  
   The baseline file pins expected SSR/SSGI/final-gather tier values (Low/Medium/High/Ultra).

2. **Temporal instability budgets**  
   Each tier declares:
   - `baselineInstabilityIndex`
   - `maxTemporalInstabilityIndex`

   Lower tiers may tolerate higher instability, while higher tiers must remain stricter.

3. **Scenario coverage**  
   The scene pack must include both:
   - `stable` temporal scenarios
   - `unstable` temporal scenarios  
   and must cover at least three quality tiers.

## CI Gate

The GitHub workflow job `gi-reflection-regression-gate` runs:

```bash
python3 scripts/ci/check_gi_reflection_regression.py
```

If tier monotonic rules, scenario coverage, or instability thresholds are violated, the workflow fails.

## Local Validation

```bash
python3 scripts/ci/check_gi_reflection_regression.py
cmake --build --preset debug-msvc --config Debug --target test_gi_reflection_regression
ctest --test-dir build/debug-msvc -C Debug --output-on-failure -R test_gi_reflection_regression
```
