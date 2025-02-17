//#include "h5test.h"
//#include "H5private.h"

#include "../src/H5Pint_mt.c"
#include "../src/H5Ppkg_mt.h"

#include <stdio.h>
#include <stdatomic.h>

#define MAX_PROP_NAME_LEN 50

void simple_test_1(void);
void serial_class_test(void);
void serial_list_test(void);




/****************************************************************************************
 * Function:    simple_test_1
 *
 * Purpose:     Performs a simple serial test of each of the H5Pint_mt.c functions.
 *
 * Return:      void   
 *
 ****************************************************************************************
 */
void simple_test_1(void)
{
    H5P_mt_prop_t           * first_prop;
    H5P_mt_prop_t           * second_prop;
    H5P_mt_prop_t           * target_prop;
    H5P_mt_list_table_entry_t * tbl_entry;
    H5P_mt_list_prop_ref_t      tbl_prop_ref;
    H5P_mt_prop_aptr_t        next_ptr;
    char                    * new_prop_name;
    void                    * value_ptr;
    size_t                    value_size;
    uint64_t                  prop1_create_ver;
    uint64_t                  prop2_create_ver;
    uint64_t                  curr_version;
    uint64_t                  next_version;
    H5P_mt_class_sptr_t       class_fl_next;
    H5P_mt_class_t          * root_class;
    H5P_mt_class_t          * new_class;
    const char              * new_class_name;
    H5P_mt_class_ref_counts_t ref_count;
    uint64_t                  log_pl_len;
    uint64_t                  phys_pl_len;
    H5P_mt_list_t           * new_list;
    H5P_mt_list_table_entry_t * lkup_tbl;
    uint32_t                    nprops;
    uint32_t                    nprops_inherited;
    uint32_t                    nprops_added;
    int32_t                     cmp_result;
    int32_t                     check_value;


    printf("\nMT PROP serial simple test #1\n");


    H5P_init();

    class_fl_next = atomic_load(&(H5P_mt_g.class_fl_head));

    root_class = class_fl_next.ptr;
    assert(root_class);
    assert(root_class->tag == H5P_MT_CLASS_TAG);
    
    curr_version = atomic_load(&(root_class->curr_version));
    assert( 1 == curr_version);

    next_version = atomic_load(&(root_class->next_version)); 
    assert( 2 == next_version);

    ref_count = atomic_load(&(root_class->ref_count));
    assert( 0 == ref_count.plc);

    first_prop = atomic_load(&(root_class->pl_head));
    
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);
    
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);
    assert(second_prop->sentinel);


    /* Testing H5P__mt_create_class() */

    new_class_name = "attribute access";

    new_class = H5P__mt_create_class(root_class, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS, 0);

    assert(new_class);
    assert(new_class->tag == H5P_MT_CLASS_TAG);

    ref_count = atomic_load(&(root_class->ref_count));
    assert( 1 == ref_count.plc);

    curr_version = atomic_load(&(new_class->curr_version));
    assert( 1 == curr_version);

    next_version = atomic_load(&(new_class->next_version)); 
    assert( 2 == next_version);

    assert(new_class);
    assert(new_class->tag == H5P_MT_CLASS_TAG);
    
    first_prop = atomic_load(&(new_class->pl_head));
    
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);
    
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);
    assert(second_prop->sentinel);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 0 == log_pl_len);
    assert( 2 == phys_pl_len);


    /* Testing inserting a property into a class */

    new_prop_name = malloc(MAX_PROP_NAME_LEN);

    snprintf(new_prop_name, MAX_PROP_NAME_LEN, "prop_1");

    value_size = snprintf(NULL, 0, "This is the value for prop_1.") + 1;
    value_ptr  = malloc(value_size);

    snprintf(value_ptr, value_size, "This is the value for prop_1.");

    H5P__mt_insert_prop_setup(new_class, new_prop_name, value_ptr, value_size);

    curr_version = atomic_load(&(new_class->curr_version));
    assert( 2 == curr_version);

    next_version = atomic_load(&(new_class->next_version)); 
    assert( 3 == next_version);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 1 == log_pl_len);
    assert( 3 == phys_pl_len);

    first_prop = new_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    target_prop = next_ptr.ptr;

    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);
    assert( ! target_prop->sentinel);


    /* Testing 'modifying' a property in a class */

    value_size = snprintf(NULL, 0, "This is the value for prop_1 modified.") + 1;
    value_ptr  = malloc(value_size);

    snprintf(value_ptr, value_size, "This is the value for prop_1 modified.");

    H5P__mt_insert_prop_setup(new_class, new_prop_name, value_ptr, value_size);
    curr_version = atomic_load(&(new_class->curr_version));
    assert( 3 == curr_version);

    next_version = atomic_load(&(new_class->next_version)); 
    assert( 4 == next_version);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 2 == log_pl_len);
    assert( 4 == phys_pl_len);

    first_prop = new_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    target_prop = next_ptr.ptr;

    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);
    assert( ! target_prop->sentinel);

    next_ptr = atomic_load(&(target_prop->next));
    second_prop = next_ptr.ptr;

    assert(target_prop->chksum == second_prop->chksum);
    
    cmp_result = strcmp(target_prop->name, second_prop->name);
    assert(cmp_result == 0);
    
    prop1_create_ver = atomic_load(&(target_prop->create_version));
    prop2_create_ver = atomic_load(&(second_prop->create_version));
    assert(prop1_create_ver > prop2_create_ver);


    /* Testing H5P__mt_create_list() */

    new_list = H5P__mt_create_list(new_class, 0);

    assert(new_list);
    assert(new_list->tag == H5P_MT_LIST_TAG);

    assert(new_list->pclass_ptr == new_class);
    assert(new_list->pclass_version == curr_version);

    curr_version = atomic_load(&(new_list->curr_version));
    next_version = atomic_load(&(new_list->next_version));

    assert(curr_version == 1);
    assert(next_version == 2);

    lkup_tbl = atomic_load(&(new_list->lkup_tbl));
    assert(lkup_tbl);
    
    tbl_entry = &lkup_tbl[0];
    tbl_prop_ref = atomic_load(&(tbl_entry->base));
    assert(tbl_prop_ref.ptr);
    assert(tbl_prop_ref.ver == 1);
    assert(tbl_prop_ref.ptr == target_prop);

    assert( 1 == new_list->nprops_inherited);
    assert( 0 == (atomic_load(&(new_list->nprops_added))));
    assert( 1 == (atomic_load(&(new_list->nprops))));

    first_prop = atomic_load(&(new_list->pl_head));
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);

    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));
    assert(second_prop);
    assert(second_prop->tag);
    assert(second_prop->sentinel);


    /* Testing inserting a property into a list's LFSLL */

    new_prop_name = malloc(MAX_PROP_NAME_LEN);

    snprintf(new_prop_name, MAX_PROP_NAME_LEN, "prop_2");

    value_size = snprintf(NULL, 0, "This is the value for prop_2.") + 1;
    value_ptr  = malloc(value_size);

    snprintf(value_ptr, value_size, "This is the value for prop_2.");

    H5P__mt_insert_prop_setup(new_list, new_prop_name, value_ptr, value_size);

    curr_version = atomic_load(&(new_list->curr_version));
    assert( 2 == curr_version);

    next_version = atomic_load(&(new_list->next_version)); 
    assert( 3 == next_version);

    log_pl_len = atomic_load(&(new_list->log_pl_len));
    phys_pl_len = atomic_load(&(new_list->phys_pl_len));
    assert( 1 == log_pl_len);
    assert( 3 == phys_pl_len);
    
    assert( 1 == new_list->nprops_inherited);
    assert( 1 == (atomic_load(&(new_list->nprops_added))));
    assert( 2 == (atomic_load(&(new_list->nprops))));


    /* Testing H5P__set_delete_version() on a prop in a class */

    H5P__set_delete_version(new_class, target_prop);

    curr_version = atomic_load(&(new_class->curr_version));
    assert( 4 == curr_version);

    next_version = atomic_load(&(new_class->next_version)); 
    assert( 5 == next_version);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 2 == log_pl_len);
    assert( 4 == phys_pl_len);

    assert( 4 == (atomic_load(&(target_prop->delete_version))));


    /* Testing H5P__set_delete_version() on a prop in a list's lkup_tbl */

    H5P__set_delete_version(new_list, target_prop);

    curr_version = atomic_load(&(new_list->curr_version));
    assert( 3 == curr_version);

    next_version = atomic_load(&(new_list->next_version)); 
    assert( 4 == next_version);

    log_pl_len = atomic_load(&(new_list->log_pl_len));
    phys_pl_len = atomic_load(&(new_list->phys_pl_len));
    assert( 1 == log_pl_len);
    assert( 3 == phys_pl_len);

    tbl_entry = &new_list->lkup_tbl[0];
    assert( 3 == (atomic_load(&(tbl_entry->base_delete_version))));


    /* Testing H5P__set_delete_version() on a prop in a list's LFSLL*/

    first_prop  = new_list->pl_head;
    next_ptr    = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));
    
    cmp_result = strcmp(second_prop->name, "prop_2");

    assert(cmp_result == 0);
    //assert(second_prop->name == "prop_2");

    H5P__set_delete_version(new_list, second_prop);

    curr_version = atomic_load(&(new_list->curr_version));
    assert( 4 == curr_version);

    next_version = atomic_load(&(new_list->next_version)); 
    assert( 5 == next_version);

    log_pl_len = atomic_load(&(new_list->log_pl_len));
    phys_pl_len = atomic_load(&(new_list->phys_pl_len));
    assert( 1 == log_pl_len);
    assert( 3 == phys_pl_len);

    assert( 4 == (atomic_load(&(second_prop->delete_version))));



    /* Testing H5P__clear_mt_list() */

    check_value = H5P__clear_mt_list(new_list);
    assert(check_value >= 0);

    /* Testing H5P__clear_mt_class() */

    check_value = H5P__clear_mt_class(new_class);
    assert(check_value >= 0);

    check_value = H5P__clear_mt_class(root_class);
    assert(check_value >= 0);

    /* Testing H5P__clear_prop_free_list() and H5P__clear_mt_prop() */

    check_value = H5P__clear_prop_free_list();
    assert(check_value >= 0);


} /* end simple_test_1() */



/****************************************************************************************
 * Function:    serial_class_test
 *
 * Purpose:     Tests the H5P__mt_create_class() function with more detail. Still serial
 *              tests, but manually sets flags to ensure certain things get triggered 
 *              correctly.
 *
 * Return:      void   
 *
 ****************************************************************************************
 */
void serial_class_test(void)
{
    H5P_mt_class_sptr_t          class_fl_next;
    H5P_mt_class_t             * root_class;
    H5P_mt_class_t             * att_class;
    H5P_mt_class_t             * group_class;
    const char                 * new_class_name;
    uint64_t                     curr_version;
    H5P_mt_active_thread_count_t thrd;
    char                       * prop_name;
    char                       * value_str;
    char                       * test_value_str;
    void                       * value_ptr;
    size_t                       value_size;
    H5P_mt_prop_t              * first_prop;
    H5P_mt_prop_t              * second_prop;
    H5P_mt_prop_aptr_t           next_ptr;
    H5P_mt_prop_value_t          prop_value;
    uint32_t                     log_pl_len;
    uint32_t                     phys_pl_len;



    printf("MT PROP serial_create_class_test\n");


    /* Initializes the root class which is needed as a base for H5P_mt */
    H5P_init();

    class_fl_next = atomic_load(&(H5P_mt_g.class_fl_head));

    root_class = class_fl_next.ptr;

    assert(root_class);
    assert(root_class->tag == H5P_MT_CLASS_TAG);


    /* Creates a new class derived from the root class */
    curr_version = atomic_load(&(root_class->curr_version));

    new_class_name = "attribute access";

    att_class = H5P__mt_create_class(root_class, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS, 0);




    /**
     * Manually sets att_class's thrd.opening to TRUE to ensure that condition is
     * triggered and and that a new class can't be created until opening is completed.
     */
    assert( 0 == (atomic_load(&(att_class->num_thrd_opening_flag_set))) );

    thrd = atomic_load(&(att_class->thrd));

    thrd.opening = TRUE;

    atomic_store(&(att_class->thrd), thrd);

    new_class_name = "group access";

    group_class = H5P__mt_create_class(att_class, new_class_name,
                                       H5P_TYPE_GROUP_ACCESS, 0);

    assert( group_class == NULL );
    assert( 1 == (atomic_load(&(att_class->num_thrd_opening_flag_set))) );

    /* Manually set thrd.opening back to FALSE */
    thrd = atomic_load(&(att_class->thrd));

    thrd.opening = FALSE;

    atomic_store(&(att_class->thrd), thrd);




    /**
     * Manually sets att_class's thrd.closing to TRUE to ensure that condition is
     * triggered and and that a new class can't be created until closing is completed.
     * 
     * *** Will print and error message to terminal ***
     */
    assert( 0 == (atomic_load(&(att_class->num_thrd_closing_flag_set))) );

    thrd = atomic_load(&(att_class->thrd));

    thrd.closing = TRUE;

    atomic_store(&(att_class->thrd), thrd);

    group_class = H5P__mt_create_class(att_class, new_class_name,
                                       H5P_TYPE_GROUP_ACCESS, 0);

    assert( group_class == NULL );
    assert( 1 == (atomic_load(&(att_class->num_thrd_closing_flag_set))) );

    /* Manually set thrd.closing back to FALSE */
    thrd = atomic_load(&(att_class->thrd));

    thrd.closing = FALSE;

    atomic_store(&(att_class->thrd), thrd);




    /**
     * Will test to make sure that only the valid properties are copied to the new class
     * but doing several inserts to put more properties att_class, and deleting a couple
     * of them, and doing a few insert calls that are 'modifications' to existing props. 
     * 
     * This also tests that att_class handles all of these commands correctly as well.
     */

    /* Inserts 10 new properties */
    for ( int i = 0; i < 10; i++ )
    {
        prop_name = malloc(MAX_PROP_NAME_LEN);

        snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

        value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
        value_ptr = malloc(value_size);
        if ( ! value_ptr )
        {
            fprintf(stderr, "Failed to allocate memory for value");
        }

        snprintf(value_ptr, value_size, "This is the value for %s.", prop_name);

        H5P__mt_insert_prop_setup(att_class, prop_name, (void *)value_ptr, value_size);

    }



    /* Sets a delete version of a property */
    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    for ( int i = 0; i < 3; i++ )
    {
        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
    }

    H5P__set_delete_version(att_class, second_prop);




    /* 'Modifies' a property */

    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    prop_name = second_prop->name;
    value_size = snprintf(NULL, 0, "This is the value for %s, modified.", 
                          prop_name) + 1;
    value_ptr = malloc(value_size);
    if ( ! value_ptr )
    {
        fprintf(stderr, "Failed to allocate memory for value");
    }
    
    snprintf(value_ptr, value_size, "This is the value for %s, modified.", 
             prop_name);


    H5P__mt_insert_prop_setup(att_class, prop_name, (void *)value_ptr, value_size);




    /* Gets the version of the class we want to derive a new class from */

    curr_version = atomic_load(&(att_class->curr_version));



    /* 'Modifies' a second property */

    next_ptr = atomic_load(&(second_prop->next));
    first_prop = atomic_load(&(next_ptr.ptr));

    prop_name = first_prop->name;
    value_size = snprintf(NULL, 0, "This is the value for %s, modified.", 
                          prop_name) + 1;
    value_ptr = malloc(value_size);
    if ( ! value_ptr )
    {
        fprintf(stderr, "Failed to allocate memory for value");
    }
    snprintf(value_ptr, value_size, "This is the value for %s, modified.", 
             prop_name);


    H5P__mt_insert_prop_setup(att_class, prop_name, (void *)value_ptr, value_size);



    /* Sets a delete version of a property */

    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    for ( int i = 0; i < 6; i++ )
    {
        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
    }

    H5P__set_delete_version(att_class, second_prop);



/** NOTE: This is a way to print info that was used to develop the tests */
#if 0
    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    
    while ( first_prop )
    {
        prop_value = atomic_load(&(first_prop->value));
        value_str = (char *)prop_value.ptr;

        printf("\nProp chksum: %lld,\nProp name: %s,\nProp value: %s,\ncreate_ver: %lld,\ndelete_ver: %lld\n", 
              (unsigned long long)first_prop->chksum,
               first_prop->name,
               value_str,
              (unsigned long long)(atomic_load(&(first_prop->create_version))),
              (unsigned long long)(atomic_load(&(first_prop->delete_version))) );

        
            first_prop = next_ptr.ptr;
            if ( first_prop )
                next_ptr = atomic_load(&(first_prop->next));
    }
#endif
    


    /**
     * Creates a new class from the version after one delete_version was set and one
     * 'modification' was done but before the second delete_version and second 
     * 'modification' to test the correct properties were copied into the new class.
     */

    group_class = H5P__mt_create_class(att_class, new_class_name,
                                       H5P_TYPE_GROUP_ACCESS, curr_version);

    assert(group_class);
    assert(group_class->tag == H5P_MT_CLASS_TAG);

    /* Checks that the lengths are what they should be */
    phys_pl_len = atomic_load(&(group_class->phys_pl_len));    
    assert(phys_pl_len == 11);

    log_pl_len = atomic_load(&(group_class->log_pl_len));
    assert(log_pl_len == 9);

    first_prop = group_class->pl_head;
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);

    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    /**
     * Iterates through group_class's LFSLL and ensures only the expected 
     * properties were copied over when it was derived from app_class.
     */
    for ( int i = 0; i < log_pl_len; i++ )
    {
        assert(second_prop);
        assert(second_prop->tag == H5P_MT_PROP_TAG);

        prop_value = atomic_load(&(second_prop->value));

        value_ptr = prop_value.ptr;
        value_str = (char *)value_ptr;

        if ( i == 0 )
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

            value_size = snprintf(NULL, 0, "This is the value for %s, modified.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s, modified.", prop_name);

            //printf("%s\n", value_str);
            //printf("%s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }
        else if ( i >= 3 )
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", (i + 1) );

            value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s.", prop_name);

            //printf("%s\n", value_str);
            //printf("%s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }
        else
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

            value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s.", prop_name);

            //printf("value.ptr: %s\n", value_str);
            //printf("test_value: %s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }

        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
        
    } /* end for() */

    H5P__clear_mt_class(group_class);
    H5P__clear_mt_class(att_class);
    H5P__clear_mt_class(root_class);
    
} /* end serial_class_test() */



/**
 * 
 */
void serial_list_test(void)
{
    H5P_mt_class_sptr_t          class_fl_next;
    H5P_mt_class_t             * root_class;
    H5P_mt_class_t             * att_class;
    H5P_mt_list_t              * test_list;
    const char                 * new_class_name;
    uint64_t                     curr_version;
    H5P_mt_active_thread_count_t thrd;
    char                       * prop_name;
    char                       * value_str;
    char                       * test_value_str;
    void                       * value_ptr;
    size_t                       value_size;
    H5P_mt_prop_t              * first_prop;
    H5P_mt_prop_t              * second_prop;
    H5P_mt_prop_aptr_t           next_ptr;
    H5P_mt_prop_value_t          prop_value;
    uint32_t                     log_pl_len;
    uint32_t                     phys_pl_len;
    H5P_mt_list_table_entry_t  * entry;
    H5P_mt_list_prop_ref_t       base;
    H5P_mt_list_prop_ref_t       curr;
    uint64_t                     base_delete_version;
    int32_t                      cmp_result;




    printf("MT PROP serial_create_list_test\n");


    /* Initializes H5P multithread */
    H5P_init();

    class_fl_next = atomic_load(&(H5P_mt_g.class_fl_head));

    root_class = class_fl_next.ptr;

    assert(root_class);
    assert(root_class->tag == H5P_MT_CLASS_TAG);


    /* Creates a new class derived from the root class to derive a list from */
    curr_version = atomic_load(&(root_class->curr_version));

    new_class_name = "attribute access";

    att_class = H5P__mt_create_class(root_class, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS, 0);


    /**
     * Manually sets att_class's thrd.opening to TRUE to ensure that condition is
     * triggered and and that a new list can't be created until opening is completed.
     */
    assert( 0 == (atomic_load(&(att_class->num_thrd_opening_flag_set))) );

    thrd = atomic_load(&(att_class->thrd));

    thrd.opening = TRUE;

    atomic_store(&(att_class->thrd), thrd);

    test_list = H5P__mt_create_list(att_class, 0);

    assert( test_list == NULL );
    assert( 1 == (atomic_load(&(att_class->num_thrd_opening_flag_set))) );

    /* Manually set thrd.opening back to FALSE */
    thrd = atomic_load(&(att_class->thrd));

    thrd.opening = FALSE;

    atomic_store(&(att_class->thrd), thrd);


    /**
     * Manually sets att_class's thrd.closing to TRUE to ensure that condition is
     * triggered and and that a new list can't be created until closing is completed.
     * 
     * *** Will print and error message to terminal ***
     */


    assert( 0 == (atomic_load(&(att_class->num_thrd_closing_flag_set))) );

    thrd = atomic_load(&(att_class->thrd));

    thrd.closing = TRUE;

    atomic_store(&(att_class->thrd), thrd);

    test_list = H5P__mt_create_list(att_class, 0);

    assert( test_list == NULL );
    assert( 1 == (atomic_load(&(att_class->num_thrd_closing_flag_set))) );

    /* Manually set thrd.closing back to FALSE */
    thrd = atomic_load(&(att_class->thrd));

    thrd.closing = FALSE;

    atomic_store(&(att_class->thrd), thrd);


    /**
     * Will test to make sure that only the valid properties are copied to the new class
     * but doing several inserts to put more properties att_class, and deleting a couple
     * of them, and doing a few insert calls that are 'modifications' to existing props. 
     */


    /* Inserts 10 new properties */
    for ( int i = 0; i < 10; i++ )
    {
        prop_name = malloc(MAX_PROP_NAME_LEN);

        snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

        value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
        value_ptr = malloc(value_size);
        if ( ! value_ptr )
        {
            fprintf(stderr, "Failed to allocate memory for value");
        }

        snprintf(value_ptr, value_size, "This is the value for %s.", prop_name);

        H5P__mt_insert_prop_setup(att_class, prop_name, (void *)value_ptr, value_size);

    }


    /* Sets a delete version of a property */
    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    for ( int i = 0; i < 3; i++ )
    {
        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
    }

    H5P__set_delete_version(att_class, second_prop);




    /* 'Modifies' a property */

    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    prop_name = second_prop->name;
    value_size = snprintf(NULL, 0, "This is the value for %s, modified.", 
                          prop_name) + 1;
    value_ptr = malloc(value_size);
    if ( ! value_ptr )
    {
        fprintf(stderr, "Failed to allocate memory for value");
    }
    
    snprintf(value_ptr, value_size, "This is the value for %s, modified.", 
             prop_name);


    H5P__mt_insert_prop_setup(att_class, prop_name, (void *)value_ptr, value_size);




    /* Gets the version of the class we want to derive a new class from */

    curr_version = atomic_load(&(att_class->curr_version));



    /* 'Modifies' a second property */

    next_ptr = atomic_load(&(second_prop->next));
    first_prop = atomic_load(&(next_ptr.ptr));

    prop_name = first_prop->name;
    value_size = snprintf(NULL, 0, "This is the value for %s, modified.", 
                          prop_name) + 1;
    value_ptr = malloc(value_size);
    if ( ! value_ptr )
    {
        fprintf(stderr, "Failed to allocate memory for value");
    }
    snprintf(value_ptr, value_size, "This is the value for %s, modified.", 
             prop_name);


    H5P__mt_insert_prop_setup(att_class, prop_name, (void *)value_ptr, value_size);



    /* Sets a delete version of a property */

    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    for ( int i = 0; i < 6; i++ )
    {
        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
    }

    H5P__set_delete_version(att_class, second_prop);


/** NOTE: This is a way to print info that was used to develop the tests */
#if 0
    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    
    while ( first_prop )
    {
        prop_value = atomic_load(&(first_prop->value));
        value_str = (char *)prop_value.ptr;

        printf("\nProp chksum: %lld,\nProp name: %s,\nProp value: %s,\ncreate_ver: %lld,\ndelete_ver: %lld\n", 
              (unsigned long long)first_prop->chksum,
               first_prop->name,
               value_str,
              (unsigned long long)(atomic_load(&(first_prop->create_version))),
              (unsigned long long)(atomic_load(&(first_prop->delete_version))) );

        
            first_prop = next_ptr.ptr;
            if ( first_prop )
                next_ptr = atomic_load(&(first_prop->next));
    }
#endif



    /**
     * Creates a new list from the version after one delete_version was set and one
     * 'modification' was done but before the second delete_version and second 
     * 'modification' to test the correct properties are setup in the lkup_tbl.
     */

    test_list = H5P__mt_create_list(att_class, curr_version);

    assert(test_list);
    assert(test_list->tag == H5P_MT_LIST_TAG);

    /* Checks the correct number of props were stored in the lkup_tbl */
    assert(test_list->nprops_inherited == 9);

    /* Checks that the lengths are what they should be */
    phys_pl_len = atomic_load(&(test_list->phys_pl_len));    
    assert(phys_pl_len == 2);

    log_pl_len = atomic_load(&(test_list->log_pl_len));
    assert(log_pl_len == 0);


    /* Iterates the lkup_tbl and ensures only the expected properties were setup */

    for ( uint32_t i = 0; i < test_list->nprops_inherited; i++ )
    {
        entry = &test_list->lkup_tbl[i];

        base = atomic_load(&(entry->base));
        
        assert(base.ptr);
        assert(base.ver == 1);

        prop_value = atomic_load(&(base.ptr->value));

        value_ptr = prop_value.ptr;
        value_str = (char *)value_ptr;

        assert(entry->chksum == base.ptr->chksum);

        cmp_result = strcmp(entry->name, base.ptr->name);

        if ( i == 0 )
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

            value_size = snprintf(NULL, 0, "This is the value for %s, modified.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s, modified.", prop_name);

            //printf("%s\n", value_str);
            //printf("%s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }
        else if ( i >= 3 )
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", (i + 1) );

            value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s.", prop_name);

            //printf("%s\n", value_str);
            //printf("%s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }
        else
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

            value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s.", prop_name);

            //printf("value.ptr: %s\n", value_str);
            //printf("test_value: %s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }

    } /* end for() */



    /**
     * Tests doing several inserts, 'modifications', and setting the delete_version
     * on props.
     */

    /**
     * Inserts 10 new properties, starts at 8 to test a couple inserts that have versions
     * in the lkup_tbl that will need to update the curr struct.
     */
    for ( int i = 8; i < 18; i++ )
    {
        prop_name = malloc(MAX_PROP_NAME_LEN);

        snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

        if ( i < 10 )
        {
            value_size = snprintf(NULL, 0, "This is the value for %s, modified.", prop_name) + 1;
            value_ptr = malloc(value_size);
            if ( ! value_ptr )
            {
                fprintf(stderr, "Failed to allocate memory for value");
            }
    
            snprintf(value_ptr, value_size, "This is the value for %s, modified.", prop_name);
    
            H5P__mt_insert_prop_setup(test_list, prop_name, (void *)value_ptr, value_size);
        }
        else
        {
            value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
            value_ptr = malloc(value_size);
            if ( ! value_ptr )
            {
                fprintf(stderr, "Failed to allocate memory for value");
            }

            snprintf(value_ptr, value_size, "This is the value for %s.", prop_name);

            H5P__mt_insert_prop_setup(test_list, prop_name, (void *)value_ptr, value_size);
        }

    }

    assert(12 == (atomic_load(&(test_list->phys_pl_len))));
    assert(10 == (atomic_load(&(test_list->log_pl_len))));

    
    /* Sets delete_version of a property in the lkup_tbl */

    /* Manually setting the fields of the property we need to search for it */
    first_prop->tag = H5P_MT_PROP_TAG;
    
    first_prop->chksum = 148505174;
    
    first_prop->name   = malloc(MAX_PROP_NAME_LEN);
    snprintf(first_prop->name, MAX_PROP_NAME_LEN, "prop_6");

    atomic_store(&(first_prop->create_version), 8);

    /* This is testing search_prop() while giving us the actual property we want */
    second_prop = H5P__mt_search_prop(test_list, first_prop);

    H5P__set_delete_version(test_list, second_prop);


    /* Checks that the delete_version was set */

    entry = &test_list->lkup_tbl[5];

    assert(entry->chksum == 148505174);
    assert(entry->base_delete_version > 0);



    /* Sets delete_version of a property in the LFSLL */

    /* Manually setting the fields of the property we need to search for it */
    first_prop->tag = H5P_MT_PROP_TAG;

    first_prop->chksum = 190448261;
    
    first_prop->name   = malloc(MAX_PROP_NAME_LEN);
    snprintf(first_prop->name, MAX_PROP_NAME_LEN, "prop_14");

    atomic_store(&(first_prop->create_version), 8);
    atomic_store(&(first_prop->delete_version), 0);

    /* This is testing search_prop() while giving us the actual property we want */
    second_prop = H5P__mt_search_prop(test_list, first_prop);

    H5P__set_delete_version(test_list, second_prop);

    /* Sets delete_version of a property with a curr.ptr pointing to it in the LFSLL */

    /* Manually setting the fields of the property we need to search for it */
    first_prop->tag = H5P_MT_PROP_TAG;

    first_prop->chksum = 148636248;
    
    first_prop->name   = malloc(MAX_PROP_NAME_LEN);
    snprintf(first_prop->name, MAX_PROP_NAME_LEN, "prop_8");

    atomic_store(&(first_prop->create_version), 2);
    atomic_store(&(first_prop->delete_version), 0);

    /* This is testing search_prop() while giving us the actual property we want */
    second_prop = H5P__mt_search_prop(test_list, first_prop);

    H5P__set_delete_version(test_list, second_prop);


    first_prop = test_list->pl_head;
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);

    next_ptr = atomic_load(&(first_prop->next));

    first_prop = next_ptr.ptr;

    for (int i = 8; i < (log_pl_len + 8); i++ )
    {
        assert(first_prop);
        assert(first_prop->tag == H5P_MT_PROP_TAG);

        prop_value = atomic_load(&(first_prop->value));

        value_ptr = prop_value.ptr;
        value_str = (char *)value_ptr;

        if ( i < 10 )
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

            value_size = snprintf(NULL, 0, "This is the value for %s, modified.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s, modified.", prop_name);

            //printf("%s\n", value_str);
            //printf("%s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }
        else
        {
            snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

            value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
            test_value_str = malloc(value_size);
            if ( ! test_value_str )
            {
                fprintf(stderr, "Failed to allocate memory for test value");
            }

            snprintf(test_value_str, value_size, "This is the value for %s.", prop_name);

            //printf("value.ptr: %s\n", value_str);
            //printf("test_value: %s\n", test_value_str);

            assert( 0 == strcmp(value_str, test_value_str) );
        }

        next_ptr = atomic_load(&(first_prop->next));
        first_prop = next_ptr.ptr;

    } /* end for() */


    H5P__clear_mt_list(test_list);
    H5P__clear_mt_class(att_class);
    H5P__clear_mt_class(root_class);


} /* serial_list_test() */



int main(void)
{
    int num_threads;

    simple_test_1();
    serial_class_test();
    serial_list_test();


    printf("\nTesting MT PROP finished!\n");

    return(0);

} /* end main() */


//#endif /* H5_HAVE_MULTITHREAD */