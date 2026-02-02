# UDSLib Release Strategy

This document outlines the development workflow, versioning, and release procedures.

## 1. Workflow

We use a simplified **Gitflow** model.

### Branches
- **`main`**: Production code. Every commit is a tagged release.
- **`develop`**: Integration branch. Features merge here first.
- **`feature/*`**: Feature or bugfix branches. Created from and merged back to `develop`.
- **`release/*`**: Release preparation. Created from `develop`, merged to `main` and `develop`.
- **`hotfix/*`**: Urgent production fixes. Created from `main`, merged to `main` and `develop`.

## 2. Versioning

We use **Semantic Versioning 2.0.0** (SemVer).

Format: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes.
- **MINOR**: Backward-compatible newly functionality.
- **PATCH**: Backward-compatible bug fixes.

## 3. CI/CD

Pushes to `develop` or `main` trigger the CI pipeline.

### Stages
1. **Quality Gate**:
   - Linting (`clang-format`).
   - Static Analysis (`cppcheck`).
2. **Build**:
   - POSIX (GCC/Clang).
   - Zephyr (`native_sim`).
3. **Tests**:
   - CMocka unit tests.
   - Python integration tests.

### 4. Release Process

1. **Freeze**: Create a `release/x.y.z` branch from `develop`.
2. **Audit & Documentation**:
   - **MISRA-C Audit**: Run `scripts/check_misra.sh` and ensure 100% compliance.
   - Update `CHANGELOG.md`.
   - Update `ROADMAP.md` milestones.
   - Synchronize `SERVICE_COMPLIANCE.md`.
   - Bump version in `include/uds/uds_version.h`.
3. **Validate**: Ensure CI passes on the release branch.
4. **Publish**:
   - Merge `release/x.y.z` into `main`.
   - Tag the release: `git tag -a vx.y.z -m "Release vx.y.z"`.
   - Merge `main` back into `develop`.
5. **Artifacts**: GitHub Actions automatically packages headers and binaries for the release tag.
