# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3

# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: Apache-2.0
#

"""
Generate an enhanced single-file HTML test report from Zephyr Twister results.
"""

import argparse
import base64
import html
import json
import sys
import webbrowser
from datetime import datetime, timezone
from pathlib import Path

import yaml

SHOW_STATUSES = {"passed", "failed", "error"}

SCRIPT_DIR     = Path(__file__).resolve().parent
# qcc74x_testing now lives at <workspace>/qapp/qcc74x/qcc74x_testing. Locate the
# west workspace root by walking up to the dir containing both `zephyr` and
# `.west`, so paths work regardless of nesting depth.
def _find_workspace_root(start: Path) -> Path:
    for parent in [start, *start.parents]:
        if (parent / "zephyr").is_dir() and (parent / ".west").is_dir():
            return parent
    return start.parent.parent.parent.parent


WORKSPACE_ROOT = _find_workspace_root(SCRIPT_DIR)
TESTING_ROOT   = SCRIPT_DIR.parent          # the qcc74x_testing tree
REPORTS_DIR    = TESTING_ROOT / "reports"

MANIFEST_DIR     = WORKSPACE_ROOT / "qcc74x_zephyr"
FEATURE_MAP_PATH = SCRIPT_DIR / "feature_map.yaml"
TESTS_DIR        = REPORTS_DIR / "tests"
COVERAGE_DIR     = REPORTS_DIR / "coverage"

# Roots to search for testcase.yaml / sample.yaml files (in priority order)
TESTCASE_SEARCH_ROOTS: list[Path] = [
    TESTING_ROOT,
    WORKSPACE_ROOT / "zephyr",
]

# Tags that carry no feature-level meaning and are skipped during matching
GENERIC_TAGS: frozenset[str] = frozenset({
    "drivers", "driver", "shell", "userspace", "fpu",
    "introduction", "dynamic_memory", "heap", "statistics",
})

REPORT_TITLE_PREFIX = "QCC7xx Test Report"

IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg"}
MIME_TYPES = {
    ".png":  "image/png",
    ".jpg":  "image/jpeg",
    ".jpeg": "image/jpeg",
    ".gif":  "image/gif",
    ".bmp":  "image/bmp",
    ".webp": "image/webp",
    ".svg":  "image/svg+xml",
}


def load_template() -> str:
    path = SCRIPT_DIR / "report_template.html"
    if not path.exists():
        sys.exit(f"ERROR: report_template.html not found next to script ({path})")
    return path.read_text(encoding="utf-8")


def load_twister_json(input_dir: Path) -> dict:
    path = input_dir / "twister.json"
    if not path.exists():
        sys.exit(f"ERROR: twister.json not found in {input_dir}")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def discover_test_results(tests_dir: Path) -> list[Path]:
    """Find all Twister .json result files under tests_dir (recursive)."""
    if not tests_dir.is_dir():
        return []
    found = []
    for p in sorted(tests_dir.rglob("*.json")):
        try:
            with open(p, encoding="utf-8") as f:
                data = json.load(f)
            if "testsuites" in data:
                found.append(p)
        except (OSError, json.JSONDecodeError):
            pass
    return found


def merge_twister_data(sources: list[Path]) -> dict:
    """Load and merge results from directories (twister.json) and standalone .json files."""
    merged_testsuites: list[dict] = []
    environment: dict = {}
    seen_files: set[Path] = set()

    for src in sources:
        if src.is_file():
            canonical = src.resolve()
            if canonical in seen_files:
                print(f"  [skip] duplicate: {src}", file=sys.stderr)
                continue
            seen_files.add(canonical)
            with open(src, encoding="utf-8") as f:
                data = json.load(f)
            source_dir = src.parent
        else:
            canonical = (src / "twister.json").resolve()
            if canonical in seen_files:
                print(f"  [skip] duplicate: {src}/twister.json", file=sys.stderr)
                continue
            seen_files.add(canonical)
            data = load_twister_json(src)
            source_dir = src

        src_env = data.get("environment", {})
        if not environment:
            environment = src_env
        run_date = src_env.get("run_date", "")
        for suite in data.get("testsuites", []):
            entry = dict(suite)
            entry["_source_dir"] = source_dir
            entry["_is_dir_source"] = src.is_dir()
            entry["_run_date"] = run_date
            merged_testsuites.append(entry)

    deduped, dropped = deduplicate_suites(merged_testsuites)
    if dropped:
        print(f"  [dedup] removed {dropped} duplicate suite(s)")

    return {
        "environment": environment,
        "testsuites": deduped,
        "_input_dirs": sources,
    }


def _suite_source_score(suite: dict) -> tuple[int, int, float]:
    """Score for deduplication: prefer shown-status results first, then directory sources (have logs), then newest run_date."""
    status = suite.get("status", "")
    has_shown_status = int(status in SHOW_STATUSES)
    has_logs = int(suite.get("_is_dir_source", False))
    run_date_str = suite.get("_run_date", "")
    try:
        run_ts = datetime.fromisoformat(run_date_str).timestamp() if run_date_str else 0.0
    except (ValueError, TypeError):
        run_ts = 0.0
    return (has_shown_status, has_logs, run_ts)


def deduplicate_suites(suites: list[dict]) -> tuple[list[dict], int]:
    """Deduplicate by (name, platform), keeping the highest-scored copy."""
    seen: dict[tuple, dict] = {}
    for suite in suites:
        key = (suite.get("name", ""), suite.get("platform", ""))
        if key not in seen:
            seen[key] = suite
        else:
            current_best = seen[key]
            if _suite_source_score(suite) > _suite_source_score(current_best):
                seen[key] = suite

    dropped = len(suites) - len(seen)
    return list(seen.values()), dropped


def read_log(source_dir: Path, platform: str, suite_name: str, filename: str,
             is_dir_source: bool = True) -> str | None:
    """Read a log file from the Twister output tree, with flat fallback for standalone JSON sources."""
    platform_dir = platform.replace("/", "_")
    log_path = source_dir / platform_dir / "zephyr" / suite_name / filename
    if log_path.exists():
        try:
            return log_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return None
    if not is_dir_source:
        flat_path = source_dir / filename
        if flat_path.exists():
            try:
                return flat_path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                return None
    return None


def read_images(source_dir: Path, platform: str, suite_name: str,
                is_dir_source: bool = True) -> list[dict]:
    """Return base64-encoded images from the test output directory as data-URI dicts."""
    platform_dir = platform.replace("/", "_")
    search_dirs: list[Path] = []
    candidate = source_dir / platform_dir / "zephyr" / suite_name
    if candidate.is_dir():
        search_dirs.append(candidate)
    elif not is_dir_source:
        if source_dir.is_dir():
            search_dirs.append(source_dir)

    images: list[dict] = []
    for d in search_dirs:
        for img_path in sorted(d.iterdir()):
            if img_path.suffix.lower() in IMAGE_EXTENSIONS and img_path.is_file():
                try:
                    raw = img_path.read_bytes()
                    b64 = base64.b64encode(raw).decode("ascii")
                    mime = MIME_TYPES.get(img_path.suffix.lower(), "image/octet-stream")
                    images.append({
                        "filename": img_path.name,
                        "data": f"data:{mime};base64,{b64}",
                    })
                except OSError:
                    pass
    return images


def read_testcase_meta(rel_path: str, test_id: str) -> tuple[frozenset[str], frozenset[str]]:
    """Return (depends_on, tags) for a specific test variant; generic tags are excluded."""
    deps: set[str] = set()
    tags: set[str] = set()

    tc_data: dict | None = None
    for root in TESTCASE_SEARCH_ROOTS:
        p = root / rel_path
        for fname in ("testcase.yaml", "sample.yaml"):
            tc_yaml = p / fname
            if tc_yaml.exists():
                try:
                    with open(tc_yaml, encoding="utf-8") as f:
                        tc_data = yaml.safe_load(f) or {}
                except (OSError, yaml.YAMLError):
                    pass
                break
        if tc_data is not None:
            break

    if not tc_data:
        return frozenset(), frozenset()

    def _collect(block: dict) -> None:
        d = block.get("depends_on", [])
        deps.update(d if isinstance(d, list) else [d])
        t = block.get("tags", [])
        tags.update(t if isinstance(t, list) else [t])

    _collect(tc_data.get("common") or {})
    tests: dict = tc_data.get("tests") or {}
    if test_id in tests:
        tval = tests[test_id]
        if isinstance(tval, dict):
            _collect(tval)
    elif tests:
        # test_id not found — fall back to first entry (e.g. single-test sample.yaml)
        for tval in tests.values():
            if isinstance(tval, dict):
                _collect(tval)
                break

    return frozenset(deps), frozenset(tags - GENERIC_TAGS)


def load_feature_map(path: Path | None) -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """Load feature_map.yaml; returns (feature_map, custom_features).

    Values are explicit test suite IDs for tests that lack matching tags/depends_on.
    custom_features covers features not in any board's supported: list.
    """
    yaml_path = path or (SCRIPT_DIR / "feature_map.yaml")
    if not yaml_path.exists():
        sys.exit(
            f"ERROR: feature_map.yaml not found: {yaml_path}\n"
        )
    with open(yaml_path, encoding="utf-8") as f:
        loaded = yaml.safe_load(f) or {}

    custom_raw: dict = loaded.pop("custom_features", None) or {}
    result = {f: [str(s) for s in (suites or [])] for f, suites in loaded.items()}
    custom = {f: [str(s) for s in (suites or [])] for f, suites in custom_raw.items()}
    return result, custom


def load_board_yaml(manifest_dir: Path, board_id: str) -> list[str]:
    """Return the supported: feature list for a board from its YAML file."""
    yaml_path = manifest_dir / "boards" / "qcom" / board_id / f"{board_id}.yaml"
    if not yaml_path.exists():
        return []
    with open(yaml_path, encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return [str(s) for s in data.get("supported", [])]


def load_all_supported_features(manifest_dir: Path) -> set[str]:
    """Union of supported: features across every board in the manifest."""
    boards_root = manifest_dir / "boards" / "qcom"
    all_features: set[str] = set()
    if not boards_root.is_dir():
        return all_features
    for board_dir in boards_root.iterdir():
        if board_dir.is_dir():
            all_features.update(load_board_yaml(manifest_dir, board_dir.name))
    return all_features


def _build_feature_entry(
    feature: str,
    explicit_suite_ids: list[str],
    suites: list[dict],
    matched_suite_names: set[str],
    custom: bool = False,
) -> dict:
    """Build a coverage entry for one feature.

    Matches by depends_on, tags, or explicit suite ID from feature_map.yaml.
    """
    explicit_set = set(explicit_suite_ids)
    matched_passed: list[str] = []
    matched_failed: list[str] = []

    for s in suites:
        deps: frozenset[str] = s.get("_deps", frozenset())
        tags: frozenset[str] = s.get("_tags", frozenset())
        suite_id: str = s["name"].rsplit("/", 1)[-1]

        if feature not in deps and feature not in tags and suite_id not in explicit_set:
            continue

        matched_suite_names.add(s["name"])
        if s["status"] == "passed":
            matched_passed.append(s["name"])
        elif s["status"] in ("failed", "error"):
            matched_failed.append(s["name"])

    if matched_passed:
        status, suite_list = "covered", sorted(set(matched_passed))
    elif matched_failed:
        status, suite_list = "failing", sorted(set(matched_failed))
    else:
        status, suite_list = "gap", []

    return {
        "name": feature,
        "status": status,
        "count": len(suite_list),
        "suites": suite_list,
        "explicit_suites": explicit_suite_ids,
        "custom": custom,
    }


def analyze_coverage(
    suites: list[dict],
    manifest_dir: Path,
    feature_map: dict[str, list[str]],
    custom_features: dict[str, list[str]] | None = None,
) -> dict:
    """Compute feature coverage across all boards."""
    board_ids: set[str] = {s.get("platform", "").split("/")[0] for s in suites}

    all_supported: set[str] = set()
    for board_id in board_ids:
        all_supported.update(load_board_yaml(manifest_dir, board_id))

    if not all_supported and not custom_features:
        return {}

    all_suite_names = {s["name"] for s in suites}
    matched_suite_names: set[str] = set()

    features = [
        _build_feature_entry(f, feature_map.get(f, []), suites, matched_suite_names)
        for f in sorted(all_supported)
    ]
    custom_entries = [
        _build_feature_entry(f, (custom_features or {}).get(f, []), suites, matched_suite_names, custom=True)
        for f in sorted((custom_features or {}).keys())
    ]

    total   = len(features)
    covered = sum(1 for f in features if f["status"] == "covered")
    failing = sum(1 for f in features if f["status"] == "failing")
    others  = sorted(all_suite_names - matched_suite_names)

    return {
        "total": total,
        "covered": covered,
        "failing": failing,
        "gap": total - covered - failing,
        "boards": sorted(board_ids),
        "features": features,
        "custom_features": custom_entries,
        "others": others,
    }


def process_data(
    data: dict,
    input_dirs: list[Path],
    manifest_dir: Path | None = None,
    feature_map_path: Path | None = None,
) -> dict:
    fallback_dir = input_dirs[0]
    env = data.get("environment", {})
    all_suites = data.get("testsuites", [])
    input_dirs_list = data.get("_input_dirs", input_dirs)

    suites = [s for s in all_suites if s.get("status") in SHOW_STATUSES]
    boards = sorted({s["platform"] for s in suites})

    total_passed = 0
    total_failed = 0

    enriched = []
    for suite in suites:
        status = suite.get("status", "unknown")
        name = suite.get("name", "")
        platform = suite.get("platform", "")
        source_dir: Path = suite.get("_source_dir", fallback_dir)

        raw_cases = suite.get("testcases", [])
        cases = [tc for tc in raw_cases if tc.get("status") in ("passed", "failed")]
        skipped_count = sum(1 for tc in raw_cases if tc.get("status") not in ("passed", "failed"))

        if status == "passed":
            total_passed += 1
        else:
            total_failed += 1

        is_dir = suite.get("_is_dir_source", False)
        handler_log = read_log(source_dir, platform, name, "handler.log", is_dir)
        build_log = read_log(source_dir, platform, name, "build.log", is_dir) if status != "passed" else None
        images = read_images(source_dir, platform, name, is_dir)

        rel_path = suite.get("path", "")
        test_id = name.rsplit("/", 1)[-1]
        deps, tags = read_testcase_meta(rel_path, test_id)

        enriched.append({
            "name": name,
            "platform": platform,
            "status": status,
            "execution_time": suite.get("execution_time", "-"),
            "build_time": suite.get("build_time", "-"),
            "reason": suite.get("reason") or "",
            "dut": suite.get("dut", ""),
            "manual": bool(suite.get("manual", False)),
            "description": suite.get("description") or "",
            "cases": cases,
            "skipped_count": skipped_count,
            "handler_log": handler_log,
            "build_log": build_log,
            "images": images,
            "_deps": deps,
            "_tags": tags,
        })

    enriched.sort(key=lambda s: (0 if s["status"] != "passed" else 1, s["name"], s["platform"]))

    run_date_raw = env.get("run_date", "")
    try:
        dt = datetime.fromisoformat(run_date_raw)
        run_date = dt.astimezone(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    except (ValueError, TypeError, OverflowError, OSError):
        run_date = run_date_raw

    coverage: dict = {}
    if manifest_dir is not None:
        feature_map, custom_features = load_feature_map(feature_map_path)
        coverage = analyze_coverage(enriched, manifest_dir, feature_map, custom_features)

    return {
        "zephyr_version": env.get("zephyr_version", "unknown"),
        "run_date": run_date,
        "boards": boards,
        "total_run": len(suites),
        "total_passed": total_passed,
        "total_failed": total_failed,
        "suites": enriched,
        "coverage": coverage,
        "input_dirs": [str(d) for d in input_dirs_list],
    }


def esc(text: str) -> str:
    return html.escape(str(text))


def build_html(ctx: dict, title: str, template: str) -> str:
    serializable_suites = [
        {k: v for k, v in s.items() if not k.startswith("_")}
        for s in ctx["suites"]
    ]
    suites_json = json.dumps(serializable_suites, ensure_ascii=False)
    coverage_json = json.dumps(ctx["coverage"], ensure_ascii=False)

    dirs = ctx.get("input_dirs", [])
    sources_info = f' &nbsp;|&nbsp; {len(dirs)} sources merged' if len(dirs) > 1 else ""

    slots = {
        "{{title}}": esc(title),
        "{{zephyr_version}}": esc(ctx["zephyr_version"]),
        "{{run_date}}": esc(ctx["run_date"]),
        "{{sources_info}}": sources_info,
        "{{total_run}}": str(ctx["total_run"]),
        "{{total_passed}}": str(ctx["total_passed"]),
        "{{total_failed}}": str(ctx["total_failed"]),
        "{{suites_data_json}}": suites_json,
        "{{coverage_data_json}}": coverage_json,
    }

    result = template
    for slot, value in slots.items():
        result = result.replace(slot, value)
    return result


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Generate QCC7xx HTML test report. "
            "Auto-discovers result files from reports/tests/; "
            "extra Twister output directories may be passed as arguments."
        )
    )
    parser.add_argument(
        "input_dirs",
        nargs="*",
        metavar="INPUT_DIR",
        help="Optional extra Twister output directories (each must contain twister.json). "
             "Merged together with auto-discovered results from reports/tests/.",
    )
    args = parser.parse_args()

    extra_dirs: list[Path] = []
    for d in args.input_dirs:
        p = Path(d).resolve()
        if not p.is_dir():
            sys.exit(f"ERROR: input directory does not exist: {p}")
        extra_dirs.append(p)

    discovered = discover_test_results(TESTS_DIR)
    if discovered:
        rel = TESTS_DIR.relative_to(WORKSPACE_ROOT)
        print(f"Auto-discovered {len(discovered)} result file(s) from {rel}:")
        for f in discovered:
            print(f"  {f.relative_to(WORKSPACE_ROOT)}")
    else:
        print(f"No result files found in {TESTS_DIR.relative_to(WORKSPACE_ROOT)}")

    if extra_dirs:
        print(f"Extra input directories ({len(extra_dirs)}):")
        for d in extra_dirs:
            try:
                print(f"  {d.relative_to(WORKSPACE_ROOT)}")
            except ValueError:
                print(f"  {d}")

    sources = discovered + extra_dirs
    if not sources:
        sys.exit(
            "ERROR: No test results found.\n"
            f"  Add .json result files under {TESTS_DIR}\n"
            "  or pass Twister output directories as arguments."
        )

    print(f"\nMerging {len(sources)} source(s) ...")
    data = merge_twister_data(sources)

    now = datetime.now()
    filename = now.strftime("qcc7xx_test_report_%Y_%m_%d_%H_%M_%S.html")
    COVERAGE_DIR.mkdir(parents=True, exist_ok=True)
    output_path = COVERAGE_DIR / filename
    title = now.strftime(f"{REPORT_TITLE_PREFIX} - %Y-%m-%d %H:%M:%S")

    manifest_dir = MANIFEST_DIR if MANIFEST_DIR.is_dir() else None
    if manifest_dir is None:
        print(f"WARNING: manifest directory not found: {MANIFEST_DIR}")
        print("  Feature Coverage section will be disabled.")

    template = load_template()

    print("Processing test data ...")
    ctx = process_data(data, sources, manifest_dir, FEATURE_MAP_PATH)
    print(f"  Suites run: {ctx['total_run']}  passed: {ctx['total_passed']}  failed: {ctx['total_failed']}")
    print(f"  Boards: {', '.join(ctx['boards']) or '(none)'}")

    cov = ctx["coverage"]
    if cov.get("features") or cov.get("custom_features"):
        board_list = ", ".join(cov.get("boards", [])) or "(none)"
        print(f"  Coverage ({board_list}): "
              f"{cov['covered']}/{cov['total']} covered, "
              f"{cov['failing']} failing, {cov['gap']} gaps"
              + (f", {len(cov['custom_features'])} custom" if cov.get("custom_features") else ""))
    else:
        print("  Coverage: not computed (manifest directory not found)")

    print(f"\nGenerating HTML → {output_path}")
    html_content = build_html(ctx, title, template)
    output_path.write_text(html_content, encoding="utf-8")
    print("Done.")

    if sys.stdout.isatty():
        try:
            webbrowser.open(output_path.as_uri())
        except OSError:
            pass


if __name__ == "__main__":
    main()
