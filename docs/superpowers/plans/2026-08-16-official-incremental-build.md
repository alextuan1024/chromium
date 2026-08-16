# Official Incremental Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Document the low-latency command and diagnosis workflow for routine production-like Chromium rebuilds.

**Architecture:** Preserve the existing Official GN configuration and output directory. Change only the build instructions so generation is one-time, non-interactive Siso uses its low-latency mode, and suspicious rebuilds can be previewed.

**Tech Stack:** GN, Siso/autoninja, Markdown

## Global Constraints

- Keep PGO, ThinLTO, proprietary codecs, Chrome FFmpeg branding, and Widevine enabled.
- Add no scripts, dependencies, compiler cache, or build directory.
- Do not run tests or build verification at the user's request.

---

### Task 1: Document the incremental Official workflow

**Files:**
- Modify: `AGENTS.md:42-49`

**Interfaces:**
- Consumes: existing `out/Official/args.gn` and `depot_tools/autoninja`
- Produces: initial-generation, incremental-build, and dry-run commands for developers and agents

- [x] **Step 1: Separate initial generation from routine builds**

Keep `./buildtools/mac/gn gen out/Official` as the one-time setup command. Add this routine command:

```sh
/Users/xiguoduan/go/src/github.com/alextuan1024/depot_tools/autoninja \
  -C out/Official --batch=false chrome
```

- [x] **Step 2: Add the broad-invalidation diagnostic**

Document this dry run for unexpectedly expensive incremental builds:

```sh
/Users/xiguoduan/go/src/github.com/alextuan1024/depot_tools/autoninja \
  -C out/Official --batch=false -n chrome
```

Explain that a near-clean schedule should prompt inspection of recent GN argument, toolchain, dependency, or source-sync changes.

- [x] **Step 3: Review without testing**

Inspect the Markdown diff for command accuracy and scope. Do not execute GN, autoninja, signing, or tests.

- [x] **Step 4: Commit**

`AGENTS.md` is locally excluded by `.git/info/exclude`; preserve that local-only
status and commit the tracked plan without force-adding the instruction file.

```sh
git add docs/superpowers/plans/2026-08-16-official-incremental-build.md
git commit -m "docs: improve Official incremental build workflow"
```
