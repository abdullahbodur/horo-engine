#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"[gi-reflection-regression-gate] ERROR: {message}")
    raise SystemExit(1)


def load_json(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except FileNotFoundError:
        fail(f"missing required file: {path}")
    except json.JSONDecodeError as exc:
        fail(f"invalid JSON in {path}: {exc}")
    return {}


def clip01(value: float) -> float:
    return max(0.0, min(1.0, value))


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    pack_path = root / "assets" / "regression" / "gi_reflection" / "scene_pack.json"
    baseline_path = root / "assets" / "regression" / "gi_reflection" / "baseline_expectations.json"

    pack = load_json(pack_path)
    baseline = load_json(baseline_path)

    if pack.get("version") != 1:
        fail("scene pack version must be 1")
    if baseline.get("version") != 1:
        fail("baseline version must be 1")

    tiers = baseline.get("qualityTiers")
    if not isinstance(tiers, dict) or not tiers:
        fail("baseline.qualityTiers must be a non-empty object")

    expected_tiers = ["Low", "Medium", "High", "Ultra"]
    for tier in expected_tiers:
        if tier not in tiers:
            fail(f"baseline missing tier: {tier}")

    previous_samples = -1
    previous_budget = math.inf
    for tier in expected_tiers:
        tier_data = tiers[tier]
        gather = tier_data["finalGather"]
        temporal = tier_data["temporal"]

        samples = int(gather["gatherSamplesPerPixel"])
        max_instability = float(temporal["maxTemporalInstabilityIndex"])

        if samples <= previous_samples:
            fail(f"gatherSamplesPerPixel must strictly increase by tier ({tier})")
        if max_instability >= previous_budget:
            fail(f"maxTemporalInstabilityIndex must strictly decrease by tier ({tier})")

        previous_samples = samples
        previous_budget = max_instability

    scenarios = pack.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        fail("scene pack scenarios must be a non-empty array")

    ids: set[str] = set()
    referenced_tiers: set[str] = set()
    stability_modes = {"stable": 0, "unstable": 0}
    for scenario in scenarios:
        scenario_id = scenario.get("id")
        tier = scenario.get("tier")
        expected_stability = scenario.get("expectedStability")
        instability_bias = float(scenario.get("instabilityBias", 0.0))

        if not scenario_id or not isinstance(scenario_id, str):
            fail("all scenarios must provide a non-empty string id")
        if scenario_id in ids:
            fail(f"duplicate scenario id: {scenario_id}")
        ids.add(scenario_id)

        if tier not in tiers:
            fail(f"scenario '{scenario_id}' references unknown tier '{tier}'")

        if expected_stability not in ("stable", "unstable"):
            fail(f"scenario '{scenario_id}' expectedStability must be 'stable' or 'unstable'")

        tier_temporal = tiers[tier]["temporal"]
        score = clip01(float(tier_temporal["baselineInstabilityIndex"]) + instability_bias)
        threshold = float(tier_temporal["maxTemporalInstabilityIndex"])
        is_stable = score <= threshold

        if expected_stability == "stable" and not is_stable:
            fail(f"scenario '{scenario_id}' exceeded instability budget ({score:.4f} > {threshold:.4f})")
        if expected_stability == "unstable" and is_stable:
            fail(f"scenario '{scenario_id}' did not exceed instability budget ({score:.4f} <= {threshold:.4f})")

        referenced_tiers.add(tier)
        stability_modes[expected_stability] += 1

    if len(referenced_tiers) < 3:
        fail("scene pack must cover at least three quality tiers")
    if stability_modes["stable"] == 0 or stability_modes["unstable"] == 0:
        fail("scene pack must include both stable and unstable temporal scenarios")

    print("[gi-reflection-regression-gate] Baseline and scene-pack checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
