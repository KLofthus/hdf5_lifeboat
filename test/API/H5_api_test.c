/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * A test suite which only makes public HDF5 API calls and which is meant
 * to test the native VOL connector or a specified HDF5 VOL connector (or
 * set of connectors stacked with each other). This test suite must assume
 * that a VOL connector could only implement the File interface. Therefore,
 * the suite should check that a particular piece of functionality is supported
 * by the VOL connector before actually testing it. If the functionality is
 * not supported, the test should simply be skipped, perhaps with a note as
 * to why the test was skipped, if possible.
 *
 * If the VOL connector being used supports the creation of groups, this
 * test suite will attempt to organize the output of these various tests
 * into groups based on their respective HDF5 interface.
 */

#include "H5_api_test.h"

#include "H5_api_attribute_test.h"
#include "H5_api_dataset_test.h"
#include "H5_api_datatype_test.h"
#include "H5_api_file_test.h"
#include "H5_api_group_test.h"
#include "H5_api_link_test.h"
#include "H5_api_misc_test.h"
#include "H5_api_object_test.h"
#include "H5_api_test_util.h"
#ifdef H5_API_TEST_HAVE_ASYNC
#include "H5_api_async_test.h"
#endif

#ifdef H5_HAVE_MULTITHREAD
#include <pthread.h>
#endif

char H5_api_test_filename_g[H5_API_TEST_FILENAME_MAX_LENGTH];

static int H5_api_check_vol_registration(void);
static int H5_api_test_display_information(unsigned seed);
static int H5_api_test_get_vol_cap_flags(uint64_t *vol_cap_flags);
static int H5_api_test_create_containers(const char *filename, uint64_t vol_cap_flags);
static int H5_api_test_create_single_container(const char *filename, uint64_t vol_cap_flags);
static int H5_api_test_destroy_container_files(void);

static void H5_api_test_display_results(void);

/* Margin of runtime for each subtest allocated to cleanup */
#define API_TEST_MARGIN 1

/* X-macro to define the following for each test:
 * - enum type
 * - name
 * - test function
 * - enabled by default
 */
#ifdef H5_API_TEST_HAVE_ASYNC
#define H5_API_TESTS                                                                                         \
    X(H5_API_TEST_NULL, "", NULL, 0)                                                                         \
    X(H5_API_TEST_FILE, "file", H5_api_file_test_add, 1)                                                     \
    X(H5_API_TEST_GROUP, "group", H5_api_group_test_add, 1)                                                  \
    X(H5_API_TEST_DATASET, "dataset", H5_api_dataset_test_add, 1)                                            \
    X(H5_API_TEST_DATATYPE, "datatype", H5_api_datatype_test_add, 1)                                         \
    X(H5_API_TEST_ATTRIBUTE, "attribute", H5_api_attribute_test_add, 1)                                      \
    X(H5_API_TEST_LINK, "link", H5_api_link_test_add, 1)                                                     \
    X(H5_API_TEST_OBJECT, "object", H5_api_object_test_add, 1)                                               \
    X(H5_API_TEST_MISC, "misc", H5_api_misc_test_add, 1)                                                     \
    X(H5_API_TEST_ASYNC, "async", H5_api_async_test_add, 1)                                                  \
    X(H5_API_TEST_MAX, "", NULL, 0)
#else
#define H5_API_TESTS                                                                                         \
    X(H5_API_TEST_NULL, "", NULL, 0)                                                                         \
    X(H5_API_TEST_FILE, "file", H5_api_file_test_add, 1)                                                     \
    X(H5_API_TEST_GROUP, "group", H5_api_group_test_add, 1)                                                  \
    X(H5_API_TEST_DATASET, "dataset", H5_api_dataset_test_add, 1)                                            \
    X(H5_API_TEST_DATATYPE, "datatype", H5_api_datatype_test_add, 1)                                         \
    X(H5_API_TEST_ATTRIBUTE, "attribute", H5_api_attribute_test_add, 1)                                      \
    X(H5_API_TEST_LINK, "link", H5_api_link_test_add, 1)                                                     \
    X(H5_API_TEST_OBJECT, "object", H5_api_object_test_add, 1)                                               \
    X(H5_API_TEST_MISC, "misc", H5_api_misc_test_add, 1)                                                     \
    X(H5_API_TEST_MAX, "", NULL, 0)
#endif

#define X(a, b, c, d) a,
enum H5_api_test_type { H5_API_TESTS };
#undef X
#define X(a, b, c, d) b,
static const char *const H5_api_test_name[] = {H5_API_TESTS};
#undef X
#define X(a, b, c, d) c,
static void (*H5_api_test_add_func[])(void) = {H5_API_TESTS};
#undef X
#define X(a, b, c, d) d,
static int H5_api_test_enabled[] = {H5_API_TESTS};
#undef X

static enum H5_api_test_type
H5_api_test_name_to_type(const char *test_name)
{
    enum H5_api_test_type i = 0;

    while (strcmp(H5_api_test_name[i], test_name) && i != H5_API_TEST_MAX)
        i++;

    return ((i == H5_API_TEST_MAX) ? H5_API_TEST_NULL : i);
}

static void
H5_api_test_add(void)
{
    enum H5_api_test_type i;

    for (i = H5_API_TEST_FILE; i < H5_API_TEST_MAX; i++)
        if (H5_api_test_enabled[i])
            H5_api_test_add_func[i]();
}

static int
parse_command_line(int argc, char **argv)
{
    /* Simple argument checking, TODO can improve that later */
    if (argc > 1) {
        enum H5_api_test_type i = H5_api_test_name_to_type(argv[argc - 1]);
        if (i != H5_API_TEST_NULL) {
            /* Run only specific API test */
            memset(H5_api_test_enabled, 0, sizeof(H5_api_test_enabled));
            H5_api_test_enabled[i] = 1;
        }
    }

    return 0;
}

static void
usage(void)
{
    print_func("file        run only the file interface tests\n");
    print_func("group       run only the group interface tests\n");
    print_func("dataset     run only the dataset interface tests\n");
    print_func("attribute   run only the attribute interface tests\n");
    print_func("datatype    run only the datatype interface tests\n");
    print_func("link        run only the link interface tests\n");
    print_func("object      run only the object interface tests\n");
    print_func("misc        run only the miscellaneous tests\n");
    print_func("async       run only the async interface tests\n");
}

int
main(int argc, char **argv)
{
    H5E_auto2_t default_err_func;
    void       *default_err_data          = NULL;
    bool        err_occurred              = false;
    int chars_written = 0;
    unsigned seed = 0;
    int testExpress = 0;

    H5open();

    /* Store current error stack printing function since TestInit unsets it */
    H5Eget_auto2(H5E_DEFAULT, &default_err_func, &default_err_data);

    /* Initialize testing framework */
    TestInit(argv[0], usage, NULL);

    /* Reset error stack printing function */
    H5Eset_auto2(H5E_DEFAULT, default_err_func, default_err_data);

    /* Hide all output from testing framework and replace with our own */
    SetTestVerbosity(VERBO_NONE);

    /* Parse command line separately from the test framework since
     * tests need to be added before TestParseCmdLine in order for
     * the -help option to show them, but we need to know ahead of
     * time which tests to add if only a specific interface's tests
     * are going to be run.
     */
    parse_command_line(argc, argv);

    /* Add tests */
    H5_api_test_add();

    /* Display testing information */
    TestInfo(argv[0]);

    /* Parse command line arguments */
    TestParseCmdLine(argc, argv);

    n_tests_run_g     = 0;
    n_tests_passed_g  = 0;
    n_tests_failed_g  = 0;
    n_tests_skipped_g = 0;

    testExpress = GetTestExpress();

    // TODO
    UNUSED(testExpress);

    /* TODO: Refactor TestAlarmOn to accept specific timeout */
    TestAlarmOn();

    /*
     * If using a VOL connector other than the native
     * connector, check whether the VOL connector was
     * successfully registered before running the tests.
     * Otherwise, HDF5 will default to running the tests
     * with the native connector, which could be misleading.
     */
    if (H5_api_check_vol_registration() < 0) {
        fprintf(stderr, "Active VOL connector not properly registered\n");
        return EXIT_FAILURE;
    }

#ifndef H5_HAVE_MULTITHREAD
    if (GetTestMaxNumThreads() > 1) {
        fprintf(stderr, "HDF5 must be built with multi-thread support to run multi-threaded API tests\n");
        exit(EXIT_FAILURE);
    }
#endif

    if (GetTestMaxNumThreads() <= 0) {
        SetTestMaxNumThreads(API_TESTS_DEFAULT_NUM_THREADS);
    }

    seed = (unsigned)HDtime(NULL);
    srand(seed);

    /* Display VOL information */
    if (H5_api_test_display_information(seed) < 0) {
        fprintf(stderr, "Error displaying VOL information\n");
        return EXIT_FAILURE;
    }

    if (H5_api_test_get_vol_cap_flags(&vol_cap_flags_g) < 0) {
        fprintf(stderr, "Error setting up VOL flags\n");
        return EXIT_FAILURE;
    }

    /* Set up test path prefix for filenames, with default being empty */
    if (test_path_prefix_g == NULL) {
        if ((test_path_prefix_g = HDgetenv(HDF5_API_TEST_PATH_PREFIX)) == NULL)
            test_path_prefix_g = (const char *)"";
    }

    if (GetTestMaxNumThreads() == 1) {
        /* Populate global test filename */
        if ((chars_written = HDsnprintf(H5_api_test_filename_g, H5_API_TEST_FILENAME_MAX_LENGTH, "%s%s",test_path_prefix_g,
                TEST_FILE_NAME)) < 0) {
            fprintf(stderr, "Error while creating test file name\n");
            return EXIT_FAILURE;
        }

        if ((size_t)chars_written >= H5_API_TEST_FILENAME_MAX_LENGTH) {
            fprintf(stderr, "Test file name exceeded expected size\n");
            return EXIT_FAILURE;
        }
    }


    /* Create the file(s) that will be used for all of the tests,
     * except for those which test file creation.*/
    if (H5_api_test_create_containers(TEST_FILE_NAME, vol_cap_flags_g) < 0) {
        fprintf(stderr, "Unable to create testing container file with basename '%s'\n", TEST_FILE_NAME);
        return EXIT_FAILURE;
    }

    /* Perform requested testing */
    PerformTests();

    /* Display test summary, if requested */
    if (GetTestSummary())
        TestSummary();
    
    /* Clean up test files, if allowed */
    if (GetTestCleanup() && !HDgetenv(HDF5_NOCLEANUP))
        TestCleanup();

    printf("Deleting container file for tests\n\n");

    if (H5_api_test_destroy_container_files() < 0) {
        fprintf(stderr, "Error cleaning up global API test info\n");
        err_occurred = true;
        goto done;
    }

    if (n_tests_run_g > 0)
        H5_api_test_display_results();

done:
    TestAlarmOff();

    if (GetTestNumErrs() > 0)
        n_tests_failed_g += (size_t) GetTestNumErrs();

    /* Release test infrastructure */
    TestShutdown();

    H5close();

    if (err_occurred || n_tests_failed_g > 0) {
        exit(EXIT_FAILURE);
    } else {
        exit(EXIT_SUCCESS);
    }
}

/* Returns a negative value if the VOL connector specified by environment variables 
 * cannot be registered or does not match the VOL connector on the 
 * default FAPL, 0 otherwise */
static int
H5_api_check_vol_registration(void) {
    hid_t default_con_id = H5I_INVALID_HID;
    hid_t registered_con_id = H5I_INVALID_HID;
    hid_t fapl_id = H5I_INVALID_HID;

    const char *vol_connector_name;
    char *vol_connector_string = NULL;
    char *vol_connector_string_copy = NULL;

    /* Get active VOL connector name */
    if (NULL == (vol_connector_string = getenv(HDF5_VOL_CONNECTOR))) {
        printf("No VOL connector selected; using native VOL connector\n");
        vol_connector_name = "native";
    }
    else {

        if (NULL == (vol_connector_string_copy = strdup(vol_connector_string))) {
            fprintf(stderr, "Unable to copy VOL connector string\n");
            goto error;
        }

        if (NULL == (vol_connector_name = (const char*) strtok(vol_connector_string_copy, " "))) {
            fprintf(stderr, "Error while parsing VOL connector string\n");
            goto error;
        }

    }

    /* If VOL is not native, make sure it is registered properly */
    if (0 != strcmp(vol_connector_name, "native")) {
        htri_t is_registered;

        if ((is_registered = H5VLis_connector_registered_by_name(vol_connector_name)) < 0) {
            fprintf(stderr, "Unable to determine if VOL connector is registered\n");
            goto error;
        }

        if (!is_registered) {
            fprintf(stderr, "Specified VOL connector '%s' wasn't correctly registered!\n",
                    vol_connector_name);
            goto error;
        }
        else {
            /*
             * If the connector was successfully registered, check that
             * the connector ID set on the default FAPL matches the ID
             * for the registered connector before running the tests.
             */
            if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
                fprintf(stderr, "Couldn't create FAPL\n");
                goto error;
            }

            if (H5Pget_vol_id(fapl_id, &default_con_id) < 0) {
                fprintf(stderr, "Couldn't retrieve ID of VOL connector set on default FAPL\n");
                goto error;
            }

            if ((registered_con_id = H5VLget_connector_id_by_name(vol_connector_name)) < 0) {
                fprintf(stderr, "Couldn't retrieve ID of registered VOL connector\n");
                goto error;
            }

            if (default_con_id != registered_con_id) {
                fprintf(stderr, "VOL connector set on default FAPL didn't match specified VOL connector\n");
                goto error;
            }
        }
    }

    if (registered_con_id > 0 && H5VLclose(registered_con_id) < 0) {
        fprintf(stderr, "Unable to close registered VOL connector\n");
        goto error;
    }

    if (default_con_id > 0 && H5VLclose(default_con_id) < 0) {
        fprintf(stderr, "Unable to close default VOL connector\n");
        goto error;
    }

    if (fapl_id > 0 && H5Pclose(fapl_id) < 0) {
        fprintf(stderr, "Unable to close FAPL\n");
        goto error;
    }

    free(vol_connector_string_copy);

    return 0;

error:
    H5E_BEGIN_TRY {
        H5VLclose(registered_con_id);
        H5VLclose(default_con_id);
        H5Pclose(fapl_id);
        free(vol_connector_string_copy);
    } H5E_END_TRY;

    return -1;
}

/* Display API test configuration info. 
 * Return: negative on failure, 0 on success */
static int
H5_api_test_display_information(unsigned seed) {
    const char *vol_connector_name = NULL;
    char *vol_connector_name_copy = NULL;
    char *vol_connector_info = NULL;


    if (NULL == (vol_connector_name = HDgetenv(HDF5_VOL_CONNECTOR))) {
        vol_connector_name = "native";
        vol_connector_info = NULL;
    }
    else {
        char *token;

        vol_connector_name_copy = HDstrdup(vol_connector_name);

        if (NULL == (token = HDstrtok(vol_connector_name_copy, " "))) {
            printf("    cannot parse VOL connector name string\n");
            goto error;
        }

        vol_connector_name = token;

        if (NULL != (token = HDstrtok(NULL, " "))) {
            vol_connector_info = token;
        }
    }

    printf("Running API tests with VOL connector '%s' and info string '%s'\n\n", vol_connector_name,
           vol_connector_info ? vol_connector_info : "");
    printf("Test parameters:\n");
    printf("  - Test file name: '%s'\n", TEST_FILE_NAME);
    printf("  - Test seed: %u\n", seed);
    printf("  - Test path prefix: '%s'\n", test_path_prefix_g);
    printf("\n\n");

    free(vol_connector_name_copy);
    return 0;
error:
    free(vol_connector_name_copy);
    return -1;
}

/* Display the total success, failure, and skip counts from API test run */
static void
H5_api_test_display_results(void) {
    const char *vol_connector_name = NULL;

    if (NULL == (vol_connector_name = HDgetenv(HDF5_VOL_CONNECTOR)))
        vol_connector_name = "native";
    
    printf("%zu/%zu (%.2f%%) API tests passed with VOL connector '%s'\n", n_tests_passed_g, n_tests_run_g,
            ((double)n_tests_passed_g / (double)n_tests_run_g * 100.0), vol_connector_name);
    printf("%zu/%zu (%.2f%%) API tests did not pass with VOL connector '%s'\n", n_tests_failed_g,
            n_tests_run_g, ((double)n_tests_failed_g / (double)n_tests_run_g * 100.0), vol_connector_name);
    printf("%zu/%zu (%.2f%%) API tests were skipped with VOL connector '%s'\n", n_tests_skipped_g,
            n_tests_run_g, ((double)n_tests_skipped_g / (double)n_tests_run_g * 100.0),
            vol_connector_name);
}

/* Retrieve the default VOL connector's capabilty flags.
 * Returns negative on failure, 0 on success */
static int
H5_api_test_get_vol_cap_flags(uint64_t *vol_cap_flags) {
    hid_t fapl_id = H5I_INVALID_HID;

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
        printf("    couldn't create FAPL\n");
        goto error;
    }

    *vol_cap_flags = H5VL_CAP_FLAG_NONE;
    if (H5Pget_vol_cap_flags(fapl_id, vol_cap_flags) < 0) {
        printf(" unable to retrieve VOL connector capability flags\n");
        goto error;
    }

    if (H5Pclose(fapl_id) < 0) {
        printf("    unable to close FAPL\n");
        goto error;
    }

    return 0;

error:
    H5Pclose(fapl_id);
    return -1;
}

/* Create the API container test file(s), one per thread.
 * Returns negative on failure, 0 on success */
static int
H5_api_test_create_containers(const char *filename, uint64_t vol_cap_flags)
{
    int max_threads = GetTestMaxNumThreads();
    char *tl_filename = NULL;

    if (!(vol_cap_flags & H5VL_CAP_FLAG_FILE_BASIC)) {
        printf("   VOL connector doesn't support file creation\n");
        goto error;
    }

    if (max_threads > 1) {
#ifdef H5_HAVE_MULTITHREAD
        for (int i = 0; i < max_threads; i++) {
            if ((tl_filename = generate_threadlocal_filename(test_path_prefix_g, i, filename)) == NULL) {
                printf("    failed to generate thread-local API test filename\n");
                goto error;
            }

            if (H5_api_test_create_single_container((const char *)tl_filename, vol_cap_flags) < 0) {
                printf("    failed to create thread-local API test container");
                goto error;
            }

        }

        free(tl_filename);
#else
        printf("    thread-specific filename requested, but multithread support not enabled\n");
        goto error;
#endif

    } else {
        if (H5_api_test_create_single_container((const char *)filename, vol_cap_flags) < 0) {
            printf("    failed to create test container\n");
            goto error;
        }
    }

    return 0;

error:
    free(tl_filename);
    return -1;
}

/* Helper for H5_api_test_create_containers().
 * Returns negative on failure, 0 on success */
static int
H5_api_test_create_single_container(const char *filename, uint64_t vol_cap_flags) {
    hid_t file_id  = H5I_INVALID_HID;
    hid_t group_id = H5I_INVALID_HID;

    if ((file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create testing container file '%s'\n", filename);
        goto error;
    }

    printf("    created container file\n");

    if (vol_cap_flags & H5VL_CAP_FLAG_GROUP_BASIC) {
        /* Create container groups for each of the test interfaces
         * (group, attribute, dataset, etc.).
         */
        if ((group_id = H5Gcreate2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >=
            0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }

        if ((group_id =
                 H5Gcreate2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }

        if ((group_id =
                 H5Gcreate2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >=
            0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >=
            0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }
    }

    if (H5Fclose(file_id) < 0) {
        printf("    failed to close testing container %s\n", filename);
        goto error;
    }

    return 0;
error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return -1;

}

/* Delete the API test container file(s).
 * Returns negative on failure, 0 on success */
static int
H5_api_test_destroy_container_files(void) {

    int max_threads = GetTestMaxNumThreads();
    char *filename = NULL;

    if (!(vol_cap_flags_g & H5VL_CAP_FLAG_FILE_BASIC)) {
        printf("   container should not have been created\n");
        goto error;
    }

    if (max_threads > 1) {
#ifndef H5_HAVE_MULTITHREAD
        printf("    thread-specific cleanup requested, but multithread support not enabled\n");
        goto error;
#endif
        
        for (int i = 0; i < max_threads; i++) {
            if ((filename = generate_threadlocal_filename(test_path_prefix_g, i, TEST_FILE_NAME)) == NULL) {
                printf("    failed to generate thread-local API test filename\n");
                goto error;
            }

            H5E_BEGIN_TRY {
                if (H5Fis_accessible(filename, H5P_DEFAULT) > 0) {
                    if (H5Fdelete(filename, H5P_DEFAULT) < 0) {
                        printf("    failed to destroy thread-local API test container");
                        goto error;
                    }
                }
            }
            H5E_END_TRY

            free(filename);
            filename = NULL;
        }
    } else {
        H5E_BEGIN_TRY {
            
            if (prefix_filename(test_path_prefix_g, TEST_FILE_NAME, &filename) < 0) {
                printf("    failed to prefix filename\n");
                goto error;
            }

            if (H5Fis_accessible(filename, H5P_DEFAULT) > 0) {
                if (H5Fdelete(filename, H5P_DEFAULT) < 0) {
                    printf("    failed to destroy thread-local API test container");
                    goto error;
                }
            }
        }
        H5E_END_TRY
    }

    free(filename);
    filename = NULL;
    return 0;

error:
    free(filename);
    filename = NULL;
    return -1;
}