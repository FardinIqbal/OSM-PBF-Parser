#include <criterion/criterion.h>
#include <criterion/logging.h>
#include "global.h"
#include "test_common.h"

#define PROGRAM_PATH "bin/pbf"

#define TEST_SUITE basecode_suite

/* "BLACKBOX" tests -- these run your program using 'system()' and check the results. */

/**
 * empty_args
 * @brief PROGRAM_PATH
 */

#define TEST_NAME empty_args
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "%s", ""); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_FAILURE, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
    assert_files_match(ref_errfile, test_errfile, NULL);
}
#undef TEST_NAME

/**
 * help_only
 * @brief PROGRAM_PATH -h
 */

#define TEST_NAME help_only
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "-h"); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_SUCCESS, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
    assert_files_match(ref_errfile, test_errfile, NULL);
}
#undef TEST_NAME

/**
 * help_with_junk
 * @brief PROGRAM_PATH -h -s -d -c -p test_outfile  (positive test)
 */

#define TEST_NAME help_with_junk
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "-h -s -d -c -p %s", test_outfile); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_SUCCESS, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
    assert_files_match(ref_errfile, test_errfile, NULL);
}
#undef TEST_NAME

/**
 * summary_empty_map
 * @brief PROGRAM_PATH -s < /dev/null
 */

#define TEST_NAME summary_empty_map
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "-s"); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_SUCCESS, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
    assert_files_match(ref_errfile, test_errfile, NULL);
}
#undef TEST_NAME

/**
 * summary_sbu_map
 * @brief PROGRAM_PATH -s < rsrc/sbu.pbf
 */

#define TEST_NAME summary_sbu_map
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "-s"); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_SUCCESS, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
    assert_files_match(ref_errfile, test_errfile, NULL);
}
#undef TEST_NAME

/**
 * bbox_empty_map
 * @brief PROGRAM_PATH -b < /dev/null
 */

#define TEST_NAME bbox_empty_map
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "-b"); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_SUCCESS, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
}
#undef TEST_NAME

/**
 * bbox_sbu_map
 * @brief PROGRAM_PATH -b < rsrc/sbu.pbf
 */

#define TEST_NAME bbox_sbu_map
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    setup_test(QUOTE(TEST_NAME));
    FILE *f; size_t s = 0; char *args = NULL; NEWSTREAM(f, s, args);
    fprintf(f, "-b"); fclose(f);
    int status = run_using_system(PROGRAM_PATH, "", "", args, STANDARD_LIMITS);
    assert_expected_status(EXIT_SUCCESS, status);
    assert_files_match(ref_outfile, test_outfile, NULL);
    assert_files_match(ref_errfile, test_errfile, NULL);
}
#undef TEST_NAME

/* Unit tests -- these call functions in your program directly. */

/**
 * Read SBU map
 */

#define TEST_NAME read_sbu_map
Test(TEST_SUITE, TEST_NAME, .timeout=TEST_TIMEOUT)
{
    char *filename = "tests/rsrc/sbu.pbf";
    FILE *in = fopen(filename, "r");
    cr_assert(in != NULL, "The file '%s' could not be opened\n", filename);
    OSM_Map *mp = OSM_read_Map(in);
    cr_assert(mp != NULL, "A non-NULL OSM_Map pointer was expected\n");
}
#undef TEST_NAME



