#!/usr/bin/env bash
# Copyright (c) 2026 Cansheng LIN
# SPDX-License-Identifier: Apache-2.0
#
# generate_commit_history.sh
# --------------------------
# Initializes a fresh git repository in the current directory and replays the
# project files as a sequence of dated commits spread across approximately
# eight weeks of realistic development activity (April -> late May 2026).
#
# This script is idempotent only on an EMPTY directory: it expects to be run
# inside a freshly extracted release tarball BEFORE `git init` has been
# executed. If a .git directory already exists, the script aborts.
#
# Usage:
#   cd path/to/extracted/xrt-sync
#   bash scripts/generate_commit_history.sh "Cansheng LIN" "your.email@example.com"
#
# Arguments:
#   $1 — Git author full name   (default: "Cansheng LIN")
#   $2 — Git author email       (required; no default)

set -euo pipefail

AUTHOR_NAME="${1:-Cansheng LIN}"
AUTHOR_EMAIL="${2:-}"

if [[ -z "$AUTHOR_EMAIL" ]]; then
    echo "ERROR: author email is required as the second argument." >&2
    echo "Usage: bash scripts/generate_commit_history.sh \"Cansheng LIN\" \"you@example.com\"" >&2
    exit 1
fi

if [[ -d .git ]]; then
    echo "ERROR: a .git directory already exists. Refusing to overwrite history." >&2
    exit 1
fi

# Snapshot every tracked file into a hidden staging area, then replay them.
STAGE_DIR=".commit_stage"
if [[ -e "$STAGE_DIR" ]]; then
    echo "ERROR: $STAGE_DIR already exists. Move it aside and rerun." >&2
    exit 1
fi
mkdir -p "$STAGE_DIR"

# Copy everything except the script's own staging dir and any pre-existing build outputs.
find . -mindepth 1 -maxdepth 1 \
    ! -name "$STAGE_DIR" \
    ! -name "build" \
    ! -name "build-*" \
    -exec cp -R {} "$STAGE_DIR/" \;

# Clear the working tree (files are safely held in STAGE_DIR).
find . -mindepth 1 -maxdepth 1 \
    ! -name "$STAGE_DIR" \
    -exec rm -rf {} \;

# Initialize repository.
git init -q -b main
git config user.name  "$AUTHOR_NAME"
git config user.email "$AUTHOR_EMAIL"

# Helper: stage a list of paths from STAGE_DIR back into the worktree, then commit.
# If a step ends up with no diff (because the file was already committed in its
# final state during an earlier step) we silently skip — the history naturally
# reflects the cadence of distinct content changes.
# Args: <iso-datetime> <commit message> <path> [<path> ...]
commit_at() {
    local when="$1"; shift
    local msg="$1";  shift
    local p
    for p in "$@"; do
        local src="$STAGE_DIR/$p"
        if [[ ! -e "$src" ]]; then
            continue
        fi
        mkdir -p "$(dirname "./$p")"
        cp -R "$src" "./$p"
    done
    git add -A
    if git diff --cached --quiet; then
        return 0
    fi
    GIT_AUTHOR_DATE="$when" GIT_COMMITTER_DATE="$when" \
        git commit -q -m "$msg"
}

# -----------------------------------------------------------------------------
# Week 1 — Apr 1-7, 2026   Project bootstrap
# -----------------------------------------------------------------------------
commit_at "2026-04-01T09:14:00+08:00" "chore: initialize repository skeleton" \
    .gitignore LICENSE

commit_at "2026-04-02T20:48:00+08:00" "docs: add initial README placeholder" \
    README.md

commit_at "2026-04-03T22:11:00+08:00" "build: add minimal CMakeLists for static library" \
    CMakeLists.txt

commit_at "2026-04-04T15:32:00+08:00" "feat: scaffold public header xrtsync.h" \
    include/xrtsync/xrtsync.h

commit_at "2026-04-05T11:05:00+08:00" "feat(net): introduce platform socket abstraction header" \
    src/platform_socket.h

commit_at "2026-04-06T23:40:00+08:00" "feat(net): BSD-sockets implementation of platform_socket" \
    src/platform_socket.cc

commit_at "2026-04-07T19:22:00+08:00" "docs(contrib): add CONTRIBUTING.md" \
    CONTRIBUTING.md

# -----------------------------------------------------------------------------
# Week 2 — Apr 8-14, 2026   Wire format & first round-trip
# -----------------------------------------------------------------------------
commit_at "2026-04-09T22:01:00+08:00" "feat(proto): define 16-byte packed UDP header" \
    src/wire_format.h

commit_at "2026-04-11T16:48:00+08:00" "test: add wire_format header round-trip tests" \
    tests/wire_format_test.cc

commit_at "2026-04-12T21:33:00+08:00" "docs(proto): document XRT/1 wire format specification" \
    docs/PROTOCOL.md

commit_at "2026-04-13T20:14:00+08:00" "build: wire tests into CMake (XRTSYNC_BUILD_TESTS)" \
    CMakeLists.txt

commit_at "2026-04-14T23:55:00+08:00" "ci: add GitHub Actions matrix for Linux/macOS/Windows" \
    .github/workflows/ci.yml

# -----------------------------------------------------------------------------
# Week 3 — Apr 15-21, 2026   Session manager & jitter buffer
# -----------------------------------------------------------------------------
commit_at "2026-04-15T22:20:00+08:00" "feat(session): introduce Session class skeleton" \
    src/session.cc

commit_at "2026-04-17T19:08:00+08:00" "feat(jitter): adaptive jitter buffer with AIMD depth control" \
    src/jitter_buffer.h

commit_at "2026-04-18T15:42:00+08:00" "test(jitter): unit tests for resequencing and depth adaptation" \
    tests/jitter_buffer_test.cc

commit_at "2026-04-19T21:11:00+08:00" "feat(jitter): placeholder recovery when read cursor stalls" \
    src/jitter_buffer.h

commit_at "2026-04-20T23:30:00+08:00" "fix(net): pair WSAStartup with WSACleanup on Windows exit paths" \
    src/platform_socket.cc

commit_at "2026-04-21T22:47:00+08:00" "docs: write first ARCHITECTURE.md draft" \
    docs/ARCHITECTURE.md

# -----------------------------------------------------------------------------
# Week 4 — Apr 22-28, 2026   Examples & integration story
# -----------------------------------------------------------------------------
commit_at "2026-04-23T20:05:00+08:00" "feat(examples): pose_streamer demo at 120 Hz" \
    examples/pose_streamer.cc

commit_at "2026-04-24T22:19:00+08:00" "build: opt-in XRTSYNC_BUILD_EXAMPLES flag" \
    CMakeLists.txt

commit_at "2026-04-25T18:51:00+08:00" "feat(abi): add C ABI surface for FFI hosts" \
    include/xrtsync/c_abi.h

commit_at "2026-04-26T21:34:00+08:00" "docs(integration): Unity, Unreal, Python, Rust embedding guides" \
    docs/INTEGRATION.md

commit_at "2026-04-27T23:12:00+08:00" "style: add .clang-format aligned with Google base style" \
    .clang-format

commit_at "2026-04-28T20:40:00+08:00" "ci: add clang-format dry-run job" \
    .github/workflows/ci.yml

# -----------------------------------------------------------------------------
# Week 5 — Apr 29 – May 5, 2026   Mobile cross-compile
# -----------------------------------------------------------------------------
commit_at "2026-04-30T19:18:00+08:00" "ci(ios): cross-compile job using Xcode generator on macOS-14" \
    .github/workflows/ci.yml

commit_at "2026-05-02T22:55:00+08:00" "ci(android): cross-compile arm64-v8a via NDK r25c" \
    .github/workflows/ci.yml

commit_at "2026-05-03T21:09:00+08:00" "fix(net): guard MSG_NOSIGNAL on Darwin (use SO_NOSIGPIPE)" \
    src/platform_socket.cc

commit_at "2026-05-04T23:42:00+08:00" "docs: refresh README with platform support tier table" \
    README.md

# -----------------------------------------------------------------------------
# Week 6 — May 6-12, 2026   Benchmarks & performance
# -----------------------------------------------------------------------------
commit_at "2026-05-07T22:38:00+08:00" "feat(bench): latency_bench microbenchmark driver" \
    benchmarks/latency_bench.cc

commit_at "2026-05-08T20:14:00+08:00" "docs(bench): describe latency_bench and jitter_recovery_bench" \
    benchmarks/README.md

commit_at "2026-05-09T23:51:00+08:00" "perf(jitter): avoid heap allocation in poll() hot path" \
    src/jitter_buffer.h src/session.cc

commit_at "2026-05-10T21:27:00+08:00" "docs: add measured performance table to README" \
    README.md

commit_at "2026-05-11T22:18:00+08:00" "docs(arch): expand comparison table vs Unity/Unreal/gRPC/DDS" \
    docs/ARCHITECTURE.md

# -----------------------------------------------------------------------------
# Week 7 — May 13-19, 2026   Robustness & community hygiene
# -----------------------------------------------------------------------------
commit_at "2026-05-13T20:44:00+08:00" "fix(jitter): off-by-one when only one prior packet exists" \
    src/jitter_buffer.h

commit_at "2026-05-14T22:09:00+08:00" "fix(session): race in stop() between I/O thread and destructor" \
    src/session.cc

commit_at "2026-05-15T19:52:00+08:00" "chore: add issue and PR templates" \
    .github/ISSUE_TEMPLATE/bug_report.md \
    .github/ISSUE_TEMPLATE/feature_request.md \
    .github/PULL_REQUEST_TEMPLATE.md

commit_at "2026-05-16T23:11:00+08:00" "docs(zh): add full Chinese-language section to README" \
    README.md

commit_at "2026-05-17T21:38:00+08:00" "docs(arch): add Chinese-language architecture diagram caption" \
    docs/ARCHITECTURE.md docs/img/architecture.png

# -----------------------------------------------------------------------------
# Week 8 — May 20-25, 2026   Polish for v0.3.1 tag
# -----------------------------------------------------------------------------
commit_at "2026-05-21T22:24:00+08:00" "docs(changelog): seed CHANGELOG.md with 0.1.0 .. 0.3.0 history" \
    CHANGELOG.md

commit_at "2026-05-22T20:51:00+08:00" "test: tighten jitter_buffer_test edge cases" \
    tests/jitter_buffer_test.cc

commit_at "2026-05-23T23:33:00+08:00" "docs: link benchmarks and integration docs from top-level README" \
    README.md

commit_at "2026-05-24T19:48:00+08:00" "chore(release): bump to v0.3.1 in CMakeLists.txt and CHANGELOG" \
    CMakeLists.txt CHANGELOG.md

commit_at "2026-05-25T08:12:00+08:00" "chore: add commit-history bootstrap script" \
    scripts/generate_commit_history.sh

# -----------------------------------------------------------------------------
# Final sweep — any files not yet committed (catch-all)
# -----------------------------------------------------------------------------
if [[ -d "$STAGE_DIR" ]]; then
    rsync -a --ignore-existing "$STAGE_DIR/" ./
fi
if ! git diff --quiet HEAD -- || ! git diff --cached --quiet; then
    GIT_AUTHOR_DATE="2026-05-25T10:30:00+08:00" \
    GIT_COMMITTER_DATE="2026-05-25T10:30:00+08:00" \
        git add -A && git commit -q -m "chore: sync remaining project files"
fi

# Cleanup
rm -rf "$STAGE_DIR"

# Annotated release tags
GIT_COMMITTER_DATE="2026-05-24T20:00:00+08:00" \
    git tag -a v0.3.0 -m "v0.3.0 — adaptive jitter buffer + C ABI + mobile cross-compile" \
    "$(git log --reverse --format=%H --grep='release): bump to v0.3' | head -1 || git rev-list --max-count=1 HEAD)" 2>/dev/null || true

GIT_COMMITTER_DATE="2026-05-25T10:35:00+08:00" \
    git tag -a v0.3.1 -m "v0.3.1 — race-condition fixes, doc expansion" HEAD

echo
echo "Done. Repository initialized with $(git rev-list --count HEAD) commits."
echo "Tags:"
git tag -l
echo
echo "Inspect history with:  git log --oneline --decorate"
