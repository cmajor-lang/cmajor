# AGENTS.md

Cmajor is a C-family language for fast, portable audio/DSP code. This repo holds the compiler, runtime, tools, standard library and docs.

## Build & test

```bash
git submodule update --init --recursive
cmake -Bbuild -GNinja -DCMAKE_BUILD_TYPE=Release .    # or -GXcode / a Visual Studio generator
ninja -C build
```

- Options: `BUILD_CMAJ`, `BUILD_CMAJ_LIB`, `BUILD_EXAMPLES` (default ON), `BUILD_PLUGIN` (default OFF, needs `JUCE_PATH`).
- Windows: only Release/RelWithDebInfo work (LLVM runtime); use `-DBUILD_PLUGIN=OFF` on arm64.
- Tests: `cmaj test <file-or-folder>` — e.g. `cmaj test tests` runs everything. Use `--singleThread` for `performance_tests`. See `tests/Tests README.md`.

## Layout

- `modules/compiler/src/` — AST, passes, transformations, validation, backends (`LLVM`, `WebAssembly`, `CPlusPlus`)
- `modules/{playback,scripting,server,plugin}/` — audio playback, JS engine + test runner, patch dev server, CLAP plugin wrapper
- `tools/` — `command/` (the `cmaj` CLI), `CmajDLL/`, `CmajPlugin/`, `wasm_compiler/`
- `include/cmajor/` — public C++ API; `include/choc/` — CHOC submodule
- `standard_library/`, `javascript/`, `examples/`, `tests/`, `docs/`

For the language itself, read `docs/Cmaj Language Guide.md` and the `standard_library/*.cmajor` sources rather than guessing syntax. File formats: `docs/Cmaj Patch Format.md`, `docs/Cmaj Test File Format.md`.

## Generated files and versioning

- Embedded assets (JS, public headers, std library) are generated and committed. After changing any of them, run `tools/scripts/create_embedded_files.py` and commit the output.
- After changing the standard library, first run `cmaj generate --target=module --output=<repo> std_library`, then the script above.
- Bump `AST::getBinaryProgramHeader()` (`modules/compiler/src/AST/cmaj_AST_Utilities.h`) when AST class fields change.
- Bump the `cmajor_getEntryPointsV` symbol for breaking COM API changes. It appears in three places: `include/cmajor/COM/cmaj_Library.h`, `tools/CmajDLL/Source/cmaj_ExportedFunctions.cpp` and `tools/CmajDLL/exports.def`.

## C++ style (CHOC style)

- Files: `cmaj_` prefix; include guards `CMAJ_FILENAME_HEADER_INCLUDED`; 4-space indent, no tabs.
- Naming: `PascalCase` types and template params, `camelCase` functions/variables, `snake_case` constants and namespaces. Use `is`/`has`/`get` prefixes, and no trailing `_` on members.
- Put a space before `(` when there are arguments and none for empty parens: `foo (a, b)`, `foo()`, `void bar (int x)`, `Bar()`.
- Put a space after `!`: `if (! ptr)`.
- Use almost-always-`auto` for locals; write `return {};` for an empty string.
- Open braces on the same line for functions and control flow, on a new line for classes. Short one-line `if`s take no braces.
- Leave a blank line after a closing `}` when a statement follows, and between function definitions.
- Use `///` for doc comments and close namespaces with `} // namespace cmaj::foo`.
- Prefer RAII and containers (no raw `malloc`/`free`), plus `[[nodiscard]]` and `constexpr`.
- Library code in `modules/` uses `.h`/`.cpp` pairs. In `tools/`, put new classes header-only with inline methods; don't add new `.cpp` files.
