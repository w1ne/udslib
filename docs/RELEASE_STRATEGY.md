# LibUDS Release Strategy

This document defines the development workflow, versioning, and release procedures for LibUDS.

## 1. Gitflow Workflow

We follow a simplified **Gitflow** model to manage development and releases.

### Branches
- **`main`**: Production-ready code. Every commit is a tagged release.
- **`develop`**: Integration branch for features. All features merge here first.
- **`feature/*`**: Individual features or bugfixes. Created from `develop`, merged back to `develop`.
- **`release/*`**: Preparation for a new production release. Created from `develop`, merged to `main` and `develop`.
- **`hotfix/*`**: Urgent fixes for production. Created from `main`, merged to `main` and `develop`.

---

## 2. Versioning Policy

LibUDS follows **Semantic Versioning 2.0.0** (SemVer).

Format: `MAJOR.MINOR.PATCH`

- **MAJOR**: Incompatible API changes.
- **MINOR**: Backward-compatible functionality additions.
- **PATCH**: Backward-compatible bug fixes.

---

## 3. CI/CD Pipeline

Every push to `develop` or `main` triggers the automated CI pipeline.

### Stages
1. **Quality Gate**: 
   - Linting (`clang-format`)
   - Static Analysis (`cppcheck`)
2. **Build**: 
   - Target 1: POSIX (GCC/Clang)
   - Target 2: Zephyr (native_sim)
3. **Tests**:
   - CMocka unit tests.
   - Python integration tests (Regression suite).

---

## 4. Release Process

1. **Freeze**: Create a `release/x.y.z` branch from `develop`.
2. **Audit & Documentation**: 
   - Update `CHANGELOG.md` with all significant changes.
   - Update `ROADMAP.md` to reflect completed milestones.
   - Synchronize `SERVICE_COMPLIANCE.md` matrix with the current feature set.
   - Bump version in `include/uds/uds_version.h`.
3. **Validate**: CI must pass on the release branch.
4. **Publish**: 
   - Merge `release/x.y.z` into `main`.
   - Create a git tag: `git tag -a vx.y.z -m "Release vx.y.z"`.
   - Merge `main` back into `develop`.
5. **Artifacts**: GitHub Actions automatically packages the library headers and compiled artifacts for the release tag.
