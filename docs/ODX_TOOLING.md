# ODX Tooling Guide

This document explains how to use LibUDS code generation tools to reduce manual configuration.

## Overview

LibUDS provides two automation tools:

1. **`odx_to_c.py`**: Generates DID tables from ODX diagnostic database files
2. **`generate_tests.py`**: Scaffolds Python integration tests from service specifications

## ODX-to-C Code Generator

### What It Does

Automatically generates C code from ODX (Open Diagnostic Data Exchange) files:
- Parses `<DIAG-DATA-DICTIONARY>` elements
- Extracts Data Identifiers (DIDs) with read/write permissions
- Generates `uds_did_table.c` and `uds_did_table.h`

### Usage

```bash
# Basic generation
python tools/odx_to_c.py vehicle_spec.odx --output src/generated/

# Verify mode (check for code drift in CI)
python tools/odx_to_c.py vehicle_spec.odx --output src/generated/ --verify
```

### Example Workflow

1. **Export ODX from your diagnostic tool** (Vector CANdelaStudio, ETAS ISOLAR, etc.)

2. **Generate C code**:
   ```bash
   python tools/odx_to_c.py docs/examples/sample_vehicle.odx --output examples/generated/
   ```

3. **Include in your build**:
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

From this ODX snippet:
```xml
<DATA-OBJECT-PROP>
    <SHORT-NAME>VIN</SHORT-NAME>
    <ID>0xF190</ID>
    <BIT-LENGTH>136</BIT-LENGTH>
</DATA-OBJECT-PROP>
```

Generates:
```c
const uds_did_entry_t generated_did_table[] = {
    {0xF190, 17, DID_READ, NULL, NULL},  // VIN
};
```

## Python Test Generator

### What It Does

Scaffolds pytest-based integration tests with:
- Positive response test cases
- Negative response test cases (NRC validation)
- Session-specific test stubs

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

## CI/CD Integration

### Add to `.github/workflows/ci.yml`:

```yaml
- name: Verify generated code is up-to-date
  run: |
    python tools/odx_to_c.py docs/vehicle_spec.odx --output src/generated/ --verify
    if [ $? -ne 0 ]; then
      echo "❌ Generated DID tables are out of sync with ODX!"
      echo "Run: python tools/odx_to_c.py docs/vehicle_spec.odx --output src/generated/"
      exit 1
    fi
```

### Add to `scripts/check_quality.sh`:

```bash
echo "=== Verifying ODX Code Generation ==="
python3 tools/odx_to_c.py docs/vehicle_spec.odx --output src/generated/ --verify
```

## Limitations

The current `odx_to_c.py` is a **minimal parser** for demonstration:
- Supports basic DID extraction only
- No complex ODX features (computed parameters, encoding, etc.)
- No validation of ODX schema

### For Production Use

Consider using the full `odxtools` library:

```bash
pip install odxtools
```

Then modify `odx_to_c.py` to use `odxtools.load_file()` for proper XML namespace handling and full ODX 2.2 support.

## Troubleshooting

### "No DIDs found in ODX file"

- Check that the ODX file contains `<DIAG-DATA-DICTIONARY-SPEC>` elements
- Verify XML namespace declarations
- Try opening the ODX in Vector CANdela/ETAS to confirm validity

### "Generated code differs from existing files"

Your hand-written DID tables are out of sync with the ODX source. Options:
1. Regenerate: `python tools/odx_to_c.py ... --output src/generated/`
2. Update ODX to match code
3. Disable verify step temporarily

## Best Practices

1. **Version Control**: Commit both ODX files and generated code
2. **Read-Only Generated Files**: Add warning comments to prevent manual edits
3. **CI Enforcement**: Fail builds if verification fails
4. **Incremental Adoption**: Start with one ODX file, expand gradually

## Example Files

See `docs/examples/sample_vehicle.odx` for a minimal working example.
