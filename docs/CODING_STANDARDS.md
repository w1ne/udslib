# LibUDS Coding Standards

This document defines the coding standards for the LibUDS project. All contributions must adhere to these rules to ensure consistency, readability, and compatibility across diverse embedded platforms.

## 1. Naming Conventions

### 1.1 Files
- Use all lowercase with underscores (e.g., `uds_core.c`, `isotp_handler.h`).
- Headers should have a unique, lowercase guard: `#ifndef UDS_CORE_H`.

### 1.2 Types
- Structs, Unions, and Enums must be suffixed with `_t` (e.g., `uds_ctx_t`).
- Use `typedef` for all major structures.

### 1.3 Functions
- **Public API**: Must be prefixed with `uds_` and follow `snake_case` (e.g., `uds_init`).
- **Internal/Private**: Must be `static` and prefixed with `uds_internal_` (e.g., `static void uds_internal_process_handler`).
- **Callback Types**: Suffix with `_fn` or `_cb` (e.g., `uds_tp_send_fn`).

### 1.4 Variables
- Use `snake_case` for all variables.
- Function parameters should be descriptive (avoid single-letter names except for loop counters like `i`).
- Member variables in structs do not need prefixes.

### 1.5 Macros and Constants
- All uppercase with a `UDS_` prefix (e.g., `UDS_OK`, `UDS_MAX_BUFFER`).

---

## 2. Documentation and Comments

### 2.1 Public API Header Documentation
- Every public function in a `.h` file must have a **Doxygen** block.
- Must include `@brief`, `@param`, and `@return`.

### 2.2 Implementation Comments
- Use `/* ... */` for block comments.
- Use `//` for short, inline explanations.
- Do not check in commented-out code.

---

## 3. Formatting Rules

LibUDS uses a strict **Clang-Format** configuration (based on Google style).

- **Indentation**: 4 spaces (no tabs).
- **Line Width**: 100 characters.
- **Bracing**: Functions have braces on a new line; Control statements (if/while) keep braces on the same line.
- **Spaces**: No trailing whitespace.

---

## 4. Portability and Safety

### 4.1 Data Types
- Use `<stdint.h>` types exclusively (`uint8_t`, `int32_t`, etc.).
- Avoid `int`, `long`, or `char` (use `uint8_t` or `char` only for actual strings).

### 4.2 Dynamic Memory
- **No `malloc`/`free` in the core library.** All memory must be provided by the caller during initialization.

### 4.3 Error Handling
- Functions should return `int` status codes (`UDS_OK` or negative error codes).
- Use assertions sparingly, and only for logic that should be impossible in production.
