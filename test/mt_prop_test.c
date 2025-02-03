//#include "h5test.h"
//#include "H5private.h"

#include "../src/H5Pint_mt.c"
#include "../src/H5Ppkg_mt.h"

#include <stdio.h>
#include <stdatomic.h>

#define MAX_PROP_NAME_LEN 50

void simple_test_1(void);
void serial_create_class_test(void);
void serial_calc_checksum_test(void);




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
    const char              * new_prop_name;
    void                    * value_ptr;
    size_t                    value_size;
    uint64_t                  prop1_create_ver;
    uint64_t                  prop2_create_ver;
    uint64_t                  curr_version;
    uint64_t                  next_version;
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


    printf("\nMT PROP serial simple test #1\n");


    H5P_init();

    assert(H5P_mt_rootcls_g);
    assert(H5P_mt_rootcls_g->tag == H5P_MT_CLASS_TAG);
    
    curr_version = atomic_load(&(H5P_mt_rootcls_g->curr_version));
    assert( 1 == curr_version);

    next_version = atomic_load(&(H5P_mt_rootcls_g->next_version)); 
    assert( 2 == next_version);

    ref_count = atomic_load(&(H5P_mt_rootcls_g->ref_count));
    assert( 0 == ref_count.plc);

    first_prop = atomic_load(&(H5P_mt_rootcls_g->pl_head));
    
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);
    
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);
    assert(second_prop->sentinel);


    printf("  testing create_class\n");

    new_class_name = "attribute access";

    new_class = H5P__mt_create_class(H5P_mt_rootcls_g, curr_version, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS);

    assert(new_class);
    assert(new_class->tag == H5P_MT_CLASS_TAG);

    ref_count = atomic_load(&(H5P_mt_rootcls_g->ref_count));
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


    printf("  testing insert_prop_class\n");

    new_prop_name = "prop_1";
    value_ptr = "This is the value for prop_1.";
    value_size = sizeof(value_ptr);

    H5P__insert_prop_class(new_class, new_prop_name, value_ptr, value_size);

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


    printf("  testing insert_prop_class (with a prop of same name)\n");

    H5P__insert_prop_class(new_class, new_prop_name, value_ptr, value_size);
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
    assert(target_prop->name == second_prop->name);
    
    prop1_create_ver = atomic_load(&(target_prop->create_version));
    prop2_create_ver = atomic_load(&(second_prop->create_version));
    assert(prop1_create_ver > prop2_create_ver);


    printf("  testing create_list\n");

    new_list = H5P__mt_create_list(new_class, curr_version);

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


    printf("  testing insert_prop_list\n");

    new_prop_name = "prop_2";
    value_ptr = "This is the value for prop_2.";
    value_size = sizeof(value_ptr);

    H5P__insert_prop_list(new_list, new_prop_name, value_ptr, value_size);

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


    printf("  testing delete_prop_class\n");

    H5P__delete_prop_class(new_class, target_prop);

    curr_version = atomic_load(&(new_class->curr_version));
    assert( 4 == curr_version);

    next_version = atomic_load(&(new_class->next_version)); 
    assert( 5 == next_version);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 2 == log_pl_len);
    assert( 4 == phys_pl_len);

    assert( 4 == (atomic_load(&(target_prop->delete_version))));


    printf("  testing delete_prop_list (in lkup_tbl)\n");

    H5P__delete_prop_list(new_list, target_prop);

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


    printf("  testing delete_prop_list (in LFSLL)\n");

    first_prop  = new_list->pl_head;
    next_ptr    = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    assert(second_prop->name == "prop_2");

    H5P__delete_prop_list(new_list, second_prop);

    curr_version = atomic_load(&(new_list->curr_version));
    assert( 4 == curr_version);

    next_version = atomic_load(&(new_list->next_version)); 
    assert( 5 == next_version);

    log_pl_len = atomic_load(&(new_list->log_pl_len));
    phys_pl_len = atomic_load(&(new_list->phys_pl_len));
    assert( 1 == log_pl_len);
    assert( 3 == phys_pl_len);

    assert( 4 == (atomic_load(&(second_prop->delete_version))));



    printf("  testing clear_list\n");

    H5P__clear_mt_list(new_list);

    printf("  testing clear_class\n");

    H5P__clear_mt_class(new_class);

    H5P__clear_mt_class(H5P_mt_rootcls_g);


} /* end simple_test_1() */



/****************************************************************************************
 * Function:    serial_create_class_test
 *
 * Purpose:     Tests the H5P__mt_create_class() function with more detail. Still serial
 *              tests, but manually sets flags to ensure certain things get triggered 
 *              correctly.
 *
 * Return:      void   
 *
 ****************************************************************************************
 */
void serial_create_class_test(void)
{
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



    printf("\nMT PROP serial_create_class_test\n");


    /* Initializes the root class which is needed as a base for H5P_mt */
    H5P_init();

    assert(H5P_mt_rootcls_g);
    assert(H5P_mt_rootcls_g->tag == H5P_MT_CLASS_TAG);


    /* Creates a new class derived from the root class */
    curr_version = atomic_load(&(H5P_mt_rootcls_g->curr_version));

    new_class_name = "attribute access";

    att_class = H5P__mt_create_class(H5P_mt_rootcls_g, curr_version, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS);




    /**
     * Manually sets att_class's thrd.opening to TRUE to ensure that condition is
     * triggered and and that a new class can't be created until opening is completed.
     */

    assert( 0 == (atomic_load(&(att_class->class_num_thrd_opening_flag_set))) );

    thrd = atomic_load(&(att_class->thrd));

    thrd.opening = TRUE;

    atomic_store(&(att_class->thrd), thrd);

    new_class_name = "group access";

    group_class = H5P__mt_create_class(att_class, curr_version, new_class_name,
                                       H5P_TYPE_GROUP_ACCESS);

    assert( group_class == NULL );
    assert( 1 == (atomic_load(&(att_class->class_num_thrd_opening_flag_set))) );

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

    assert( 0 == (atomic_load(&(att_class->class_num_thrd_closing_flag_set))) );

    thrd = atomic_load(&(att_class->thrd));

    thrd.closing = TRUE;

    atomic_store(&(att_class->thrd), thrd);

    group_class = H5P__mt_create_class(att_class, curr_version, new_class_name,
                                       H5P_TYPE_GROUP_ACCESS);

    assert( group_class == NULL );
    assert( 1 == (atomic_load(&(att_class->class_num_thrd_closing_flag_set))) );

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
        char prop_name[MAX_PROP_NAME_LEN];

        snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

        value_size = snprintf(NULL, 0, "This is the value for %s.", prop_name) + 1;
        value_str = malloc(value_size);
        if ( ! value_str )
        {
            fprintf(stderr, "Failed to allocate memory for value");
        }

        snprintf(value_str, value_size, "This is the value for %s.", prop_name);

        value_ptr = (void *)value_str;

        H5P__insert_prop_class(att_class, prop_name, value_ptr, value_size);

    }

    /* Gets the version of the class we want to derive a new class from */
    curr_version = atomic_load(&(att_class->curr_version));

    /* 'Modifies' a property */
    first_prop = att_class->pl_head;
    next_ptr = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    prop_name = second_prop->name;
    value_size = snprintf(NULL, 0, "This is the value for %s, modified.", 
                          prop_name) + 1;
    value_str = malloc(value_size);
    if ( ! value_str )
    {
        fprintf(stderr, "Failed to allocate memory for value");
    }
    snprintf(value_str, value_size, "This is the value for %s, modified.", 
             prop_name);

    value_ptr = (void *)value_str;

    H5P__insert_prop_class(att_class, prop_name, value_ptr, value_size);


    /* Sets a delete version of a property */

    for ( int i = 0; i < 6; i++ )
    {
        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
    }

    H5P__delete_prop_class(att_class, second_prop);


    /* Creates a new class from the version before the modification and deletion */

    group_class = H5P__mt_create_class(att_class, curr_version, new_class_name,
                                       H5P_TYPE_GROUP_ACCESS);

    assert(group_class);
    assert(group_class->tag == H5P_MT_CLASS_TAG);

    /* Checks that the lengths are what they should be */
    phys_pl_len = atomic_load(&(group_class->phys_pl_len));    
    assert(phys_pl_len == 12);

    log_pl_len = atomic_load(&(group_class->log_pl_len));
    assert(log_pl_len == 10);

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

        char prop_name[MAX_PROP_NAME_LEN];

        snprintf(prop_name, MAX_PROP_NAME_LEN, "prop_%d", i);

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

        first_prop = second_prop;
        next_ptr = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
        
    }

    H5P__clear_mt_class(group_class);
    H5P__clear_mt_class(att_class);
    H5P__clear_mt_class(H5P_mt_rootcls_g);
    
} /* end serial_create_class_test() */



/**
 * 
 */
void serial_calc_checksum_test(void)
{
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



    printf("\nMT PROP serial_create_class_test\n");


    /* Initializes the root class which is needed as a base for H5P_mt */
    H5P_init();

    assert(H5P_mt_rootcls_g);
    assert(H5P_mt_rootcls_g->tag == H5P_MT_CLASS_TAG);


    /* Creates a new class derived from the root class */
    curr_version = atomic_load(&(H5P_mt_rootcls_g->curr_version));

    new_class_name = "attribute access";

    att_class = H5P__mt_create_class(H5P_mt_rootcls_g, curr_version, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS);

    
       
}



int main(void)
{
    int num_threads;

    simple_test_1();
    serial_create_class_test();
    serial_calc_checksum_test();


    printf("\nTesting MT PROP finished!\n");

    return(0);

} /* end main() */


//#endif /* H5_HAVE_MULTITHREAD */