# SonarCloud Conventions

Rules enforced on PR analysis and future code.

## C++ Rules (SonarCloud `cpp:*`)

### S3608: Explicit Lambda Captures
**Do not** use default captures `[&]` or `[=]`. Always list captured variables explicitly.
```cpp
// ❌ Wrong
auto fn = [&]() { return st.field + f.mono->FontSize; };

// ✅ Correct
auto fn = [&st, &f]() { return st.field + f.mono->FontSize; };
```

### S5945: C-Style Arrays
Use `std::array` or `std::vector` instead of C-style arrays.
```cpp
// ❌ Wrong
static constexpr const char *kItems[] = {"A", "B", "C"};

// ✅ Correct
static constexpr std::array<const char *, 3> kItems = {"A", "B", "C"};
```

**Exception:** `char buf[N]` members in structs that are passed to ImGui `InputText` may remain as C-style arrays until a sync-to-`std::string` wrapper is in place. New code should prefer `std::string` with a temp buffer helper.

### S3642: Enum Class
Use `enum class` instead of plain `enum`.
```cpp
// ❌ Wrong
enum Tab { General = 0, Appearance };

// ✅ Correct
enum class Tab : int { General = 0, Appearance };
```

### S125: Commented-Out Code
Do not leave commented-out code or CSS blocks in production source. CSS reference comments belong in architecture docs, not in `.cpp`/`.h` files.

### S6004: Init-Statement in `if`
Use C++17 init-statement to scope variables to the `if` block.
```cpp
// ❌ Wrong
const auto cmd = compute();
if (cmd == OpenSettings) { ... }

// ✅ Correct
if (const auto cmd = compute(); cmd == OpenSettings) { ... }
```

### S5827: Redundant Type with `auto`
Use `auto` when the type is explicitly stated in the initializer.
```cpp
// ❌ Wrong
const int steps = static_cast<int>(std::ceil(w / s));

// ✅ Correct
const auto steps = static_cast<int>(std::ceil(w / s));
```

### S3358: Nested Conditional Operator
Extract nested ternary operators into independent statements or use lookup arrays.
```cpp
// ❌ Wrong
const char *n = (s == 1) ? "1" : (s == 2) ? "2" : "3";

// ✅ Correct
static constexpr std::array<const char *, 4> k = {"", "1", "2", "3"};
const char *n = k[s];
```

### S6009: `std::string_view` Instead of `const std::string&`
Prefer `std::string_view` for read-only string parameters.
```cpp
// ❌ Wrong
void Parse(const std::string &value);

// ✅ Correct
void Parse(std::string_view value);
```

### S6177: `using enum`
Use `using enum` (C++20) to reduce verbosity in functions that heavily reference enum values.
```cpp
// ✅ Correct
void ToString(EditorStartupBehavior value) {
    using enum EditorStartupBehavior;
    switch (value) {
    case LastProject: return "last_project";
    case WelcomeScreen: return "welcome";
    }
}
```

### S2738: Specific Catch
Catch specific exception types, not `...`.
```cpp
// ❌ Wrong
try { ... } catch (...) { return nullopt; }

// ✅ Correct
try { ... } catch (const std::invalid_argument &) { return nullopt; }
                     catch (const std::out_of_range &) { return nullopt; }
```

### S995 / S5350: Pointer-to-Const Parameters
Make pointer parameters `const` when the function does not modify the pointed-to object.
```cpp
// ❌ Wrong
void PushFont(::ImFont* f);

// ✅ Correct
void PushFont(const ::ImFont* f);
// If the API requires non-const, use const_cast internally.
```

### S3628: Raw String Literals
For strings with heavy escaping (regex, JSON), prefer raw string literals with an appropriate delimiter.
```cpp
// ✅ Correct
out += R"(\\)";
out += R"(\")";
```

### S1135: TODO Comments
Either complete the TODO or reference a tracked issue. Stale TODOs are flagged.
```cpp
// ✅ Acceptable
// HORO-123: Replace with real filesystem check after VFS layer lands.
```

### S5281: Format String as Literal
`snprintf`/`printf`-family format strings must be string literals, not variables. Design APIs to avoid user-supplied format strings.
```cpp
// ❌ Wrong
std::snprintf(buf, sizeof(buf), userSuffix, value);

// ✅ Correct (the format is always literal)
std::snprintf(buf, sizeof(buf), "%d%%", value);
```

### S6494: `std::format` Instead of `snprintf`
In C++20, prefer `std::format` or `std::format_to_n` over `snprintf`.
```cpp
// ❌ Wrong
std::snprintf(dst, dstSize, "%s", src.c_str());

// ✅ Correct (C++20)
auto result = std::format_to_n(dst, dstSize - 1, "{}", src);
```

## General

- Run `cmake --build build/skeleton -j$(nproc) && ctest --test-dir build/skeleton` before pushing.
- New code must not reintroduce already-fixed rule violations.
- Large-CR (S3776 / cognitive complexity) and large-param (S107) functions should be extracted into smaller helpers when practical, but are not blocking for mechanical rule fixes.
