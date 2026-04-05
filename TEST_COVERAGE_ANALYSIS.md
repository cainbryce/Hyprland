# Hyprland Test Coverage Analysis

## Current State

### Test Infrastructure

Hyprland has a **dual-framework** test setup:

1. **GTest (Unit Tests)** - `tests/` directory
   - Built as `hyprland_gtests`, linked against `hyprland_lib`
   - Coverage flags (`--coverage`) enabled in debug builds
   - **Currently contains only 1 test case**: `TEST(Desktop, reservedArea)` (51 lines)

2. **Hyprtester (Integration Tests)** - `hyprtester/` directory
   - Custom framework that launches Hyprland in headless mode and tests via IPC
   - 18 main test files, 4 client protocol tests, 2 plugin tests
   - ~4,700 lines of integration test code

3. **CI**: Nix-based test VM (4 cores, 8GB RAM, headless 1920x1080) runs both suites

### Source Code Size

| Module | Lines of C++ | Integration Tests | Unit Tests |
|---|---|---|---|
| `protocols/` | 16,376 | 4 client tests | None |
| `managers/` | 14,975 | keybinds, gestures, animations | None |
| `render/` | 9,258 | None | None |
| `desktop/` | 6,972 | window, workspaces, groups, tags, solitary | 1 (ReservedArea) |
| `layout/` | 6,469 | dwindle, master, scroll, snap, layout | None |
| `helpers/` | 5,515 | None | None |
| `config/` | 3,345 | misc (partial) | None |
| `debug/` | 3,281 | hyprctl | None |
| `xwayland/` | 2,943 | None | None |
| `i18n/` | 1,693 | None | None |
| `plugins/` | 1,111 | plugin load/VKB | None |
| `devices/` | 1,114 | None | None |

**Total: ~76,700 lines of source code, ~5,500 lines of tests (~7% test-to-source ratio)**

---

## Recommended Improvements

### Priority 1: Expand GTest Unit Tests for Pure Logic

The GTest infrastructure exists but is almost empty (1 test). Many modules contain pure, deterministic logic that can be unit-tested **without** a running compositor. These are the highest-value, lowest-effort additions:

#### 1.1 Math Expression Evaluation (`src/helpers/math/Expression.cpp`)
- `CExpression::compute()` parses and evaluates math expressions from config values
- Test cases: basic arithmetic, variable substitution, invalid expressions, edge cases (division by zero, empty strings)

#### 1.2 Color Conversion (`src/helpers/Color.cpp`)
- `CHyprColor` supports construction from RGB, hex (uint64_t), and conversions to OkLab/HSL/hex
- Test cases: round-trip conversions (RGB -> OkLab -> RGB), hex encoding/decoding, alpha handling, `stripA()`, `modifyA()`

#### 1.3 Config String Parsing (`src/helpers/MiscFunctions.cpp`)
- `configStringToInt()` - parses hex, decimal, and other integer formats
- `configStringToVector2D()` - parses "WxH" style strings
- `getPlusMinusKeywordResult()` - handles relative "+5" / "-5" syntax
- `stringToPercentage()` - percentage string parsing
- `escapeJSONStrings()` - JSON escaping
- `absolutePath()` - path resolution
- `isDirection()` - direction string validation
- `getWorkspaceIDNameFromString()` - complex workspace string parser
- `truthy()` - truthiness checking for config values

These are **ideal** GTest candidates: pure functions with string input and simple output.

#### 1.4 Match Engines (`src/desktop/rule/matchEngine/`)
- `CRegexMatchEngine` - regex matching with `negative:` prefix support
- `CIntMatchEngine` - integer comparison matching
- `CBoolMatchEngine` - boolean matching
- `CTagMatchEngine` - tag matching
- `CWorkspaceMatchEngine` - workspace ID/name matching

These are small, self-contained classes. Test cases: positive/negative matches, edge cases, invalid input, negative prefix handling.

#### 1.5 TagKeeper (`src/helpers/TagKeeper.cpp`)
- `isTagged()`, `applyTag()`, `removeDynamicTag()`
- Standalone logic with no compositor dependencies
- Test cases: add/remove tags, strict vs non-strict matching, dynamic tags

#### 1.6 Direction Utilities (`src/helpers/math/Direction.hpp`)
- `fromChar()` and `toString()` - trivial but currently untested
- Test cases: all valid chars (r/l/t/u/b/d), invalid char, round-trip

#### 1.7 Byte Operation Literals (`src/helpers/ByteOperations.hpp`)
- `_kB`, `_MB`, `_GB`, `_TB` user-defined literals and conversion functions
- Constexpr, easily testable at compile time or via GTest

---

### Priority 2: Improve Integration Test Coverage for Critical Paths

#### 2.1 Rendering (`src/render/` - 9,258 lines, 0 tests)
The entire rendering subsystem is untested. While full render testing is complex, consider:
- Shader compilation validation tests
- Render pass configuration tests (verify correct pass ordering)
- Damage tracking / damage ring logic tests

#### 2.2 Input Manager (`src/managers/input/` - 2,119 lines, partially tested)
- Keyboard handling is tested via keybinds, but **mouse/touch input routing** is not
- Focus-follows-mouse logic
- Cursor constraint handling
- Drag-and-drop flow

#### 2.3 Protocols (`src/protocols/` - 16,376 lines, 4 client tests)
The largest module has minimal coverage. Priority protocol tests:
- **XDGShell** (891 lines) - window lifecycle, surface commit, popup handling
- **DataDevice** (846 lines) - copy/paste, drag-and-drop
- **OutputManagement** (670 lines) - monitor configuration changes
- **LinuxDMABUF** (647 lines) - buffer import/export

#### 2.4 XWayland (`src/xwayland/` - 2,943 lines, 0 tests)
- X11 window management bridge is completely untested
- XWM event handling, property synchronization, and coordinate translation are prone to edge-case bugs

#### 2.5 Config Manager (`src/config/ConfigManager.cpp` - 3,230 lines, barely tested)
- Config parsing, validation, reload, and watcher logic
- Only indirectly tested through `misc.cpp` integration test
- Add tests for: malformed config handling, config reload, value type coercion, nested sections

---

### Priority 3: Structural Improvements

#### 3.1 Increase GTest Isolation
The current `hyprland_gtests` links against `hyprland_lib`, pulling in the entire compositor. Many unit tests (string parsing, math, match engines) should be testable with minimal dependencies. Consider:
- Creating a `hyprland_core_lib` target with pure utility code
- Linking unit tests against only that, enabling faster compilation and execution

#### 3.2 Add Coverage Reporting to CI
The build already passes `--coverage` flags in debug mode, but there is no coverage reporting step in CI. Add:
- `gcovr` or `lcov` to generate HTML/XML coverage reports
- Coverage thresholds to prevent regressions
- Coverage badge in README

#### 3.3 Fuzz Testing for Parsers
Several parsers handle untrusted input and would benefit from fuzzing:
- Config parser (`ConfigManager.cpp` - 3,230 lines)
- Workspace string parser (`getWorkspaceIDNameFromString`)
- IPC command parser (`HyprCtl.cpp` - 2,400 lines)
- i18n engine (`Engine.cpp` - 1,693 lines)

Consider integrating OSS-Fuzz or libFuzzer targets.

#### 3.4 Add Window Rule Tests
Window rules (`src/desktop/rule/` - multiple files, 642+ lines for applicator alone) are a user-facing feature with complex matching logic. The match engines are unit-testable, but end-to-end rule application (parse rule string -> match window -> apply effect) deserves dedicated integration tests.

---

## Summary of Quick Wins

| Test File to Create | Target Source | Effort | Impact |
|---|---|---|---|
| `tests/helpers/MiscFunctions.cpp` | Config parsing, path resolution, JSON escaping | Low | High |
| `tests/helpers/Color.cpp` | Color construction and conversions | Low | Medium |
| `tests/helpers/TagKeeper.cpp` | Tag management logic | Low | Medium |
| `tests/helpers/math/Expression.cpp` | Math expression parser | Low | Medium |
| `tests/desktop/MatchEngines.cpp` | All 5 match engine types | Low | High |
| `tests/helpers/math/Direction.cpp` | Direction char parsing | Trivial | Low |
| `tests/helpers/ByteOperations.cpp` | Byte literal conversions | Trivial | Low |
| CI coverage reporting | Build system | Medium | High |
