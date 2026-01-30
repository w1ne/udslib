---
description: How to execute a LibUDS release according to the formal strategy
---

# LibUDS Release Workflow

This workflow ensures that all releases follow the [RELEASE_STRATEGY.md](file:///home/andrii/Projects/mobile_3000/UDS/libuds/docs/RELEASE_STRATEGY.md) and that all documentation remains synchronized.

## Steps

### 1. Preparation & Branching
Ensure you are on a clean `develop` branch and create a release branch.
```bash
git checkout develop
git pull origin develop
git checkout -b release/<version>
```

### 2. Documentation Synchronization
Update the following files to reflect the new version's features and status:
- [CHANGELOG.md](file:///home/andrii/Projects/mobile_3000/UDS/CHANGELOG.md): Add a new version header and document all changes.
- [ROADMAP.md](file:///home/andrii/Projects/mobile_3000/UDS/libuds/docs/ROADMAP.md): Mark completed milestones as `[x]`.
- [SERVICE_COMPLIANCE.md](file:///home/andrii/Projects/mobile_3000/UDS/libuds/docs/SERVICE_COMPLIANCE.md): Update the service support matrix.

### 3. Version Bump
// turbo
Update the version macros in `include/uds/uds_version.h`.
```bash
# Example: Change MAJOR, MINOR, PATCH to match the target release
# and update the UDS_VERSION_STR.
```

### 4. Quality Verification
// turbo
Run the quality check script to ensure all tests pass and formatting is correct.
```bash
./scripts/check_quality.sh
```

### 5. Finalize Release
Once everything passes, commit the changes and finalize the branch.
```bash
git add .
git commit -m "chore: prepare release <version>"
git checkout main
git merge release/<version>
git tag -a v<version> -m "Release v<version>"
git push origin main --tags
git checkout develop
git merge main
git push origin develop
```
