# Programming Style

General defaults for C/C++ and Qt. Project-specific rules in `code-styles.md`, `qt-guidelines.md`, and `docs/_invariants/` take precedence when they conflict.

1. Prefer value semantics and RAII over raw pointers and manual memory management.
2. Use OOP for entities with identity and state; prefer free functions and namespaces for pure transformations.
3. Write pure functions where possible — modify only return values, never input parameters or global state.
4. Follow DRY, KISS, and YAGNI.
5. Use strong typing everywhere — explicit types for function returns, variables, and parameters. Avoid `void*` and unchecked casts.
6. Check whether logic already exists before writing new code.
7. Avoid `auto` when the concrete type is not immediately obvious from context.
8. Never use default parameter values for public APIs — make all parameters explicit at call sites.
9. Create proper type definitions (structs, enums) for complex data — do not use `QVariant` or `std::any` as a substitute.
10. All includes at the top of the file, grouped by project / third-party / standard library.
11. Write simple single-purpose functions — no multi-mode behavior, no flag parameters that switch logic.
12. Prefer `const` correctness: const methods, const references, const pointers where applicable.
13. Prefer compile-time polymorphism (templates, `if constexpr`) over runtime polymorphism (virtual functions) when the set of types is known at compile time.
14. Use `enum class` over plain `enum` for type safety.
15. Avoid global mutable state; use singletons sparingly and only when truly necessary.
