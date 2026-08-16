# Official Incremental Build Design

## Goal

Reduce the latency of routine `out/Official` rebuilds while preserving the PGO
and ThinLTO behavior required for production-like validation.

## Evidence

- The retained Siso logs contain one 39-second incremental build with 1,075
  actions.
- Four near-clean builds executed about 54,000–56,000 actions and took 2 hours
  43 minutes to 3 hours 39 minutes.
- `out/Official` already enables Siso and the ThinLTO cache. Remote execution and
  compiler caching are unavailable locally.
- The machine has 26 GiB free, so adding a compiler cache would create immediate
  disk-pressure risk.

## Design

Change only `AGENTS.md`:

1. Keep the existing `out/Official/args.gn` values unchanged.
2. Separate one-time generation from the regular incremental command.
3. Use `autoninja -C out/Official --batch=false chrome` for routine builds.
   `--batch=false` preserves Siso's low-latency behavior when a build runs from
   a non-interactive agent session; it is harmless in an interactive terminal.
4. Document a dry run with `-n` as the first diagnostic when an incremental
   build unexpectedly schedules substantial work.

The workflow continues to use the existing output directory, dependency graph,
and ThinLTO cache. It adds no scripts, dependencies, or build variants.

## Failure Handling

- Run `gn gen out/Official` when creating the directory or after changing GN
  arguments. Normal dependency-file changes are regenerated automatically.
- If a dry run schedules most of Chromium, inspect recent GN argument, toolchain,
  dependency, or source-sync changes before starting the expensive build.
- Continue repairing and verifying the ad-hoc signature after compilation.

## Verification

Run a dry build and confirm Siso reports zero executed actions for an unchanged
tree:

```sh
/Users/xiguoduan/go/src/github.com/alextuan1024/depot_tools/autoninja \
  -C out/Official --batch=false -n chrome
```

After a representative source edit, run the same command without `-n` and
confirm that it schedules only the affected dependency closure.

## Exclusions

- Do not disable PGO or ThinLTO.
- Do not add `ccache` until sufficient disk space is available and rebuild data
  shows compilation cache misses are the dominant cost.
- Do not add another production-like output directory; `out/Release` already
  serves fast development builds.
