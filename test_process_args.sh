#!/bin/bash

# Path to your compiled executable
PBF_EXEC="./bin/pbf"

# Directory for test output
TEST_DIR="./test_output"
mkdir -p "$TEST_DIR"

# Function to check test results
run_test() {
    local test_name="$1"
    local command="$2"
    local expected_exit="$3"
    local expected_output="$4"
    local expected_err="$5"

    echo "Running test: $test_name"

    # Execute the command
    eval "$command" > "$TEST_DIR/${test_name}.out" 2> "$TEST_DIR/${test_name}.err"
    local exit_status=$?

    # Check exit status
    if [ "$exit_status" -ne "$expected_exit" ]; then
        echo "[FAIL] $test_name: Expected exit $expected_exit, but got $exit_status"
        return 1
    fi

    # Check stdout (if expected_output is provided)
    if [ -n "$expected_output" ] && ! diff -q "$TEST_DIR/${test_name}.out" "$expected_output"; then
        echo "[FAIL] $test_name: Unexpected stdout"
        return 1
    fi

    # Check stderr (if expected_err is provided)
    if [ -n "$expected_err" ] && ! diff -q "$TEST_DIR/${test_name}.err" "$expected_err"; then
        echo "[FAIL] $test_name: Unexpected stderr"
        return 1
    fi

    echo "[PASS] $test_name"
    return 0
}

# Run tests

# 1. Help flag (-h)
run_test "help_flag" "$PBF_EXEC -h" 0 "" ""

# 2. No arguments (should fail)
run_test "no_arguments" "$PBF_EXEC" 1 "" ""

# 3. Invalid flag (should fail)
run_test "invalid_flag" "$PBF_EXEC -x" 1 "" ""

# 4. Multiple -f options (should fail)
run_test "multiple_f" "$PBF_EXEC -f file1.pbf -f file2.pbf" 1 "" ""

# 5. -s (summary) without a file (should fail)
run_test "summary_no_file" "$PBF_EXEC -s" 1 "" ""

# 6. -s with valid file (should pass if file exists)
run_test "summary_valid" "$PBF_EXEC -f tests/rsrc/sbu.pbf -s" 0 "" ""

# 7. -b (bounding box) with valid file
run_test "bounding_box_valid" "$PBF_EXEC -f tests/rsrc/sbu.pbf -b" 0 "" ""

# 8. -n with valid node ID (if exists in the file)
run_test "valid_node" "$PBF_EXEC -f tests/rsrc/sbu.pbf -n 213352011" 0 "" ""

# 9. -n with invalid node ID (should produce no output but exit success)
run_test "invalid_node" "$PBF_EXEC -f tests/rsrc/sbu.pbf -n 999999999999" 0 "" ""

# 10. -w with valid way ID (if exists)
run_test "valid_way" "$PBF_EXEC -f tests/rsrc/sbu.pbf -w 20175414" 0 "" ""

# 11. -w with invalid way ID (should produce no output but exit success)
run_test "invalid_way" "$PBF_EXEC -f tests/rsrc/sbu.pbf -w 999999999999" 0 "" ""

# 12. -w with key lookup
run_test "way_with_key" "$PBF_EXEC -f tests/rsrc/sbu.pbf -w 20175414 highway surface" 0 "" ""

# 13. Extra arguments after -s (should fail)
run_test "extra_args_after_s" "$PBF_EXEC -s extra_arg" 1 "" ""

# 14. Extra arguments after -b (should fail)
run_test "extra_args_after_b" "$PBF_EXEC -b extra_arg" 1 "" ""

# 15. Valid combined options (-s -b)
run_test "combined_s_b" "$PBF_EXEC -f tests/rsrc/sbu.pbf -s -b" 0 "" ""

# 16. Unrecognized option (should fail)
run_test "unrecognized_option" "$PBF_EXEC -q" 1 "" ""

# 17. Valid order of options (-b -s -n -w)
run_test "valid_option_order" "$PBF_EXEC -f tests/rsrc/sbu.pbf -b -s -n 213352011 -w 20175414" 0 "" ""

# Done
echo "All tests executed."
