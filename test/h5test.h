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
 * Purpose:     Test support stuff.
 */
#ifndef H5TEST_H
#define H5TEST_H

/*
 * Include required headers.  This file tests internal library functions,
 * so we include the private headers here.
 */
#include "hdf5.h"
#include "H5private.h"
#include "H5Eprivate.h"

#ifdef H5_HAVE_MULTITHREAD
#include <stdatomic.h>
#endif

/*
 * Predefined test verbosity levels.
 *
 * Convention:
 *
 * The higher the verbosity value, the more information printed.
 * So, output for higher verbosity also include output of all lower
 * verbosity.
 *
 *  Value     Description
 *  0         None:   No informational message.
 *  1                 "All tests passed"
 *  2                 Header of overall test
 *  3         Default: header and results of individual test
 *  4
 *  5         Low:    Major category of tests.
 *  6
 *  7         Medium: Minor category of tests such as functions called.
 *  8
 *  9         High:   Highest level.  All information.
 */
#define VERBO_NONE 0 /* None    */
#define VERBO_DEF  3 /* Default */
#define VERBO_LO   5 /* Low     */
#define VERBO_MED  7 /* Medium  */
#define VERBO_HI   9 /* High    */

/*
 * Verbose queries
 * Only None needs an exact match.  The rest are at least as much.
 */

/* A macro version of HDGetTestVerbosity(). */
/* Should be used internally by the libtest.a only. */
#define HDGetTestVerbosity() (TestVerbosity)

#define VERBOSE_NONE (HDGetTestVerbosity() == VERBO_NONE)
#define VERBOSE_DEF  (HDGetTestVerbosity() >= VERBO_DEF)
#define VERBOSE_LO   (HDGetTestVerbosity() >= VERBO_LO)
#define VERBOSE_MED  (HDGetTestVerbosity() >= VERBO_MED)
#define VERBOSE_HI   (HDGetTestVerbosity() >= VERBO_HI)

/*
 * Test controls definitions.
 */
#define SKIPTEST  1 /* Skip this test */
#define ONLYTEST  2 /* Do only this test */
#define BEGINTEST 3 /* Skip all tests before this test */

/*
 * This contains the filename prefix specified as command line option for
 * the parallel test files.
 */
H5TEST_DLLVAR char *paraprefix;
#ifdef H5_HAVE_PARALLEL
H5TEST_DLLVAR MPI_Info h5_io_info_g; /* MPI INFO object for IO */
#endif

#define H5_API_TEST_FILENAME_MAX_LENGTH 1024

#define API_TEST_PASS 1
#define API_TEST_FAIL 0
#define API_TEST_ERROR -1

/* Information for an individual thread running the API tests */
typedef struct thread_info_t {
    int thread_idx; /* The test-assign index of the thread */
    char* test_thread_filename; /* The name of the test container file */
} thread_info_t;

#ifdef H5_HAVE_MULTITHREAD
extern pthread_key_t test_thread_info_key_g;

#define IS_MAIN_TEST_THREAD ((GetTestMaxNumThreads() <= 1) ||\
    ((pthread_getspecific(test_thread_info_key_g)) && (((thread_info_t*)pthread_getspecific(test_thread_info_key_g))->thread_idx == 0)))

#else
#define IS_MAIN_TEST_THREAD true
#endif /* H5_HAVE_MULTITHREAD */

/* Flag values for TestFrameworkFlags */
#define ALLOW_MULTITHREAD 0x00000001 /* Run the test in a multi-threaded environment */

/*
 * Print the current location on the standard output stream.
 */
#define AT()                                                                                                 \
    do {                                                                                                     \
        printf("   at %s:%d in %s()...\n", __FILE__, __LINE__, __func__);                                    \
    } while (0)

/*
 * Muli-thread-compatible testing macros for use in API tests
 */
#ifdef H5_HAVE_MULTITHREAD

/* Increment global atomic testing variable. Used for MT testing by
 * tests that don't define threadlocal test information */
#define INCR_TEST_STAT(field_name)  atomic_fetch_add(&field_name, 1);

#else
#define INCR_TEST_STAT(field_name) field_name++
#endif

/*
 * The name of the test is printed by saying TESTING("something") which will
 * result in the string `Testing something' being flushed to standard output.
 * If a test passes, fails, or is skipped then the PASSED(), H5_FAILED(), or
 * SKIPPED() macro should be called.  After H5_FAILED() or SKIPPED() the caller
 * should print additional information to stdout indented by at least four
 * spaces.  If the h5_errors() is used for automatic error handling then
 * the H5_FAILED() macro is invoked automatically when an API function fails.
 */
#define TESTING(WHAT)                                                                                        \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            printf("Testing %-62s", WHAT);                                                                   \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
        INCR_TEST_STAT(n_tests_run_g);                                                                       \
    } while (0)
#define TESTING_2(WHAT)                                                                                      \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            printf("  Testing %-60s", WHAT);                                                                 \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
        INCR_TEST_STAT(n_tests_run_g);                                                                       \
    } while (0)
#define PASSED()                                                                                             \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            HDputs(" PASSED");                                                                               \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
        INCR_TEST_STAT(n_tests_passed_g);                                                                    \
    } while (0)
#define H5_FAILED()                                                                                          \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            HDputs("*FAILED*");                                                                              \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
        INCR_TEST_STAT(n_tests_failed_g);                                                                    \
    } while (0)
#define H5_WARNING()                                                                                         \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            HDputs("*WARNING*");                                                                             \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
    } while (0)
#define SKIPPED()                                                                                            \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            HDputs(" -SKIP-");                                                                               \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
        INCR_TEST_STAT(n_tests_skipped_g);                                                                   \
    } while (0)
#define PUTS_ERROR(s)                                                                                        \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            HDputs(s);                                                                                       \
            AT();                                                                                            \
        }                                                                                                    \
        goto error;                                                                                          \
    } while (0)
#define TEST_ERROR                                                                                           \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            H5_FAILED();                                                                                     \
            AT();                                                                                            \
        }                                                                                                    \
        goto error;                                                                                          \
    } while (0)
#define STACK_ERROR                                                                                          \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD)                                                                             \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        goto error;                                                                                          \
    } while (0)
#define FAIL_STACK_ERROR                                                                                     \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            H5_FAILED();                                                                                     \
            AT();                                                                                            \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
        goto error;                                                                                          \
    } while (0)
#define FAIL_PUTS_ERROR(s)                                                                                   \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            H5_FAILED();                                                                                     \
            AT();                                                                                            \
            HDputs(s);                                                                                       \
        }                                                                                                    \
        goto error;                                                                                          \
    } while (0)
/*
 * Testing macros used for multi-part tests.
 */
#define TESTING_MULTIPART(WHAT)                                                                              \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            printf("Testing %-62s", WHAT);                                                                   \
            HDputs("");                                                                                      \
            fflush(stdout);                                                                                  \
        }                                                                                                    \
    } while (0)

/*
 * Begin and end an entire section of multi-part tests. By placing all the
 * parts of a test between these macros, skipping to the 'error' cleanup
 * section of a test is deferred until all parts have finished.
 */
#define BEGIN_MULTIPART                                                                                      \
    {                                                                                                        \
        int part_nerrors = 0;

#define END_MULTIPART                                                                                        \
    if (part_nerrors > 0)                                                                                    \
        goto error;                                                                                          \
    }

/*
 * Begin, end and handle errors within a single part of a multi-part test.
 * The PART_END macro creates a goto label based on the given "part name".
 * When a failure occurs in the current part, the PART_ERROR macro uses
 * this label to skip to the next part of the multi-part test. The PART_ERROR
 * macro also increments the error count so that the END_MULTIPART macro
 * knows to skip to the test's 'error' label once all test parts have finished.
 */
#define PART_BEGIN(part_name) {
#define PART_END(part_name)                                                                                  \
    }                                                                                                        \
    part_##part_name##_end:
#define PART_ERROR(part_name)                                                                                \
    do {                                                                                                     \
        INCR_TEST_STAT(n_tests_failed_g);                                                                    \
        part_nerrors++;                                                                                      \
        goto part_##part_name##_end;                                                                         \
    } while (0)
#define PART_TEST_ERROR(part_name)                                                                           \
    do {                                                                                                     \
        if (IS_MAIN_TEST_THREAD) {                                                                           \
            H5_FAILED();                                                                                     \
            AT();                                                                                            \
        }                                                                                                    \
        part_nerrors++;                                                                                      \
        goto part_##part_name##_end;                                                                         \
    } while (0)
/*
 * Simply skips to the goto label for this test part and moves on to the
 * next test part. Useful for when a test part needs to be skipped for
 * some reason or is currently unimplemented and empty.
 */
#define PART_EMPTY(part_name)                                                                                \
    do {                                                                                                     \
        goto part_##part_name##_end;                                                                         \
    } while (0)


/* Number of seconds to wait before killing a test (requires alarm(2)) */
#define H5_ALARM_SEC 1200 /* default is 20 minutes */

/* Flags for h5_fileaccess_flags() */
#define H5_FILEACCESS_LIBVER 0x01

/* Flags for h5_driver_uses_multiple_files() */
#define H5_EXCLUDE_MULTIPART_DRIVERS     0x01
#define H5_EXCLUDE_NON_MULTIPART_DRIVERS 0x02

/* Fill an array on the heap with an increasing count value.  BUF
 * is expected to point to a `struct { TYPE arr[...][...]; }`.
 */
#define H5TEST_FILL_2D_HEAP_ARRAY(BUF, TYPE)                                                                 \
    do {                                                                                                     \
        /* Prefix with h5tfa to avoid shadow warnings */                                                     \
        size_t h5tfa_i     = 0;                                                                              \
        size_t h5tfa_j     = 0;                                                                              \
        TYPE   h5tfa_count = 0;                                                                              \
                                                                                                             \
        for (h5tfa_i = 0; h5tfa_i < NELMTS((BUF)->arr); h5tfa_i++)                                           \
            for (h5tfa_j = 0; h5tfa_j < NELMTS((BUF)->arr[0]); h5tfa_j++) {                                  \
                (BUF)->arr[h5tfa_i][h5tfa_j] = h5tfa_count;                                                  \
                h5tfa_count++;                                                                               \
            }                                                                                                \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ugly hack to cast away const for freeing const-qualified pointers.
 * Should only be used sparingly, where the alternative (like keeping
 * an equivalent non-const pointer around) is far messier.
 */
#ifndef h5_free_const
#define h5_free_const(mem) free((void *)(uintptr_t)mem)
#endif

/* Generally useful testing routines */
H5TEST_DLL void        h5_clean_files(const char *base_name[], hid_t fapl);
H5TEST_DLL int         h5_cleanup(const char *base_name[], hid_t fapl);
H5TEST_DLL char       *h5_fixname(const char *base_name, hid_t fapl, char *fullname, size_t size);
H5TEST_DLL char       *h5_fixname_superblock(const char *base_name, hid_t fapl, char *fullname, size_t size);
H5TEST_DLL char       *h5_fixname_no_suffix(const char *base_name, hid_t fapl, char *fullname, size_t size);
H5TEST_DLL char       *h5_fixname_printf(const char *base_name, hid_t fapl, char *fullname, size_t size);
H5TEST_DLL hid_t       h5_fileaccess(void);
H5TEST_DLL hid_t       h5_fileaccess_flags(unsigned flags);
H5TEST_DLL void        h5_no_hwconv(void);
H5TEST_DLL const char *h5_rmprefix(const char *filename);
H5TEST_DLL void        h5_reset(void);
H5TEST_DLL void        h5_restore_err(void);
H5TEST_DLL void        h5_show_hostname(void);
H5TEST_DLL h5_stat_size_t h5_get_file_size(const char *filename, hid_t fapl);
H5TEST_DLL int            print_func(const char *format, ...) H5_ATTR_FORMAT(printf, 1, 2);
H5TEST_DLL int            h5_make_local_copy(const char *origfilename, const char *local_copy_name);
H5TEST_DLL herr_t         h5_verify_cached_stabs(const char *base_name[], hid_t fapl);
H5TEST_DLL H5FD_class_t  *h5_get_dummy_vfd_class(void);
H5TEST_DLL H5VL_class_t  *h5_get_dummy_vol_class(void);
H5TEST_DLL const char    *h5_get_version_string(H5F_libver_t libver);
H5TEST_DLL int            h5_compare_file_bytes(char *fname1, char *fname2);
H5TEST_DLL int            h5_duplicate_file_by_bytes(const char *orig, const char *dest);
H5TEST_DLL herr_t         h5_check_if_file_locking_enabled(bool *are_enabled);
H5TEST_DLL herr_t         h5_using_native_vol(hid_t fapl_id, hid_t obj_id, bool *is_native_vol);
H5TEST_DLL bool           h5_using_default_driver(const char *drv_name);
H5TEST_DLL herr_t         h5_using_parallel_driver(hid_t fapl_id, bool *driver_is_parallel);
H5TEST_DLL herr_t         h5_driver_is_default_vfd_compatible(hid_t fapl_id, bool *default_vfd_compatible);
H5TEST_DLL bool           h5_driver_uses_multiple_files(const char *drv_name, unsigned flags);

/* Functions that will replace components of a FAPL */
H5TEST_DLL herr_t h5_get_libver_fapl(hid_t fapl_id);

/* h5_clean_files() replacements */
H5TEST_DLL void h5_delete_test_file(const char *base_name, hid_t fapl);
H5TEST_DLL void h5_delete_all_test_files(const char *base_name[], hid_t fapl);

/* h5_reset() replacement */
H5TEST_DLL void h5_test_init(void);

/* h5_cleanup() replacement */
H5TEST_DLL void h5_test_shutdown(void);

/* Routines for operating on the list of tests (for the "all in one" tests) */
H5TEST_DLL void        TestUsage(void);
H5TEST_DLL void        AddTest(const char *TheName, void (*TheCall)(void), void (*Cleanup)(void),
                               const char *TheDescr, const void *TestParameters, const int64_t TestFrameworkFlags);
H5TEST_DLL void        TestInfo(const char *ProgName);
H5TEST_DLL void        TestParseCmdLine(int argc, char *argv[]);
H5TEST_DLL void        PerformTests(void);
H5TEST_DLL void        TestSummary(void);
H5TEST_DLL void        TestCleanup(void);
H5TEST_DLL void        TestShutdown(void);
H5TEST_DLL void        TestInit(const char *ProgName, void (*private_usage)(void),
                                int (*private_parser)(int ac, char *av[]));
H5TEST_DLL int         GetTestVerbosity(void);
H5TEST_DLL int         SetTestVerbosity(int newval);
H5TEST_DLL int         GetTestSummary(void);
H5TEST_DLL int         GetTestCleanup(void);
H5TEST_DLL int         SetTestNoCleanup(void);
H5TEST_DLL int         GetTestExpress(void);
H5TEST_DLL int         SetTestExpress(int newval);
H5TEST_DLL void        ParseTestVerbosity(char *argv);
H5TEST_DLL int         GetTestNumErrs(void);
H5TEST_DLL void        IncTestNumErrs(void);
H5TEST_DLL const void *GetTestParameters(void);
H5TEST_DLL int         TestErrPrintf(const char *format, ...) H5_ATTR_FORMAT(printf, 1, 2);
H5TEST_DLL void        SetTest(const char *testname, int action);
H5TEST_DLL void        TestAlarmOn(void);
H5TEST_DLL void        TestAlarmOff(void);

#ifdef H5_HAVE_MULTITHREAD
H5TEST_DLL void*       ThreadTestWrapper(void *test);
H5TEST_DLL int         H5_mt_test_global_setup(void);
H5TEST_DLL int         H5_mt_test_thread_setup(int thread_idx);
H5TEST_DLL void        H5_test_thread_info_key_destructor(void *value);
#endif

/* Allow up to 3-digit thread indexes (0-999)*/
#define MAX_THREAD_IDX 999
#define MAX_THREAD_IDX_LEN

/* Generate a heap-allocated filename of the form <prefix><thread_idx><filename> */
char *generate_threadlocal_filename(const char *prefix, int thread_idx, const char *filename);

/*
 * Environment variable specifying a prefix string to add to
 * filenames generated by the API tests
 */
#define HDF5_API_TEST_PATH_PREFIX "HDF5_API_TEST_PATH_PREFIX"
#define TEST_FILE_NAME "H5_api_test.h5"
/* Prefix to use for filepaths in API tests */
extern const char *test_path_prefix;

H5TEST_DLL int  GetTestMaxNumThreads(void);
H5TEST_DLL void SetTestMaxNumThreads(int num_threads);

#ifdef H5_HAVE_FILTER_SZIP
H5TEST_DLL int h5_szip_can_encode(void);
#endif /* H5_HAVE_FILTER_SZIP */

#ifdef H5_HAVE_PARALLEL
H5TEST_DLL int   h5_set_info_object(void);
H5TEST_DLL void  h5_dump_info_object(MPI_Info info);
H5TEST_DLL char *getenv_all(MPI_Comm comm, int root, const char *name);
#endif

/* Extern global variables */
H5TEST_DLLVAR int      TestVerbosity;
/* Global variables for testing */
H5TEST_DLLVAR  H5_ATOMIC(size_t) n_tests_run_g;
H5TEST_DLLVAR  H5_ATOMIC(size_t) n_tests_passed_g;
H5TEST_DLLVAR  H5_ATOMIC(size_t) n_tests_failed_g;
H5TEST_DLLVAR  H5_ATOMIC(size_t) n_tests_skipped_g;

H5TEST_DLLVAR uint64_t vol_cap_flags_g;

H5TEST_DLL void   h5_send_message(const char *file, const char *arg1, const char *arg2);
H5TEST_DLL herr_t h5_wait_message(const char *file);

#ifdef __cplusplus
}
#endif
#endif
