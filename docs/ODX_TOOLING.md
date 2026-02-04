# ODX Tooling Guide

This guide describes how to use UDSLib code generation tools.

## Overview

UDSLib includes two automation tools:

1. **`odx_to_c.py`**: Generates DID tables from ODX diagnostic database files.
2. **`generate_tests.py`**: Scaffolds Python integration tests from service specifications.

## ODX-to-C Code Generator

### Functionality

Generates C code from ODX (Open Diagnostic Data Exchange) files:
- Parses `<DIAG-DATA-DICTIONARY>` elements.
- Extracts Data Identifiers (DIDs) with read/write permissions.
- Generates `uds_did_table.c` and `uds_did_table.h`.

### Usage

```bash
# Basic generation
python tools/odx_to_c.py vehicle_spec.odx --output src/generated/

# Verify mode (for CI checks)
python tools/odx_to_c.py vehicle_spec.odx --output src/generated/ --verify
```

### Workflow

1. **Export ODX**: Save your diagnostic specification (from Vector CANdelaStudio, ETAS ISOLAR, etc.) as an ODX file.
2. **Generate C code**:
   ```bash
   python tools/odx_to_c.py docs/examples/sample_vehicle.odx --output examples/generated/
   ```
3. **Include in build**:
   ```c
   #include "generated/uds_did_table.h"
   
   uds_config_t config = {
       .did_table = {
           .entries = generated_did_table,
           .count = generated_did_table_count,
       },
       // ...
   };
   ```

### Generated Code Example

Input ODX:
```xml
<DATA-OBJECT-PROP>
    <SHORT-NAME>VIN</SHORT-NAME>
    <ID>0xF190</ID>
    <BIT-LENGTH>136</BIT-LENGTH>
</DATA-OBJECT-PROP>
```

Output C:
```c
const uds_did_entry_t generated_did_table[] = {
    {0xF190, 17, DID_READ, NULL, NULL},  // VIN
};
```

## Python Test Generator

### Functionality

Creates pytest-based integration test skeletons with:
- Positive response cases.
- Negative response cases (NRC validation).
- Session-specific stubs.

### Usage

```bash
# Generate test for a specific service
python tools/generate_tests.py --service 0x22 --output tests/integration/

# Generate tests for all known services
python tools/generate_tests.py --all --output tests/integration/
```

### Generated Test Example

```python
def test_read_data_by_id_positive(uds):
    """Test Read Data By Identifier - Positive Response"""
    request = bytes([0x22, 0xF1, 0x90])
    response = uds.send(request, timeout=1.0)
    
    assert response is not None, "No response received"
    assert response[0] == 0x62, "Expected positive response"
```

## CI/CD Setup

### GitHub Actions (`.github/workflows/ci.yml`)

```yaml
- name: Verify generated code is up-to-date
  run: |
    python tools/odx_to_c.py docs/vehicle_spec.odx --output src/generated/ --verify
    if [ $? -ne 0 ]; then
      echo "❌ Generated DID tables are out of sync with ODX!"
      exit 1
    fi
```

### Script (`scripts/check_quality.sh`)

```bash
echo "=== Verifying ODX Code Generation ==="
python3 tools/odx_to_c.py docs/vehicle_spec.odx --output src/generated/ --verify
```

## Limitations

The included `odx_to_c.py` is a **minimal parser**:
- Extracts basic DIDs only.
- Does not handle complex ODX features (computed parameters, encoding).
- Does not validate ODX schema.

### Recommendation for Production
For full ODX 2.2 support, install `odxtools`:

```bash
pip install odxtools
```

Then modify `odx_to_c.py` to use `odxtools.load_file()`.

## Common Issues

### "No DIDs found in ODX file"
- Ensure the file contains `<DIAG-DATA-DICTIONARY-SPEC>` elements.
- Verify XML namespace declarations.
- Validate the ODX file in a commercial tool.

### "Generated code differs from existing files"
Your generated files do not match the ODX source.
1. Regenerate: `python tools/odx_to_c.py ...`
2. Update the ODX source.
3. (Temporary) Disable the verify step.

## Best Practices

1. **Version Control**: Commit both ODX files and generated code.
2. **Read-Only**: Treat generated files as read-only.
3. **CI Enforcement**: Fail builds if verification fails.
4. **Incremental Adoption**: Start with a single ODX file.
