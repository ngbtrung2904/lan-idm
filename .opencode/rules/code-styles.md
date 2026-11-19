# C/C++ Code Style

## Naming Conventions

1. Member variables: `mFoo` (prefix `m` + PascalCase).
2. Local variables and parameters: `fooBar` (camelCase).
3. Functions and methods: `fooBar()` (camelCase).
4. Class names: `FooBar` (PascalCase).
5. Constants and enums: `kFooBar` (prefix `k` + PascalCase) or `ALL_CAPS` for preprocessor macros.
6. Namespaces: `foobar` (lowercase).
7. Files: `FooBar.h` / `FooBar.cpp` (PascalCase).

## General Style

1. Do not extract code into new files unless explicitly asked or the code is reused by 2+ files. Single-use helpers must be co-located in the file that uses them.
2. Do not extract single-use logic into named helper functions within the same file unless it significantly improves readability.
3. Prefer `const` correctness everywhere — const methods, const references, const pointers where applicable.
4. Use RAII for all resource management — no raw `new`/`delete`, no manual `malloc`/`free`.
5. Prefer smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw pointers for ownership.
6. Use `nullptr` instead of `NULL` or `0`.
7. Prefer references over pointers for function parameters that cannot be null.
8. Avoid C-style casts — use `static_cast`, `dynamic_cast`, `const_cast`, `reinterpret_cast` explicitly.
9. Use `override` and `final` keywords for virtual function declarations.
10. Prefer `enum class` over plain `enum`.
11. Use `auto` when the type is obvious from the right-hand side, but avoid `auto` for return types or when the type is not immediately clear.
12. All includes at the top of the file, grouped: project headers, then third-party, then standard library.
13. Use forward declarations in headers when possible to reduce compile-time dependencies.
14. Prefer passing by `const&` over passing by value for non-trivial types.
15. Do not add unnecessary `this->` prefixes unless resolving ambiguity.

## Qt-Specific

1. Use `Q_OBJECT` macro in all classes that use signals/slots or Qt meta-object features.
2. Prefer Qt's `connect()` syntax with function pointers over `SIGNAL()`/`SLOT()` macros.
3. Use `QString` over `std::string` for UI-facing text; `std::string` is acceptable for non-UI logic.
4. Use `Q_NULLPTR` or `nullptr` consistently.
5. Prefer range-based for loops with Qt containers.
6. Use `Q_DECLARE_METATYPE` for custom types used in QVariant or queued connections.
