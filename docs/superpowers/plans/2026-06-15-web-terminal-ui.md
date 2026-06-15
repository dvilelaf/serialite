# Web Terminal UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current confusing web terminal with a mobile-first serial console UI.

**Architecture:** Keep the existing embedded `esp_http_server` route and security model. Change only the terminal contract strings and `/terminal` HTML/CSS/JS so the UI exposes three separate operational states: USB, stream, and input lock.

**Tech Stack:** ESP-IDF C, embedded HTML/CSS/JS string in `components/web_server/src/web_server.c`, host C regression tests.

---

### Task 1: Terminal Copy Contract

**Files:**
- Modify: `components/web_server/include/web_terminal_contract.h`
- Modify: `tests/host/test_web_terminal_contract.c`

- [ ] Add host assertions for production terminal vocabulary.
- [ ] Replace internal labels with user-facing labels.
- [ ] Verify `test_web_terminal_contract` fails before implementation and passes after.

### Task 2: Mobile-First Terminal Route

**Files:**
- Modify: `components/web_server/src/web_server.c`

- [ ] Replace `/terminal` layout with compact header, full-screen terminal, and sticky command bar.
- [ ] Remove demo controls from the terminal page.
- [ ] Use `Unlock input` / `Lock input` and focus the input after unlock.
- [ ] Use `ws://` or `wss://` based on `location.protocol`.
- [ ] Replace opaque `Reconnecting` with actionable stream/session status.

### Task 3: Verification and Flash

**Files:**
- Verify repo only.

- [ ] Run `./scripts/dev-verify.sh && git diff --check`.
- [ ] Flash to `/dev/ttyACM0`.
- [ ] Observe AP and absence of immediate reboot markers.
