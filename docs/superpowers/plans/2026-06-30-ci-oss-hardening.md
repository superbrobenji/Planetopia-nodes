# CI OSS Hardening + Integration Test Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring Planetopia-nodes CI to OSS parity with its sibling repos by adding least-privilege permissions, SHA-pinned actions, CodeQL C++ scanning, dependency review, Dependabot automation, and fixing a stale integration test.

**Architecture:** Six independent changes applied sequentially. No logic changes to firmware — all changes are build/CI configuration and integration test harness. The existing CMake test infrastructure doubles as the CodeQL build proxy.

**Tech Stack:** GitHub Actions, CMake 3.16+, Python 3, `requests` library, `pyserial`, `pytest`

## Global Constraints

- All YAML workflow files must pass `python3 -c "import yaml; yaml.safe_load(open('FILE').read())"` before commit
- All action references must use full 40-char commit SHA with `# vX.Y.Z` comment on the same line
- Pinned SHAs (resolved 2026-06-30):
  - `actions/checkout@v4` → `34e114876b0b11c390a56381ad16ebd13914f8d5`
  - `actions/upload-artifact@v4` → `ea165f8d65b6e75b540449e92b4886f43607fa02`
  - `github/codeql-action/*@v3.36.2` → `dd903d2e4f5405488e5ef1422510ee31c8b32357`
  - `actions/dependency-review-action@v5.0.0` → `a1d282b36b6f3519aa1f3fc636f609c47dddb294`
- GoogleTest SHA256 (v1.14.0 zip): `1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4`
- Local git identity must be `49689582+superbrobenji@users.noreply.github.com` — verify with `git config user.email` before committing
- Work on `main` branch (no feature branch needed — all changes are independent infra files)

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `.github/workflows/unit-tests.yml` | Modify | Add `permissions: contents: read` to all 3 jobs; SHA-pin checkout + upload-artifact |
| `tests/CMakeLists.txt` | Modify | Add `URL_HASH SHA256=...` to GoogleTest FetchContent_Declare |
| `.github/workflows/codeql.yml` | Create | C++ CodeQL scan using CMake test build as proxy |
| `.github/workflows/dependency-review.yml` | Create | PR-only dependency audit |
| `.github/dependabot.yml` | Create | Weekly auto-PRs for action SHA bumps + submodule updates |
| `tests/integration/harness.py` | Modify | Replace `NotImplementedError` in `send_enrollment_approve` with HTTP POST |
| `tests/integration/requirements.txt` | Modify | Add `requests>=2.31.0` |
| `tests/integration/test_enrollment.py` | Modify | Pass `SERVER_URL`/`ADMIN_KEY` env vars into approval test |

---

## Task 1: Pin GoogleTest SHA256

**Files:**
- Modify: `tests/CMakeLists.txt:12-17`

**Interfaces:**
- Produces: nothing downstream relies on this change

- [ ] **Step 1: Add URL_HASH to FetchContent_Declare**

Replace the existing `FetchContent_Declare` block:

```cmake
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
```

- [ ] **Step 2: Verify CMake configure still works**

```bash
cmake -B tests/build tests/ -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
```

Expected: ends with `-- Build files have been written to: .../tests/build` (no errors). If the zip is already cached, CMake skips the download entirely.

- [ ] **Step 3: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "build: pin GoogleTest v1.14.0 download with SHA256 integrity check"
```

---

## Task 2: Harden unit-tests.yml (permissions + SHA pins)

**Files:**
- Modify: `.github/workflows/unit-tests.yml`

**Interfaces:**
- Produces: nothing downstream depends on this file's content

- [ ] **Step 1: Replace the full file content**

```yaml
name: CI

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4
        with:
          submodules: recursive

      - name: Install CMake
        run: sudo apt-get install -y cmake

      - name: Configure
        run: cmake -B tests/build tests/ -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build tests/build --parallel

      - name: Run tests
        run: ctest --test-dir tests/build --output-on-failure --parallel 4

      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4
        with:
          name: test-results
          path: tests/build/Testing/

  lint-format:
    runs-on: ubuntu-latest
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4

      - name: Install clang-format
        run: sudo apt-get install -y clang-format

      - name: Check formatting
        run: |
          find main/src \( -name '*.cpp' -o -name '*.h' \) \
            ! -path '*/nanopb/*' \
            ! -name 'mesh.pb.h' \
            ! -name 'mesh.pb.c' | \
            xargs clang-format --style=file --dry-run --Werror

  static-analysis:
    runs-on: ubuntu-latest
    permissions:
      contents: read
    steps:
      - uses: actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4
        with:
          submodules: recursive

      - name: Install cppcheck
        run: sudo apt-get install -y cppcheck

      - name: Run cppcheck
        run: |
          cppcheck \
            --error-exitcode=1 \
            --suppress=missingIncludeSystem \
            --suppress=unmatchedSuppression \
            --inline-suppr \
            -I main/src \
            -i main/src/Mesh/serialization/nanopb \
            -i main/src/Mesh/serialization/mesh.pb.c \
            main/src/ 2>&1
```

- [ ] **Step 2: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/unit-tests.yml').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/unit-tests.yml
git commit -m "ci: add least-privilege permissions and SHA-pin actions in unit-tests.yml"
```

---

## Task 3: Add codeql.yml

**Files:**
- Create: `.github/workflows/codeql.yml`

**Interfaces:**
- Consumes: the CMake test build defined in `tests/CMakeLists.txt` (Task 1) — CodeQL intercepts the compiler during this build to build its analysis database
- Produces: nothing downstream depends on this

- [ ] **Step 1: Create the file**

```yaml
name: CodeQL

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 2 * * 1'

jobs:
  analyze:
    name: Analyze (cpp)
    runs-on: ubuntu-latest
    permissions:
      security-events: write
      packages: read
      actions: read
      contents: read
    steps:
      - uses: actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4
        with:
          submodules: recursive

      - uses: github/codeql-action/init@dd903d2e4f5405488e5ef1422510ee31c8b32357 # v3.36.2
        with:
          languages: cpp
          build-mode: manual
          queries: security-and-quality

      - name: Install CMake
        run: sudo apt-get install -y cmake

      - name: Build for CodeQL
        run: |
          cmake -B tests/build tests/ -DCMAKE_BUILD_TYPE=Release
          cmake --build tests/build --parallel

      - uses: github/codeql-action/analyze@dd903d2e4f5405488e5ef1422510ee31c8b32357 # v3.36.2
        with:
          category: '/language:cpp'
```

- [ ] **Step 2: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/codeql.yml').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/codeql.yml
git commit -m "ci: add CodeQL C++ security analysis workflow"
```

---

## Task 4: Add dependency-review.yml

**Files:**
- Create: `.github/workflows/dependency-review.yml`

**Interfaces:**
- Produces: nothing downstream depends on this

- [ ] **Step 1: Create the file**

```yaml
name: Dependency Review

on:
  pull_request:
    branches: [main]

permissions:
  contents: read

jobs:
  dependency-review:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4
      - uses: actions/dependency-review-action@a1d282b36b6f3519aa1f3fc636f609c47dddb294 # v5.0.0
```

- [ ] **Step 2: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/dependency-review.yml').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/dependency-review.yml
git commit -m "ci: add dependency-review workflow for PR auditing"
```

---

## Task 5: Add dependabot.yml

**Files:**
- Create: `.github/dependabot.yml`

**Interfaces:**
- Produces: nothing downstream depends on this

- [ ] **Step 1: Create the file**

```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: /
    schedule:
      interval: weekly
    labels:
      - dependencies
      - ci

  - package-ecosystem: gitsubmodule
    directory: /
    schedule:
      interval: weekly
    labels:
      - dependencies
      - submodule
```

- [ ] **Step 2: Validate YAML syntax**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/dependabot.yml').read()); print('OK')"
```

Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add .github/dependabot.yml
git commit -m "ci: add Dependabot config for actions and submodule auto-updates"
```

---

## Task 6: Fix stale integration test code

**Files:**
- Modify: `tests/integration/harness.py:84-96`
- Modify: `tests/integration/requirements.txt`
- Modify: `tests/integration/test_enrollment.py:1-10,43-59`

**Interfaces:**
- Consumes: `POST /api/v1/enrollments/{mac}/approve` on motionSensorServer, authenticated via `Authorization: Bearer <admin_key>` header (see `motionSensorServer/server/orchestrator/mesh/api.go:119-128`)
- Produces: nothing downstream depends on this file's internals

- [ ] **Step 1: Fix `send_enrollment_approve` in harness.py**

Replace lines 84–96 (the `get_public_key` method stays; only `send_enrollment_approve` changes):

```python
    def send_enrollment_approve(self, mac: bytes, pub_key: bytes, server_url: str, admin_key: str) -> None:
        """Approve enrollment via server HTTP API (POST /api/v1/enrollments/{mac}/approve)."""
        import requests
        mac_str = ':'.join(f'{b:02X}' for b in mac)
        url = f"{server_url}/api/v1/enrollments/{mac_str}/approve"
        resp = requests.post(url, headers={"Authorization": f"Bearer {admin_key}"}, timeout=5.0)
        resp.raise_for_status()
```

The signature change (two new params `server_url`, `admin_key`) is intentional — callers must supply connection details rather than relying on implicit state.

- [ ] **Step 2: Add `requests` to requirements.txt**

Replace the full file:

```
pyserial==3.5
pytest==7.4.0
requests>=2.31.0
```

- [ ] **Step 3: Update test_enrollment.py to supply env vars**

Add `SERVER_URL` and `ADMIN_KEY` constants after the existing `NODE_PORT` line, and update `test_server_approval_triggers_join_ack` to pass them:

Full updated file:

```python
"""
Integration test: enrollment flow.
Requires: master ESP32 on MASTER_PORT, node ESP32 on NODE_PORT.
Requires: motionSensorServer running at SERVER_URL with ADMIN_KEY set.
"""
import pytest
import os
import time
from harness import Node

MASTER_PORT = os.getenv('MASTER_PORT', '/dev/ttyUSB0')
NODE_PORT   = os.getenv('NODE_PORT',   '/dev/ttyUSB1')
SERVER_URL  = os.getenv('SERVER_URL',  'http://localhost:8080')
ADMIN_KEY   = os.getenv('ADMIN_KEY',   '')


@pytest.fixture(scope='module')
def master():
    n = Node(MASTER_PORT, 'master')
    yield n
    n.close()


@pytest.fixture(scope='module')
def node():
    n = Node(NODE_PORT, 'node')
    yield n
    n.close()


@pytest.mark.integration
def test_node_prints_public_key_on_boot(node):
    """New node should print its public key to serial for provisioning."""
    pub_key = node.get_public_key(timeout=10.0)
    assert pub_key is not None, "Node did not print PLANETOPIA_PUBKEY"
    assert len(pub_key) == 32


@pytest.mark.integration
def test_master_receives_enrollment_request(master, node):
    """Master should relay OP_ENROLLMENT_REQ to server (serial) within 15s."""
    enrolled = master.wait_for_log('Enrollment request complete, relaying to server', timeout=15.0)
    assert enrolled, "Master did not receive enrollment request from node"


@pytest.mark.integration
def test_server_approval_triggers_join_ack(master, node):
    """
    Approve enrollment via server HTTP API.
    Node should receive JOIN_ACK and log 'Enrollment approved'.
    """
    pub_key = node.get_public_key(timeout=5.0)
    assert pub_key is not None

    test_mac = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01])
    master.send_enrollment_approve(test_mac, pub_key, SERVER_URL, ADMIN_KEY)

    approved = node.wait_for_log('Enrollment approved', timeout=5.0)
    assert approved, "Node did not receive JOIN_ACK after server approval"


@pytest.mark.integration
def test_enrolled_node_stops_broadcasting_requests(node):
    """After enrollment, node should not re-broadcast enrollment requests."""
    time.sleep(12.0)
    new_request = node.wait_for_log('Enrollment request sent', timeout=2.0)
    assert not new_request, "Enrolled node is still broadcasting enrollment requests"
```

Note: the original `test_master_receives_enrollment_request` had `_node` (underscore) as a param — this was a bug (fixture not injected). Fixed to `node`.

- [ ] **Step 4: Validate Python syntax**

```bash
python3 -m py_compile tests/integration/harness.py && echo "harness OK"
python3 -m py_compile tests/integration/test_enrollment.py && echo "test OK"
```

Expected: both print OK with no errors.

- [ ] **Step 5: Validate pytest can collect the tests (no hardware needed)**

```bash
cd tests/integration && pip install pyserial pytest requests -q && \
  python3 -m pytest test_enrollment.py --collect-only 2>&1 | grep "test session\|collected\|ERROR"
```

Expected output contains `4 tests collected` with no `ERROR` lines. Tests are marked `@pytest.mark.integration` so they won't run without hardware — collection alone verifies imports and fixtures resolve.

- [ ] **Step 6: Commit**

```bash
git add tests/integration/harness.py tests/integration/requirements.txt tests/integration/test_enrollment.py
git commit -m "fix(integration): update enrollment approval to use server HTTP API"
```
