//#include "h5test.h"
//#include "H5private.h"

#include "../src/H5Pint_mt.c"
#include "../src/H5Ppkg_mt.h"

#include <stdio.h>
#include <stdatomic.h>



void simple_test_1(void);
void simple_test_2(void);




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
    H5P_mt_prop_aptr_t        next_prop;
    const char              * new_prop_name;
    void                    * value_ptr;
    size_t                    value_size;
    uint64_t                  curr_version;
    uint64_t                  next_version;
    H5P_mt_class_t          * new_class;
    const char              * new_class_name;
    H5P_mt_class_ref_counts_t ref_count;
    uint64_t                  log_pl_len;
    uint64_t                  phys_pl_len;
    hid_t                     parent_id;


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
    
    next_prop = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_prop.ptr));

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);
    assert(second_prop->sentinel);


    parent_id = (hid_t)atomic_load(&(H5P_mt_rootcls_g->id));

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
    
    next_prop = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_prop.ptr));

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);
    assert(second_prop->sentinel);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 0 == log_pl_len);
    assert( 2 == phys_pl_len);

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
    next_prop = atomic_load(&(first_prop->next));
    target_prop = next_prop.ptr;

    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);
    assert( ! target_prop->sentinel);

    H5P__delete_prop_class(new_class, target_prop);

    curr_version = atomic_load(&(new_class->curr_version));
    assert( 3 == curr_version);

    next_version = atomic_load(&(new_class->next_version)); 
    assert( 4 == next_version);

    log_pl_len = atomic_load(&(new_class->log_pl_len));
    phys_pl_len = atomic_load(&(new_class->phys_pl_len));
    assert( 1 == log_pl_len);
    assert( 3 == phys_pl_len);

    H5P__clear_mt_class();


} /* end simple_test_1() */



/****************************************************************************************
 * Function:    simple_test_2
 *
 * Purpose:     Further tests the functions and serially tests the function's multithread
 *              safety checks by manually setting them.
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
    H5P_mt_prop_aptr_t        next_prop;
    const char              * new_prop_name;
    void                    * value_ptr;
    size_t                    value_size;
    uint64_t                  curr_version;
    uint64_t                  next_version;
    H5P_mt_class_t          * new_class;
    const char              * new_class_name;
    H5P_mt_class_ref_counts_t ref_count;
    uint64_t                  log_pl_len;
    uint64_t                  phys_pl_len;
    hid_t                     parent_id;


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
    
    next_prop = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_prop.ptr));

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);
    assert(second_prop->sentinel);


    parent_id = (hid_t)atomic_load(&(H5P_mt_rootcls_g->id));

    new_class_name = "attribute access";

    new_class = H5P__mt_create_class(H5P_mt_rootcls_g, curr_version, new_class_name, 
                                     H5P_TYPE_ATTRIBUTE_ACCESS);




                                     
} /* simple_test_2() */



int main(void)
{
    int num_threads;

    simple_test_1();
    simple_test_2();

    return(0);

} /* end main() */


//#endif /* H5_HAVE_MULTITHREAD */