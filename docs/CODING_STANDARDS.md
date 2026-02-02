# UDSLib Coding Standards

This guide defines the coding rules for UDSLib. Follow them to ensure the library remains portable, readable, and consistent.

## 1. Naming Conventions

### 1.1 Files
- Use snake_case (e.g., `uds_core.c`).
- Use unique, lowercase header guards: `#ifndef UDS_CORE_H`.

### 1.2 Types
- Suffix Structs, Unions, and Enums with `_t` (e.g., `uds_ctx_t`).
- Use `typedef` for all major structures.

### 1.3 Functions
- **Public API**: Prefix with `uds_` (e.g., `uds_init`).
- **Internal**: Make them `static` and prefix with `uds_internal_` (e.g., `static void uds_internal_process_handler`).
- **Callbacks**: Suffix with `_fn` or `_cb` (e.g., `uds_tp_send_fn`).

### 1.4 Variables
- Use `snake_case`.
- Use descriptive names for parameters, avoiding single letters (except loop counters).
- Do not prefix struct members.

### 1.5 Macros and Constants
- Use all uppercase with a `UDS_` prefix (e.g., `UDS_OK`, `UDS_MAX_BUFFER`).

---

## 2. Documentation and Comments

### 2.1 Public API Header
- Document every public function in the `.h` file using a **Doxygen** block.
- Include `@brief`, `@param`, and `@return`.

### 2.2 Implementation
- Use `/* ... */` for block comments.
- Use `//` for short, inline explanations.
- Remove dead code; do not comment it out.

---

## 3. Formatting

We enforce formatting via **Clang-Format** (Google style).

- **Indentation**: 4 spaces (no tabs).
- **Line Width**: 100 characters.
- **Bracing**: New line for functions; same line for control statements.
- **Spaces**: No trailing whitespace.

---

## 4. Portability and Safety

### 4.1 Data Types
- Use `<stdint.h>` types (`uint8_t`, `int32_t`).
- Avoid `int`, `long`, or `char` (use `uint8_t` or `char` only for actual strings).

### 4.2 Dynamic Memory
- **Do not use `malloc` or `free`.** The caller must provide all memory buffers.

### 4.3 Error Handling
- Return `int` status codes (`UDS_OK` or negative error codes).
- Use assertions only for logic that should never happen in production.
