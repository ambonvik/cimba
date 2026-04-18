# Test harness for asserts, where a non-zero exit code may (or may not) be a "success".
# Usage: python test_assert.py <expected_trip: true | false> <executable> [args...]
import subprocess
import sys

should_trip = sys.argv[1].lower() == "true"
cmd = sys.argv[2:]

try:
    result = subprocess.run(cmd, capture_output=True)
    did_trip = result.returncode != 0
    if should_trip == did_trip:
        sys.exit(0)
    else:
        print(f"Expected {should_trip}, got {did_trip}")
        sys.exit(1)

except Exception as e:
    print(f"Failed to execute test: {e}")
    sys.exit(1)