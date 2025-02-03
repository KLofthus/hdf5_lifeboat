/*
 * Purpose: Multi-Thread Safe Generic Property Functions
 */

/****************/
/* Module Setup */
/****************/

#include "H5Pmodule.h" /* This source code file is part of the H5P module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions			*/
#ifdef H5_HAVE_PARALLEL
#include "H5ACprivate.h" /* Metadata cache                       */
#endif                   /* H5_HAVE_PARALLEL */
#include "H5Eprivate.h"  /* Error handling		  	*/
#include "H5Fprivate.h"  /* File access				*/
#include "H5FLprivate.h" /* Free lists                           */
#include "H5Iprivate.h"  /* IDs			  		*/
#include "H5MMprivate.h" /* Memory management			*/
#include "H5Ppkg.h"      /* Property lists		  	*/
#include "H5Ppkg_mt.h"


/****************/
/* Local Macros */
/****************/

//#ifdef H5_HAVE_MULTITHREAD

#define H5P_MT_SINGLE_THREAD_TESTING    1


/******************/
/* Local Typedefs */
/******************/

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/
H5P_mt_class_t * H5P__mt_create_class(H5P_mt_class_t *parent, uint64_t parent_version, 
                                      const char *name, H5P_plist_type_t type);

H5P_mt_prop_t * H5P__copy_lfsll(H5P_mt_prop_t *pl_head, uint64_t curr_version, 
                                H5P_mt_class_t *new_class);

int64_t H5P__calc_checksum(const char *name);

H5P_mt_prop_t *
    H5P__create_prop(const char *name, void *value_ptr, size_t value_size, 
                     bool in_prop_class, uint64_t create_version);

herr_t H5P__insert_prop_class(H5P_mt_class_t *class, const char *name, 
                              void *value, size_t size);

herr_t H5P__delete_prop_class(H5P_mt_class_t *class, H5P_mt_prop_t *prop);

herr_t H5P__clear_mt_class(H5P_mt_class_t *class);

void H5P__find_mod_point(H5P_mt_class_t *class, H5P_mt_list_t *list,
                    H5P_mt_prop_t **first_ptr_ptr, H5P_mt_prop_t **second_ptr_ptr, 
                    int32_t *deletes_ptr, int32_t *nodes_visited_ptr, 
                    int32_t *thrd_cols_ptr, H5P_mt_prop_t *target_prop, 
                    int32_t *cmp_result_ptr);

H5P_mt_list_t * H5P__mt_create_list(H5P_mt_class_t *parent, uint64_t parent_version);

void H5P__init_lkup_tbl(H5P_mt_class_t *parent, uint64_t version, H5P_mt_list_t *list);

herr_t H5P__insert_prop_list(H5P_mt_list_t *list, const char *name, 
                             void *value, size_t size);

herr_t H5P__delete_prop_list(H5P_mt_list_t *list, H5P_mt_prop_t *prop);

herr_t H5P__clear_mt_list(H5P_mt_list_t *list);




/*********************/
/* Package Variables */
/*********************/

hid_t           H5P_CLS_ROOT_ID_g = H5I_INVALID_HID;
H5P_mt_class_t  *H5P_mt_rootcls_g = NULL;


/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/



/****************************************************************************************
 * Function:    H5P_init
 *
 * Purpose:     Initialize the interface from some other layer.
 *
 *              At present, this function performs initializations needed for the 
 *              multi-thread build of H5P.  Thus it need not be called in other contexts.
 * 
 *              H5P_init() creates the root H5P_mt_class_t structure, which is a global
 *              structure for convenienced, with the only entries in it's lock-free 
 *              singly linked list (LFSLL) being the sentinel nodes. 
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t
H5P_init(void)
{
    H5P_mt_prop_t              * pos_sentinel;
    H5P_mt_prop_aptr_t           pos_next;
    H5P_mt_prop_value_t          pos_value;
    H5P_mt_prop_t              * neg_sentinel;
    H5P_mt_prop_aptr_t           neg_next;
    H5P_mt_prop_value_t          neg_value;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_class_ref_counts_t    ref_count;
    H5P_mt_class_sptr_t          fl_next;


    herr_t           ret_value = SUCCEED;


    /* Allocating and initializing the positive sentinel node of the LFSLL */
    pos_sentinel = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! pos_sentinel )
        fprintf(stderr, "\nNew property allocation failed."); 

    /* Initalize property fields */
    pos_sentinel->tag = H5P_MT_PROP_TAG;

    pos_next.ptr          = NULL;
    pos_next.deleted      = FALSE;
    pos_next.dummy_bool_1 = FALSE;
    pos_next.dummy_bool_2 = FALSE;
    pos_next.dummy_bool_3 = FALSE;
    atomic_store(&(pos_sentinel->next), pos_next);

    pos_sentinel->sentinel      = TRUE;
    pos_sentinel->in_prop_class = TRUE;

    /* ref_count is only used if the property is in a property class. */
    atomic_store(&(pos_sentinel->ref_count), 0);

    pos_sentinel->chksum = LLONG_MAX;

    pos_sentinel->name = (char *)"pos_sentinel";

    pos_value.ptr  = NULL;
    pos_value.size = 0;

    atomic_store(&(pos_sentinel->value), pos_value);
    
    atomic_store(&(pos_sentinel->create_version), 1);
    atomic_store(&(pos_sentinel->delete_version), 0); 


    /* Allocating and initializing the negative sentinel node of the LFSLL */
    neg_sentinel = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! neg_sentinel )
        fprintf(stderr, "\nNew property allocation failed."); 

    /* Initalize property fields */
    neg_sentinel->tag = H5P_MT_PROP_TAG;

    neg_next.ptr          = pos_sentinel;
    neg_next.deleted      = FALSE;
    neg_next.dummy_bool_1 = FALSE;
    neg_next.dummy_bool_2 = FALSE;
    neg_next.dummy_bool_3 = FALSE;
    atomic_store(&(neg_sentinel->next), neg_next);

    neg_sentinel->sentinel      = TRUE;
    neg_sentinel->in_prop_class = TRUE;

    /* ref_count is only used if the property is in a property class. */
    atomic_store(&(neg_sentinel->ref_count), 0);

    neg_sentinel->chksum = LLONG_MIN;

    neg_sentinel->name = (char *)"neg_sentinel";

    neg_value.ptr  = NULL;
    neg_value.size = 0;

    atomic_store(&(neg_sentinel->value), neg_value);
    
    atomic_store(&(neg_sentinel->create_version), 1);
    atomic_store(&(neg_sentinel->delete_version), 0); /* Set to 0 because it's not deleted */




    /* Allocating and Initializing the root property list class */


    /* Allocate memory for the root property list class */
    H5P_mt_rootcls_g = (H5P_mt_class_t *)malloc(sizeof(H5P_mt_class_t));
    if ( ! H5P_mt_rootcls_g )
        fprintf(stderr, "New class allocation failed.");


    /* Initialize class fields */
    H5P_mt_rootcls_g->tag = H5P_MT_CLASS_TAG;

    H5P_mt_rootcls_g->parent_id      = 0;
    H5P_mt_rootcls_g->parent_ptr     = NULL;
    H5P_mt_rootcls_g->parent_version = 0;

    H5P_mt_rootcls_g->name = "root";
    atomic_store(&(H5P_mt_rootcls_g->id), H5I_INVALID_HID); 
    H5P_mt_rootcls_g->type = H5P_TYPE_ROOT;

    atomic_store(&(H5P_mt_rootcls_g->curr_version), 1);
    atomic_store(&(H5P_mt_rootcls_g->next_version), 2);

    H5P_mt_rootcls_g->pl_head = neg_sentinel;
    atomic_store(&(H5P_mt_rootcls_g->log_pl_len),  0);
    atomic_store(&(H5P_mt_rootcls_g->phys_pl_len), 2);

    ref_count.pl      = 0;
    ref_count.plc     = 0;
    ref_count.deleted = FALSE;
    atomic_store(&(H5P_mt_rootcls_g->ref_count), ref_count);

    thrd.count   = 0;
    thrd.opening = FALSE;
    thrd.closing = FALSE;
    atomic_store(&(H5P_mt_rootcls_g->thrd), thrd);

    fl_next.ptr = NULL;
    fl_next.sn  = 0;
    atomic_store(&(H5P_mt_rootcls_g->fl_next), fl_next);


    /* Initialize stats */

    /* H5P_mt_class_t comparison stats */
    atomic_init(&(H5P_mt_rootcls_g->class_max_derived_classes),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_max_version_number),         0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_max_num_phys_props),         2ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_max_num_log_props),          0ULL);

    /* H5P_mt_class_t stats */
    atomic_init(&(H5P_mt_rootcls_g->class_insert_total_nodes_visited),   0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_delete_total_nodes_visited),   0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_insert_max_nodes_visited),     0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_delete_max_nodes_visited),     0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_wait_for_curr_version_to_inc), 0ULL);

    /* H5P_mt_active_thread_count_t stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_thrd_count_update_cols), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_thrd_count_update),      0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_thrd_closing_flag_set),  0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_thrd_opening_flag_set),  0ULL);

    /* H5P_mt_class_ref_counts_t stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_ref_count_cols),   0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_ref_count_update), 0ULL);

    /* H5P_mt_class_sptr_t (free list) stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_class_fl_insert_cols), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_class_fl_insert),      0ULL);

    /* H5P__insert_prop_class() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_insert_prop_cols),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_insert_prop_success),     0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_insert_nodes_visited),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__insert_prop_class__num_calls), 0ULL);

    /* H5P__delete_prop_class() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_delete_nodes_visited),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_delete_prop_nonexistant), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_delete_version_set), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__delete_prop_class__num_calls), 0ULL);

    /* H5P__modify_prop_class() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_modify_prop_cols),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_modify_prop_success),     0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_modify_nodes_visited),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__modify_prop_class__num_calls), 0ULL);

    /* H5P__find_mod_point() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_chksum_cols),     0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_name_cols),       0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__find_mod_point__num_calls), 0ULL);
    
    /* H5P__create_prop() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_created),      0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__create_prop__num_calls), 0ULL);

    /* H5P_mt_prop_t stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_props_modified),    0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_max_num_prop_modified), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_freed),        0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_prop_ref_count_cols),   0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_prop_ref_count_update), 0ULL);

} /* H5P_init() */



/****************************************************************************************
 * Function:    H5P__create_class
 *
 * Purpose:     Function to create a new property list class (H5P_mt_class_t) 
 * 
 *              NOTE: property list classes are referred to as classes and property lists
 *              are referred to as lists.
 * 
 *              A multi-thread safe function to create a new class, derived from an 
 *              existing class. The new class copies the properties from it's parent 
 *              class that are valid for the version of the parent class the new class is
 *              derived from.             
 * 
 * Details:     NOTE: for more information on the H5P_mt_class_t structure or specific
 *              fields, check the detailed comment for the structure in H5Ppkg_mt.h
 * 
 *              To ensure multi-thread safety the first step is to check the parent 
 *              class's thrd field to ensure the parent class is not in the process 
 *              opening or closing. If opening is TRUE the function loops checking again. 
 *              If opening, the class will only be briefly visible to other threads 
 *              before completing the opening process, so this thread should see that 
 *              opening's been set to FALSE without waiting long (stats are collected so 
 *              if this proves incorrect, it can be found quickly and fixed). If closing
 *              is true, an error is thrown. If neither are true, the count field in the
 *              parent class's thrd field is incremented to show another thread is 
 *              accessing the structure. 
 * 
 *              Next, memory is allocated for the new H5P_mt_class_t and the fields are 
 *              initialized. The new class's pl_head is initialized to the H5P_mt_prop_t*
 *              that is returned by the function H5P__copy_lfsll. The new class copies
 *              all valid properties from it's parent's LFSLL into it's own LFSLL. 
 *              Two things make a property invalid:
 *                  1) any property that has a create_version greater than the parent 
 *                     class's version from which the new class is derived.
 *                  2) any property that has a delete_version less than or equal to the
 *                     parent class's version from which the new class is derived.
 *              
 *              The thrd.count and thrd.opening fields of the new class are initialized 
 *              to 1 and TRUE respectively. Opening being set to TRUE is to prevent
 *              other threads from accessing the structure until it's completely set up.
 *              
 *              After the fields are initialized, the parent class increments it's 
 *              ref_count->plc field to count the new derived class.
 * 
 *              NOTE: will probably add the code to insert the new class into the index
 *              at this spot in the function.
 * 
 *              Finally, the thrd fields are updated for the new class and parent class.
 *              The new class's thrd.count is decremented, it's thrd.opening is set
 *              to FALSE, and the parent class's thrd.count is decremented.
 *              
 *              
 *
 * Return:      Success: Returns a pointer to the new H5P_mt_class_t struct.
 * 
 *              Failure: NULL
 *
 ****************************************************************************************
 */
H5P_mt_class_t *
H5P__mt_create_class(H5P_mt_class_t *parent, uint64_t parent_version, 
                     const char *name, H5P_plist_type_t type)
{
    H5P_mt_class_t             * new_class;
    hid_t                        parent_id;
    H5P_mt_class_ref_counts_t    parent_ref;
    H5P_mt_class_ref_counts_t    ref_count;
    H5P_mt_class_ref_counts_t    update_ref;
    H5P_mt_active_thread_count_t parent_thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_class_sptr_t          fl_next;
    H5P_mt_class_sptr_t          new_fl_next;
    H5P_mt_class_t             * fl_class_one;
    H5P_mt_class_t             * fl_class_two;
    hbool_t                      done  = FALSE;
    hbool_t                      retry = FALSE;
    hbool_t                      closing = FALSE;

    H5P_mt_class_t * ret_value = NULL;

    assert(parent);
    assert(parent->tag == H5P_MT_CLASS_TAG);


    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        parent_thrd = atomic_load(&(parent->thrd));

        if ( parent_thrd.closing )
        {
            /* update stats */
            atomic_fetch_add(&(parent->class_num_thrd_closing_flag_set), 1);

            fprintf(stderr, "CLOSING flag is set when it shouldn't be.\n");

            done    = TRUE;
            closing = TRUE;

            new_class = NULL;
        }
        else if ( ! parent_thrd.opening )
        {
            update_thrd = parent_thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                   &parent_thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            atomic_fetch_add(&(parent->class_num_thrd_opening_flag_set), 1);

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            done    = TRUE;
            closing = TRUE;

            new_class = NULL;
#endif
        } /* end else() */


    } while ( ! done );


    if ( ! closing )
    {

        /* Allocate memory for the new property list class */

        new_class = (H5P_mt_class_t *)malloc(sizeof(H5P_mt_class_t));
        if ( ! new_class )
        {
            fprintf(stderr, "\nNew class allocation failed.\n");
            return NULL;
        }

        /* Initialize class fields */
        new_class->tag = H5P_MT_CLASS_TAG;

        parent_id = atomic_load(&(parent->id));
        new_class->parent_id      = parent_id;

        new_class->parent_ptr     = parent;
        new_class->parent_version = parent_version;

        new_class->name = name;

        /** NOTE: Needs to be actually set after creation */
        atomic_store(&(new_class->id), H5I_INVALID_HID);
        new_class->type = type;

        atomic_store(&(new_class->curr_version), 1);
        atomic_store(&(new_class->next_version), 2);

        atomic_store(&(new_class->log_pl_len),  0);
        atomic_store(&(new_class->phys_pl_len), 2);

        /* Copies the valid properties from the parent */
        new_class->pl_head = H5P__copy_lfsll(parent->pl_head, parent_version, new_class);

        ref_count.pl      = 0;
        ref_count.plc     = 0;
        ref_count.deleted = FALSE;
        atomic_store(&(new_class->ref_count), ref_count);

        thrd.count   = 1;
        thrd.opening = TRUE;
        thrd.closing = FALSE;
        atomic_store(&(new_class->thrd), thrd);

        fl_next.ptr = NULL;
        fl_next.sn  = 0;
        atomic_store(&(new_class->fl_next), fl_next);

        /* Initialize stats */

        /* H5P_mt_class_t comparison stats */
        atomic_init(&(new_class->class_max_derived_classes),          0ULL);
        atomic_init(&(new_class->class_max_version_number),           0ULL);
        atomic_init(&(new_class->class_max_num_phys_props),           2ULL);
        atomic_init(&(new_class->class_max_num_log_props),            0ULL);

        /* H5P_mt_class_t stats */
        atomic_init(&(new_class->class_insert_total_nodes_visited),   0ULL);
        atomic_init(&(new_class->class_delete_total_nodes_visited),   0ULL);
        atomic_init(&(new_class->class_insert_max_nodes_visited),     0ULL);
        atomic_init(&(new_class->class_delete_max_nodes_visited),     0ULL);
        atomic_init(&(new_class->class_wait_for_curr_version_to_inc), 0ULL);

        /* H5P_mt_active_thread_count_t stats */
        atomic_init(&(new_class->class_num_thrd_count_update_cols),   0ULL);
        atomic_init(&(new_class->class_num_thrd_count_update),        0ULL);
        atomic_init(&(new_class->class_num_thrd_closing_flag_set),    0ULL);
        atomic_init(&(new_class->class_num_thrd_opening_flag_set),    0ULL);

        /* H5P_mt_class_ref_counts_t stats */
        atomic_init(&(new_class->class_num_ref_count_cols),           0ULL);
        atomic_init(&(new_class->class_num_ref_count_update),         0ULL);

        /* H5P_mt_class_sptr_t (free list) stats */
        atomic_init(&(new_class->class_num_class_fl_insert_cols),     0ULL);
        atomic_init(&(new_class->class_num_class_fl_insert),          0ULL);

        /* H5P__insert_prop_class() function stats */
        atomic_init(&(new_class->class_num_insert_prop_cols),         0ULL);
        atomic_init(&(new_class->class_num_insert_prop_success),      0ULL);
        atomic_init(&(new_class->class_insert_nodes_visited),         0ULL);
        atomic_init(&(new_class->H5P__insert_prop_class__num_calls),  0ULL);

        /* H5P__delete_prop_class() function stats */
        atomic_init(&(new_class->class_delete_nodes_visited),         0ULL);
        atomic_init(&(new_class->class_num_delete_prop_nonexistant),  0ULL);
        atomic_init(&(new_class->class_num_prop_delete_version_set),  0ULL);
        atomic_init(&(new_class->H5P__delete_prop_class__num_calls),  0ULL);

        /* H5P__modify_prop_class() function stats */
        atomic_init(&(new_class->class_num_modify_prop_cols),         0ULL);
        atomic_init(&(new_class->class_num_modify_prop_success),      0ULL);
        atomic_init(&(new_class->class_modify_nodes_visited),         0ULL);
        atomic_init(&(new_class->H5P__modify_prop_class__num_calls),  0ULL);

        /* H5P__find_mod_point() function stats */
        atomic_init(&(new_class->class_num_prop_chksum_cols),         0ULL);
        atomic_init(&(new_class->class_num_prop_name_cols),           0ULL);
        atomic_init(&(new_class->H5P__find_mod_point__num_calls),     0ULL);
        
        /* H5P__create_prop() function stats */
        atomic_init(&(new_class->class_num_prop_created),             0ULL);
        atomic_init(&(new_class->H5P__create_prop__num_calls),        0ULL);

        /* H5P_mt_prop_t stats */
        atomic_init(&(new_class->class_num_props_modified),           0ULL);
        atomic_init(&(new_class->class_max_num_prop_modified),        0ULL);
        atomic_init(&(new_class->class_num_prop_freed),               0ULL);


        /* Update Parent class derived counts */
        done = FALSE;

        do
        {
            parent_ref = atomic_load(&(parent->ref_count));
            update_ref = parent_ref;
            update_ref.plc++;

            /* Attempt to atomically update the parent class's count of derived classes */
            if ( ! atomic_compare_exchange_strong(&(parent->ref_count), &parent_ref, update_ref) )
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_ref_count_cols), 1);
            }
            else /* Attempt was successful */
            {
                /* attempt succeeded, update stats and continue */
                atomic_fetch_add(&(parent->class_num_ref_count_update), 1);

                done = TRUE;
            }

        } while ( ! done );

        assert(done);

/** 
 * This is a temporary solution for freeing the 
 * H5P_mt_class_t structs at the end of early testing 
 */
#if 0
        /* Insert new class at the tail of the class free list */
        done = FALSE;
        do 
        {
            assert(!done);

            retry = FALSE;

            fl_class_one = parent;

            fl_next = atomic_load(&(fl_class_one->fl_next));
            fl_class_two = fl_next.ptr;

            do
            {
                /* If fl_class is not the tail, iterate the free list */
                if ( fl_class_two )
                {
                    fl_class_one = fl_class_two;
                    fl_next = atomic_load(&(fl_class_two->fl_next));
                    fl_class_two = fl_next.ptr;
                }
                /* If fl_class is the tail insert new_class at tail */
                else
                {
                    new_fl_next.ptr = new_class;
                    new_fl_next.sn  = fl_next.sn + 1;
                    
                    /** Attempt to atomically insert the new_class.
                     * 
                     * NOTE: if this fails, another thread modified the class free list and
                     * we must update stats and restart to ensure the new_class is correctly
                     * inserted.
                     */
                    if ( ! atomic_compare_exchange_strong(&(fl_class_one->fl_next), 
                                                        &fl_next, new_fl_next))
                    {
                        /* update stats */
                        atomic_fetch_add(&(parent->class_num_class_fl_insert_cols), 1);

                        retry = TRUE;
                    }
                    else /* The attempt was successfule update stats and mark done */
                    {
                        /* update stats */
                        atomic_fetch_add(&(parent->class_num_class_fl_insert), 1);

                        done = TRUE;
                    }
                } /* end else ( ! fl_class_two ) */

            } while ( ( ! done ) && ( ! retry ) );

            assert( ! ( done && retry ) );

        } while ( retry );

        assert(done);
        assert(!retry);
#endif

        /**
         * NOTE: will probably add the code to insert this class into the index here.
         */



        /**
         * Update the thrd structures of the new class and parent class.
         * 
         * NOTE: This update is done with an atomic_compare_exchage_strong() as a safety
         * measure, even through we have the earlier safety measure of other threads
         * waiting for the opening thread to be set to FALSE before incrementing the
         * count.
         * 
         * If the stats and testing show this is unnecessary it can be changed to a more 
         * simple atomic_store().
         */
        done = FALSE;

        do
        {
        
            thrd = atomic_load(&(new_class->thrd));
            
            update_thrd = thrd;
            update_thrd.count--;
            update_thrd.opening = FALSE;

            if ( ! atomic_compare_exchange_strong(&(new_class->thrd), 
                                                &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(new_class->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeed, update stats and continue */
                atomic_fetch_add(&(new_class->class_num_thrd_count_update), 1);
                
                done = TRUE;
            }

        } while ( ! done );

        done = FALSE;

        do
        {
            parent_thrd = atomic_load(&(parent->thrd));

            update_thrd = parent_thrd;
            update_thrd.count--;

            if (! atomic_compare_exchange_strong(&(parent->thrd), &parent_thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeed, update stats and continue */
                atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                done = TRUE;
            }

        } while ( ! done );

    } /* end if ( ! closing )*/

    ret_value = new_class;

    return(ret_value);
   
} /* H5P__create_class() */



/****************************************************************************************
 * Function:    H5P__copy_lfsll
 *
 * Purpose:     Copies the valid properties from the parent class's LFSLL into the new
 *              class's LFSLL.
 *
 *              Two things make a property invalid:
 *                  1) any property that has a create_version greater than the parent 
 *                     class's version from which the new class is derived.
 *                  2) any property that has a delete_version less than or equal to the
 *                     parent class's version from which the new class is derived.
 *
 * Return:      Success: Returns a pointer to the head of the LFSLL for the new class.
 * 
 *              Failure: NULL    
 *
 ****************************************************************************************
 */
H5P_mt_prop_t *
H5P__copy_lfsll(H5P_mt_prop_t *pl_head, uint64_t curr_version, H5P_mt_class_t *new_class)
{
    H5P_mt_prop_t     * new_head;
    H5P_mt_prop_t     * new_prop;
    H5P_mt_prop_t     * prev_prop;
    H5P_mt_prop_t     * parent_prop;
    H5P_mt_prop_aptr_t  nulls_next;
    H5P_mt_prop_aptr_t  new_next;
    H5P_mt_prop_aptr_t  parent_next;
    H5P_mt_prop_value_t parent_value;
    uint64_t            phys_pl_len = 0;
    uint64_t            log_pl_len  = 0;

    H5P_mt_prop_t * ret_value;

    assert(pl_head);
    assert(pl_head->tag ==  H5P_MT_PROP_TAG);
    assert(pl_head->sentinel == TRUE);


    /* Allocate the head of LFSLL, the first sentinel node */
    new_head = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! new_head )
        fprintf(stderr, "\nNew property allocation failed.\n"); 
    
    parent_next  = atomic_load(&(pl_head->next));
    parent_value = atomic_load(&(pl_head->value));

    assert(parent_next.ptr);
    assert(parent_next.ptr->tag == H5P_MT_PROP_TAG);
    
    nulls_next.ptr          = NULL;
    nulls_next.deleted      = FALSE;
    nulls_next.dummy_bool_1 = FALSE;
    nulls_next.dummy_bool_2 = FALSE;
    nulls_next.dummy_bool_3 = FALSE;


    new_head->tag            = H5P_MT_PROP_TAG;

    atomic_store(&(new_head->next), nulls_next);

    new_head->sentinel       = TRUE;
    new_head->in_prop_class  = pl_head->in_prop_class;

    atomic_store(&(new_head->ref_count), 0);

    new_head->chksum         = pl_head->chksum;
    new_head->name           = pl_head->name;

    atomic_store(&(new_head->value), parent_value);
    atomic_store(&(new_head->create_version), 1);
    atomic_store(&(new_head->delete_version), 0);

    prev_prop = new_head;

    /* Increment physical length */
    phys_pl_len++;    

    /* Iterate through the parent LFSLL and copy valid properties */
    while ( parent_next.ptr != NULL )
    {
        /* Update pointers to the next property */
        parent_prop  = atomic_load(&(parent_next.ptr));
        parent_next  = atomic_load(&(parent_prop->next));
        parent_value = atomic_load(&(parent_prop->value));

        /* If the parent property is a valid entry for the new LFSLL copy it */
        if (( parent_prop->create_version <= curr_version ) &&
            (( parent_prop->delete_version > curr_version ) ||
             ( parent_prop->delete_version == 0 ) ) )
        {
            /* Allocate memory for new property */
            new_prop = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
            if ( ! new_prop )
                fprintf(stderr, "New property allocation failed."); 

            /* Copy appropriate fields or set them if copying isn't appropriate */
            new_prop->tag            = H5P_MT_PROP_TAG;

            atomic_store(&(new_prop->next), nulls_next);

            new_prop->sentinel       = parent_prop->sentinel;
            new_prop->in_prop_class  = parent_prop->in_prop_class;

            atomic_store(&(new_prop->ref_count), 0);

            new_prop->chksum         = parent_prop->chksum;
            new_prop->name           = parent_prop->name;

            atomic_store(&(new_prop->value), parent_value);
            atomic_store(&(new_prop->create_version), 1);
            atomic_store(&(new_prop->delete_version), 0);


            new_next = atomic_load(&(prev_prop->next));
            new_next.ptr = new_prop;

            atomic_store(&(prev_prop->next), new_next);

            prev_prop = atomic_load(&(new_prop));

            /* If it's not a sentinel node increment logical length */
            if ( ! parent_prop->sentinel )
            {
                log_pl_len++;
            }
            /* Increment physical length */
            phys_pl_len++;

        } /* end if () */

    } /* end while ( parent_next.ptr != NULL ) */
       
    assert(parent_prop->sentinel);

    /* Atomically store the logical and physical lengths into the new class */
    atomic_store(&(new_class->log_pl_len), log_pl_len);
    atomic_store(&(new_class->phys_pl_len), phys_pl_len);

    ret_value = new_head;

    return(ret_value);

} /* H5P__mt_copy_lfsll() */



/****************************************************************************************
 * Function:    H5P__calc_checksum
 *
 * Purpose:     Creates a checksum (chksum) value for a property based on it's name.
 *
 *              This function uses the Fletcher-32 Checksum Function to create a 32-bit
 *              checksum in a 64-bit integer. A 64-bit size integer is used so the 
 *              sentinel entries as the head and tail in the LFSLL (lock free singly 
 *              linked list) as LLONG MAX and LLONG MIN respectively.
 *              
 *
 * Return:      Success: Returns a 32-bit checksum for the property
 * 
 *              Failure: 0   
 *
 ****************************************************************************************
 */
int64_t 
H5P__calc_checksum(const char* name)
{
    uint32_t        sum1; /* First Fletcher chksum accumulator */
    uint32_t        sum2; /* Second Fletcher chksum accumulator */
    const uint8_t * data; /* value to hold input name in bytes */
    size_t          len;  /* Length of the input name */
    size_t          tlen; /* temp length if name is long enough to be broken up */

    assert(name);

    /* Initializes the accumulators */
    sum1 = 0xFFFF; 
    sum2 = 0xFFFF; 

    /* Casts the input name to bytes */
    data = (const uint8_t *)name; 
    len = strlen(name);  

    /**
     * Loop to process the name in chunks
     * NOTE: Most names should be processed in only one chunk, but this is
     * still set up as a safety net to prevent integer overflow.
     */
    while (len) 
    {
        /* Process chucks of 360 bytes */
        size_t tlen = (len > 360) ? 360 : len; 
        len -= tlen;

        do 
        {
            sum1 += *data++; /* Add the current byte to sum1 */
            sum2 += sum1;    /* Add the updated sum1 to sum2 */
            tlen--;

        } while (tlen);

        /* Reduce sums to 16 bits */
        sum1 = (sum1 & 0xFFFF) + (sum1 >> 16);
        sum2 = (sum2 & 0xFFFF) + (sum2 >> 16);

    } /* end while (len) */

    /* Final reduction to 16 bits */
    sum1 = (sum1 & 0xFFFF) + (sum1 >> 16);
    sum2 = (sum2 & 0xFFFF) + (sum2 >> 16);

    return (sum2 << 16) | sum1; /* Combine the two 16-bit sums into a 32-bit checksum */

} /* H5P__calc_checksum() */



/****************************************************************************************
 * Function:    H5P__create_prop
 *
 * Purpose:     Creates a new property (H5P_mt_prop_t)
 *
 *              This function is called by the H5P__insert_prop_class() and 
 *              H5P__insert_prop_list() functions to allocate and initialize a new 
 *              property.
 * 
 *              H5P__create_prop() calls the function H5P__calc_checksum() to calculate
 *              a property's chksum.
 *              
 *
 * Return:      Success: Returns a pointer to the new H5P_mt_prop_t (property struct)
 * 
 *              Failure: NULL
 *
 ****************************************************************************************
 */
H5P_mt_prop_t *
H5P__create_prop(const char *name, void *value_ptr, size_t value_size, 
                 bool in_prop_class, uint64_t version)
{
    H5P_mt_prop_t     * new_prop;
    H5P_mt_prop_aptr_t  next;
    H5P_mt_prop_value_t value;

    H5P_mt_prop_t * ret_value = NULL;

    assert(name);

    /* Allocate memory for the new property */
    new_prop = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! new_prop )
        fprintf(stderr, "New property allocation failed."); 

    /* Initalize property fields */
    new_prop->tag = H5P_MT_PROP_TAG;

    next.ptr = NULL;
    next.deleted = FALSE;
    next.dummy_bool_1 = FALSE;
    next.dummy_bool_2 = FALSE;
    next.dummy_bool_3 = FALSE;
    atomic_store(&(new_prop->next), next);

    new_prop->sentinel      = FALSE;
    new_prop->in_prop_class = in_prop_class;

    atomic_store(&(new_prop->ref_count), 0);

    new_prop->chksum = H5P__calc_checksum(name);

    assert( new_prop->chksum > LLONG_MIN && new_prop->chksum < LLONG_MAX );

    new_prop->name = (char *)name;

    value.ptr  = value_ptr;
    value.size = value_size;
    atomic_store(&(new_prop->value), value);
    
    atomic_store(&(new_prop->create_version), version);
    atomic_store(&(new_prop->delete_version), 0); /* Set to 0 because it's not deleted */

    ret_value = new_prop;

    return ret_value;

} /* H5P__create_prop() */



/****************************************************************************************
 * Function:    H5P__insert_prop_class
 *
 * Purpose:     Inserts a new property (H5P_mt_prop_t struct) into the LFSLL of a 
 *              property list class (H5P_mt_class_t).
 *
 *              NOTE: When 'modifying' a property is this multithread safe version of 
 *              H5P, a new property must be created with a new create_version. Because
 *              of this, when a entirely new property is created, or a new property is
 *              created for the 'modified' property, this function is called to handle
 *              either case.
 * 
 *              This function firstly calls H5P__create_prop() to create the new
 *              H5P_mt_prop_t struct to store the property info. Secondly, this function 
 *              passes pointers to the void function H5P__find_mod_point() which passes 
 *              back the pointers that have been updated to the two properties in the 
 *              LFSLL where the new property will be inserted. Pointers to values needed 
 *              for stats keeping and a value used for comparing the name field of the 
 *              H5P_mt_prop_t structs is passed for if the properties had the same chksum
 *              field are also passed to H5P__find_mod_point().
 * 
 *              If the new_prop falls between first_prop and second_prop then it is 
 *              atomically inserted between them. 
 * 
 *              NOTE: See the comment description above struct H5P_mt_prop_t in 
 *              H5Ppkg_mt.h for details on how the LFSLL are sorted.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the class, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_class_t in H5Ppkg_mt.h
 *              for more details.
 * 
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t 
H5P__insert_prop_class(H5P_mt_class_t *class, const char *name, void *value, size_t size)
{
    H5P_mt_prop_t    * new_prop;
    H5P_mt_prop_t    * first_prop;
    H5P_mt_prop_t    * second_prop;
    H5P_mt_prop_aptr_t next;
    H5P_mt_prop_aptr_t new_prop_next;
    H5P_mt_prop_aptr_t updated_next;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    uint64_t           curr_version  = 0;
    uint64_t           next_version  = 0;
    int32_t            deletes       = 0;
    int32_t            nodes_visited = 0;
    int32_t            thrd_cols     = 0;
    int32_t            cmp_result    = 0;
    hbool_t            done          = FALSE;
    hbool_t            closing       = FALSE;

    herr_t             ret_value     = SUCCEED;

    assert(class);
    assert(class->tag == H5P_MT_CLASS_TAG);

    assert(name);
    assert((size > 0 && value != NULL) || (size == 0));

    /* update stats */
    atomic_fetch_add(&(class->H5P__insert_prop_class__num_calls), 1);

    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        thrd = atomic_load(&(class->thrd));

        if ( thrd.closing )
        {
            /* update stats */
            atomic_fetch_add(&(class->class_num_thrd_closing_flag_set), 1);

            done    = TRUE;
            closing = TRUE;
        }
        else if ( ! thrd.opening )
        {
            update_thrd = thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(class->thrd), 
                                                   &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded, update stats and set done */
                atomic_fetch_add(&(class->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            atomic_fetch_add(&(class->class_num_thrd_opening_flag_set), 1);

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            done    = TRUE;
            closing = TRUE;

#endif
        }

    } while ( ! done );



    if ( ! closing )
    {
        /**
         * First thing is we must ensure there are no other modifies, inserts, or deletes
         * other threads are doing in this property list class. We do this by using 
         * atomic_fetch_add() on class->next_version to get the next_version and increment 
         * it. Then class->curr_version is read and curr_version and next_version are 
         * compared. If curr_version is one less than next_version we proceed. However, if 
         * next_version is more than one greater than curr_version, then at least one other 
         * thread is performing an operation on this property list class's LFSLL. We must 
         * enter a loop to sleep and then read curr_version from the class until it's been 
         * updated so curr_version is only one less than the next_version this thread 
         * previously read.
         * 
         * NOTE: this is a temporary solution to simultaneous threads editing the LFSLL and 
         * future iterations of this code will improve how this is handled.
         */
        curr_version = atomic_load(&(class->curr_version));
        next_version = atomic_fetch_add(&(class->next_version), 1);

        assert(curr_version < next_version); 

        while ( curr_version + 1 < next_version )
        {
            atomic_fetch_add(&(class->class_wait_for_curr_version_to_inc), 1);

            sleep(1);

            curr_version = atomic_load(&(class->curr_version));

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            curr_version++;      
#endif
        } /* end while ( curr_version + 1 < next_version ) */

        if ( curr_version > next_version )
        {
            fprintf(stderr, 
                   "\nsimulanious modify, insert, and/or delete was done out of order.");
        }   

        assert(curr_version + 1 ==  next_version);  

        
        /* Now that this thread is good to proceed, create the new property */
        new_prop = H5P__create_prop(name, value, size, TRUE, next_version);

        assert(new_prop);
        assert(new_prop->tag == H5P_MT_PROP_TAG);

        /* update stats */
        atomic_fetch_add(&(class->H5P__create_prop__num_calls), 1);


        /* Finds the position in the class's LFSLL to insert the new property. */

        /** 
         * NOTE: As long as H5P__find_mod_point() works correctly 
         * the do-while can probably be removed.
         */
        done = FALSE;
        do 
        {
            first_prop  = NULL;
            second_prop = NULL;

            /** 
             * The current implementation of H5P__find_mod_point() should either succeed or 
             * trigger an assertion -- thus no need to check return value at present.
             */
            H5P__find_mod_point(class,          /* pointer to the class, NULL if a list */
                                NULL,           /* pointer to the list, NULL if a class */
                                &first_prop,    
                                &second_prop,   
                                &deletes,       /** NOTE: not used in this version of code */
                                &nodes_visited, 
                                &thrd_cols,
                                new_prop,
                                &cmp_result);

            assert(first_prop);
            assert(second_prop);

            assert(first_prop->tag == H5P_MT_PROP_TAG);
            assert(second_prop->tag == H5P_MT_PROP_TAG);

            /* Ensure the returned properties are correct for inserting the new prop */
            if ( first_prop->chksum <= new_prop->chksum )
            {
                if (( second_prop->chksum > new_prop->chksum ) ||
                (( second_prop->chksum == new_prop->chksum ) && ( cmp_result > 0 )) ||
                (( second_prop->chksum == new_prop->chksum ) && ( cmp_result == 0 ) &&
                    ( second_prop->create_version < new_prop->create_version ) ) )
                {  
                    /* prep new_prop to be inserted between first_prop and second_prop */
                    new_prop_next.ptr          = second_prop;
                    new_prop_next.deleted      = FALSE;
                    new_prop_next.dummy_bool_1 = FALSE;
                    new_prop_next.dummy_bool_2 = FALSE;
                    new_prop_next.dummy_bool_3 = FALSE;
                    atomic_store(&(new_prop->next), new_prop_next);

                    /** Attempt to atomically insert new_prop 
                     * 
                     * NOTE: If this fails, another thread modified the LFSLL and we must 
                     * update stats and restart to ensure new_prop is correctly inserted.
                     * However, this shouldn't happen due to other threads attempting to
                     * modify the LFSLL in some way, must wait until the curr_version
                     * is one less than the next_version they, were assigned.
                     */
                    next = atomic_load(&(first_prop->next));

                    assert(next.ptr == second_prop);

                    updated_next.ptr          = new_prop;
                    updated_next.deleted      = FALSE;
                    updated_next.dummy_bool_1 = FALSE;
                    updated_next.dummy_bool_2 = FALSE;
                    updated_next.dummy_bool_3 = FALSE;

                    if ( ! atomic_compare_exchange_strong(&(first_prop->next),
                                                            &next, updated_next) )
                    {
                        /* attempt failed, update stats and try again */
                        atomic_fetch_add(&(class->class_num_insert_prop_cols), 1);
                        
                        continue;
                    }
                    else 
                    {
                        /* attempt succeeded, update stats */
                        atomic_fetch_add(&(class->class_num_insert_prop_success), 1);

                        done = TRUE;
                    }

                } /* end if () */

/** 
 * NOTE: as long as H5P__find_mod_point() works correctly and we don't try 
 * to insert a property that already exists in the LFSLL, this else could
 * probably be removed.
 */
#if 0
                /**
                 * ( second_prop->chksum < new_prop->chksum ) ||
                 * ( second_prop->chksum == new_prop->chksum && cmp_result < 0 ) ||
                 * ( second_prop->chksum == new_prop->chksum && cmp_result == 0 &&
                 *   second_prop->create_version > target_prop->create_version )
                 */
                else 
                {
                    /** NOTE: this should not happen */
                    if ( cmp_result == 0 && 
                        second_prop->create_version == new_prop->create_version )
                    {
                        fprintf(stderr, "\nNew property already exists in property class.");

                        ret_value = -1;
                    }
                    /** NOTE: this also should not happen */
                    else
                    {
                        /** 
                         * H5P__find_mod_point didn't return pointers correctly, throw an
                         * assertion error to end code.
                         */
                        assert(cmp_result >= 0);
                        assert(second_prop->create_version > new_prop->create_version);
                    }
                    
                }
#endif
            } /* end if ( first_prop->chksum <= new_prop->chksum ) */

        } while ( ! done );

        assert(done);
        assert(deletes >= 0);
        assert(nodes_visited >= 0);
        assert(thrd_cols >= 0);

        /* update stats */
        atomic_fetch_add(&(class->class_insert_nodes_visited), nodes_visited);
        atomic_fetch_add(&(class->class_num_insert_prop_cols), thrd_cols);

        /* Update physical and logical length of the LFSLL */
        atomic_fetch_add(&(class->log_pl_len), 1);
        atomic_fetch_add(&(class->phys_pl_len), 1);

        /* Update the class's current version */
        atomic_store(&(class->curr_version), next_version);


        /* Decrement the number of threads that are currently active in the host struct */
        done = FALSE;
        do
        {
            thrd = atomic_load(&(class->thrd));

            update_thrd = thrd;
            update_thrd.count--;

            if ( ! atomic_compare_exchange_strong(&(class->thrd), &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->class_num_thrd_count_update), 1);

                done = TRUE;
            }

        } while ( ! done ); 

    } /* end if ( ! closing ) */

    return(ret_value);

} /* end H5P__insert_prop_class() */



/****************************************************************************************
 * Function:    H5P__delete_prop_class
 *
 * Purpose:     Sets the delete_version of a property in a property list class.
 *
 *              NOTE: Current implementation does not physically or logically delete 
 *              H5P_mt_prop_t structs from the LFSLL of a H5P_mt_class_t. Currently the
 *              delete_version field of the structure is set, and new H5P_mt_class_t
 *              structures derived from this class at the version equal to or greater 
 *              than the delete_version will not copy that property into their LFSLL 
 *              with the other properties.
 * 
 *              This function calls the function H5P__find_mod_point() and passes 
 *              pointers for the class and property being deleted, two double pointers of 
 *              H5P_mt_prop_t that are used to iterate the LFSLL, variables used for 
 *              stats keeping, and a variable for the result of strcmp() if the chksum
 *              values of the properties are the same.
 * 
 *              If the second_prop pointer points to a property that is equal to the 
 *              target_prop, then  it will have it's delete_version set to the class's
 *              next_version and the curr_version of the class will be set to the
 *              next_version as well.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the class, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_class_t in H5Ppkg_mt.h
 *              for more details.
 * 
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t
H5P__delete_prop_class(H5P_mt_class_t *class, H5P_mt_prop_t *target_prop)
{
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    H5P_mt_prop_aptr_t next;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    uint64_t        curr_version    = 0;
    uint64_t        next_version    = 0;
    uint64_t        delete_version  = 0;
    int32_t         deletes         = 0;
    int32_t         nodes_visited   = 0;
    int32_t         thrd_cols       = 0;
    int32_t         cmp_result      = 0;
    hbool_t         done            = FALSE;
    hbool_t         closing         = FALSE;

    herr_t          ret_value = SUCCEED;

    assert(class);
    assert(class->tag == H5P_MT_CLASS_TAG);

    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);
    assert(target_prop->delete_version == 0);


    /* update stats */
    atomic_fetch_add(&(class->H5P__delete_prop_class__num_calls), 1);

    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        thrd = atomic_load(&(class->thrd));

        if ( thrd.closing )
        {
            /* update stats */
            atomic_fetch_add(&(class->class_num_thrd_closing_flag_set), 1);

            done    = TRUE;
            closing = TRUE;
        }
        else if ( ! thrd.opening )
        {
            update_thrd = thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(class->thrd), 
                                                   &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(class->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            atomic_fetch_add(&(class->class_num_thrd_opening_flag_set), 1);

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            done    = TRUE;
            closing = TRUE;
#endif
        }

    } while ( ! done );


    if ( ! closing )
    {
        /**
         * For details on why the following while loop is being used, see the comment above
         * the same while loop in the function H5P__insert_prop_class().
         */
        next_version = atomic_fetch_add(&(class->next_version), 1);
        curr_version = atomic_load(&(class->curr_version));

        assert(curr_version < next_version);

        while ( curr_version + 1 < next_version )
        {
            atomic_fetch_add(&(class->class_wait_for_curr_version_to_inc), 1);

            sleep(1);

            curr_version = atomic_load(&(class->curr_version));

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            curr_version++;      
#endif
        } /* end while ( curr_version + 1 < next_version ) */

        if ( curr_version > next_version )
        {
            fprintf(stderr, 
                   "\nsimulanious modify, insert, and/or delete was done out of order.");
        }

        assert(curr_version + 1 == next_version);


        /* Iterates the LFSLL to find the property to delete */

        /** 
         * NOTE: As long as H5P__find_mod_point() works correctly 
         * the do-while can probably be removed.
         */
        do
        {
            first_prop  = NULL;
            second_prop = NULL;

            /** 
             * The current implementation of H5P__find_mod_point() should either succeed or 
             * trigger an assertion -- thus no need to check return value at present.
             */
            H5P__find_mod_point(class,
                                NULL,
                                &first_prop,
                                &second_prop,
                                &deletes,
                                &nodes_visited,
                                &thrd_cols,
                                target_prop,
                                &cmp_result);

            assert(first_prop);
            assert(second_prop);

            assert(first_prop->tag == H5P_MT_PROP_TAG);
            assert(second_prop->tag == H5P_MT_PROP_TAG);


            if ( first_prop->chksum <= target_prop->chksum )
            {
                /* If true, then we have found target property and can now delete it. */
                if (( second_prop->chksum == target_prop->chksum ) && 
                        ( cmp_result == 0 ) && 
                        ( second_prop->create_version == target_prop->create_version ) )
                {

                    delete_version = atomic_load(&(second_prop->delete_version));

                    assert(delete_version == 0);

                    /* Set the prop's delete_version */
                    atomic_store(&(target_prop->delete_version), next_version);

                    done = TRUE;

                    /* update stats */
                    atomic_fetch_add(&(class->class_num_prop_delete_version_set), 1);

                }
/** 
 * NOTE: as long as H5P__find_mod_point() works correctly and we don't try 
 * to delete a property that doesn't exists in the LFSLL, this else could
 * probably be removed.
 */
#if 0

                /* If any are true, the target property doesn't exist in the LFSLL */
                else if (( second_prop->chksum > target_prop->chksum ) ||
                (( second_prop->chksum == target_prop->chksum ) && ( cmp_result > 0 )) ||
                (( second_prop->chksum == target_prop->chksum ) && ( cmp_result == 0 ) &&
                    ( second_prop->create_version < target_prop->create_version ) ) )
                {
                    /* update stats */
                    atomic_fetch_add(&(class->class_num_delete_prop_nonexistant), 1);

                    fprintf(stderr, "\nTarget property to delete doesn't exist in property class.");

                    done = TRUE;
                }

                /**
                 * ( second_prop->chksum < target_chksum ) || 
                 * ( second_prop->chksum == target_chksum && cmp_result < 0 ) ||
                 * ( second_prop->chksum == target_chksum && cmp_result == 0 &&
                 *   second_prop->create_version > target_prop->create_version )
                 */
                else 
                {
                    /** 
                     * H5P__find_mod_point didn't return pointers correctly,
                     * update stats and try again.
                     */
                    assert(cmp_result >= 0);
                    assert(second_prop->create_version > target_prop->create_version);

                }
#endif

            } /* end if ( first_prop->chksum <= target_prop->chksum ) */


        } while ( ! done );

        assert(done);
        assert(deletes >= 0);
        assert(nodes_visited >= 0);
        assert(thrd_cols >= 0);

        /* update stats */
        atomic_fetch_add(&(class->class_delete_nodes_visited), nodes_visited);
        atomic_fetch_add(&(class->class_num_delete_prop_cols), thrd_cols);

        /* Update the class's current version */
        atomic_store(&(class->curr_version), next_version);

        /* Decrement the number of threads that are currently active in the host struct */
        done = FALSE;
        do
        {
            thrd = atomic_load(&(class->thrd));

            update_thrd = thrd;
            update_thrd.count--;

            if ( ! atomic_compare_exchange_strong(&(class->thrd), &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->class_num_thrd_count_update), 1);

                done = TRUE;
            }

        } while ( ! done );

    } /* end if ( ! closing ) */

    return(ret_value);    

} /* end H5P__delete_prop_class() */



/****************************************************************************************
 * Function:    H5P__find_mod_point
 *
 * Purpose:     Iterates the lock free singly linked list (LFSLL) of a property list 
 *              class (H5P_mt_class_t) or property list (H5P_mt_list_t).
 * 
 *              This function is called when the LFSLL of a class or list is needed to 
 *              be iterated to find the point where a new property needs to be inserted, 
 *              or find the property that needs to be deleted.
 * 
 *              NOTE: H5P__find_mod_point() has H5P_mt_class_t* and H5P_mt_list_t* as 
 *              parameters, when a class is calling this function the list pointer 
 *              paramenter is set to NULL, and vise versa for when a list is calling this
 *              function.
 * 
 *              Pointers are passed into this function, one is for the H5P_mt_class_t or
 *              the H5P_mt_list_t that contains the LFSLL we are iterating. Three 
 *              H5P_mt_prop_t pointers, with the target_prop being the property that will 
 *              be inserted or is being searched for in the LFSLL. The first_ptr_ptr and 
 *              second_ptr_ptr are pointers that will be passed back to the calling 
 *              function with the properties they need. H5P__find_mod_point uses two 
 *              H5P_mt_prop_t*, first_prop and second_prop to iterate the LFSLL, and 
 *              compare the entries to target_prop.  
 * 
 *              If the target_prop equals the second_prop then the search is done. 
 *              If the target_prop falls between first_prop and second_prop then 
 *              target_prop doesn't exist in the list, and for inserts the target_prop 
 *              will be inserted between these two.
 *              When either of these conditions are met first_ptr_ptr and second_ptr_ptr 
 *              are set equal to first_prop and second_prop respectively and passed back
 *              to the calling function. 
 * 
 *              cmp_result is a field used for string compares of the name field of the
 *              H5P_mt_prop_t structs if there is a chksum collision while searching. 
 *              The field cmp_result stores the result from strcmp() and passes that 
 *              returned value back to the calling function. 
 * s
 *              NOTE: for more information on how the LFSLL is sorted see the description
 *              comment for structure H5P_mt_class_t in H5Ppkg_mt.h.               
 * 
 *              The parameters *deletes_ptr, *nodes_visited_ptr, and *thrd_cols_ptr are
 *              for stats collecting.
 *              
 *      
 * Return:      Void
 *
 ****************************************************************************************
 */
void
H5P__find_mod_point(H5P_mt_class_t *class,
                    H5P_mt_list_t  *list,
                    H5P_mt_prop_t **first_ptr_ptr, 
                    H5P_mt_prop_t **second_ptr_ptr, 
                    int32_t        *deletes_ptr, 
                    int32_t        *nodes_visited_ptr, 
                    int32_t        *thrd_cols_ptr, 
                    H5P_mt_prop_t  *target_prop,
                    int32_t        *cmp_result_ptr)
{
    bool               done          = FALSE;
    int32_t            thrd_cols     = 0;
    int32_t            deletes       = 0;
    int32_t            nodes_visited = 0;
    int32_t            cmp_result;
    H5P_mt_prop_t    * first_prop;
    H5P_mt_prop_t    * second_prop;
    H5P_mt_prop_aptr_t next_prop;

    if ( class )
    {
        assert(class);
        assert(class->tag == H5P_MT_CLASS_TAG);
    }
    else if ( list )
    {
        assert(list);
        assert(list->tag == H5P_MT_LIST_TAG);
    }

    assert(first_ptr_ptr);
    assert(NULL == *first_ptr_ptr);
    assert(second_ptr_ptr);
    assert(NULL == *second_ptr_ptr);
    assert(deletes_ptr);
    assert(nodes_visited_ptr);
    assert(thrd_cols_ptr);
    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);
    assert(target_prop->chksum > LLONG_MIN && target_prop->chksum < LLONG_MAX);



    assert(!done);
    
    if ( class )
    {
        /* update stats */
        atomic_fetch_add(&(class->H5P__find_mod_point__num_calls), 1);

        first_prop = class->pl_head;
    }
    else if ( list )
    {
        /* update stats */
        atomic_fetch_add(&(list->H5P__find_mod_point__num_calls), 1);

        first_prop = list->pl_head;
    }

    assert(first_prop->sentinel);
    
    next_prop = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_prop.ptr));
    assert(first_prop != second_prop);

    /* Iterate through the LFSLL of properties */
    do 
    {
        assert(first_prop);
        assert(first_prop->tag == H5P_MT_PROP_TAG);

        assert(second_prop);
        assert(second_prop->tag == H5P_MT_PROP_TAG);

        assert(first_prop->chksum <= target_prop->chksum);

        /* If true then we're done, and can return the current pointers */
        if ( second_prop->chksum > target_prop->chksum )
        {
            done = TRUE;
        }
        /* If the chksums equal we must compare the property names */
        else if ( second_prop->chksum == target_prop->chksum )
        {
            /* update stats */
            if ( class )
            {
                atomic_fetch_add(&(class->class_num_prop_chksum_cols), 1);
            }
            else if ( list )
            {
                atomic_fetch_add(&(list->list_num_prop_chksum_cols), 1);
            }

            assert(first_prop);
            assert(first_prop->tag == H5P_MT_PROP_TAG);

            assert(second_prop);
            assert(second_prop->tag == H5P_MT_PROP_TAG);

            assert(first_prop->chksum <= target_prop->chksum);

            cmp_result = strcmp(second_prop->name, target_prop->name);

            /** 
             * If true then second_prop is greater than target_prop 
             * lexicographically, and the result is the same as if 
             * second_prop->chksum was greater than new_prop->chksum.
             */
            if ( cmp_result > 0 )
            {
                done = TRUE;
            }
            /* If true then compare create_versions */
            else if ( cmp_result == 0 )
            {
                /* update stats */
                if ( class )
                {
                    atomic_fetch_add(&(class->class_num_prop_name_cols), 1);
                }
                else if ( list )
                {
                    atomic_fetch_add(&(list->list_num_prop_name_cols), 1);
                }

                /* If true then we have found the target property */
                if ( second_prop->create_version <= target_prop->create_version )
                {
                    done = TRUE;
                }
            }
                
        } /* end else if ( second_prop->chksum == target_prop->chksum ) */

        /* If !done, update the pointers to iterate the LFSLL */
        if ( ! done )
        {
            next_prop = atomic_load(&(second_prop->next));

            assert(next_prop.ptr);
            assert(next_prop.ptr->tag == H5P_MT_PROP_TAG);

            first_prop = second_prop;
            second_prop = next_prop.ptr;

            nodes_visited++;
        }
    
    } while ( ! done ); 

    assert(done);

    assert(first_prop->chksum <= target_prop->chksum);
    assert(second_prop->chksum >= target_prop->chksum);

    /**
     * Update the pointers passed into the function with the pointers for the 
     * correct positions in the LFSLL, and the stats gathered while iterating.
     */
    *first_ptr_ptr      = first_prop;
    *second_ptr_ptr     = second_prop;
    *thrd_cols_ptr     += thrd_cols;
    *deletes_ptr       += deletes;
    *nodes_visited_ptr += nodes_visited;

    return;

} /* H5P__find_mod_point() */



/****************************************************************************************
 * Function:    H5P__create_list
 *
 * Purpose:     Function to create a new property list (H5P_mt_list_t) derived from a 
 *              property list class (H5P_mt_class_t).
 * 
 *              NOTE: property list classes are referred to as classes and property lists
 *              are referred to as lists.
 *
 *              A multi-thread safe function to create a new list, derived from an 
 *              existing class. Lists create an array of H5P_mt_list_table_entry_t of 
 *              length nprops_inherited which point to the valid properties in the parent
 *              class's LFSLL.         
 * 
 * Details:     NOTE: for more information of the H5P_mt_list_t structure or specific
 *              fields, check the detailed comment for the struture in H5Ppkg_mt.h 
 * 
 *              To ensure multi-thread safety the first step is to check the parent 
 *              class's thrd field to ensure the parent class is not in the process 
 *              opening or closing. If opening is TRUE the function loops checking again. 
 *              If opening, the class will only be briefly visible to other threads 
 *              before completing the opening process, so this thread should see that 
 *              opening's been set to FALSE without waiting long (stats are collected so 
 *              if this proves incorrect, it can be found quickly and fixed). If closing
 *              is true, an error is thrown. If neither are true, the count field in the
 *              parent class's thrd field is incremented to show another thread is 
 *              accessing the structure. 
 *              
 * 
 *              Next, memory is allocated for the sentinel nodes for the lists LFSLL,
 *              then memory is allocated for the new H5P_mt_list_t and the fields are 
 *              initialized, and the list's pl_head is set to the first sentinel node. 
 *              Then the function H5P__init_lkup_tbl() is called where the lkup_tbl is
 *              created with entries that point to the parent class's valid properties. 
 *              NOTE: for more information of the lkup_tbl see the description comment 
 *              for H5P__init_lkup_tbl() or for the structures H5P_mt_list_table_entry_t
 *              and H5P_mt_list_prop_ref_t in H5Ppkg_mt.h 
 *              
 *
 * Return:      Success: Returns a pointer to the new H5P_mt_list_t struct.
 * 
 *              Failure: NULL
 *
 ****************************************************************************************
 */
H5P_mt_list_t *
H5P__mt_create_list(H5P_mt_class_t *parent, uint64_t parent_version)
{
    H5P_mt_list_t              * new_list;
    H5P_mt_prop_t              * pos_sentinel;
    H5P_mt_prop_aptr_t           pos_next;
    H5P_mt_prop_value_t          pos_value;
    H5P_mt_prop_t              * neg_sentinel;
    H5P_mt_prop_aptr_t           neg_next;
    H5P_mt_prop_value_t          neg_value;
    hid_t                        parent_id;
    H5P_mt_class_ref_counts_t    parent_ref;
    H5P_mt_class_ref_counts_t    update_ref;
    H5P_mt_active_thread_count_t parent_thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_active_thread_count_t list_thrd;
    H5P_mt_list_sptr_t           fl_next;
    H5P_mt_list_sptr_t           new_fl_next;
    hbool_t                      done = FALSE;
    hbool_t                      retry = FALSE;
    hbool_t                      closing = FALSE;

    H5P_mt_list_t * ret_value = NULL;

    assert(parent);
    assert(parent->tag == H5P_MT_CLASS_TAG);


    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        parent_thrd = atomic_load(&(parent->thrd));

        if ( parent_thrd.closing )
        {
            /* update stats */
            atomic_fetch_add(&(parent->class_num_thrd_closing_flag_set), 1);

            fprintf(stderr, "CLOSING flag is set when it shouldn't be.\n");

            done    = TRUE;
            closing = TRUE;
        }
        else if ( ! parent_thrd.opening )
        {
            update_thrd = parent_thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                   &parent_thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            atomic_fetch_add(&(parent->class_num_thrd_opening_flag_set), 1);

#if H5P_MT_SINGLE_THREAD_TESTING

            /** 
             * This is for testing that this MT safty net gets trigged while running
             * tests in single thread, to prevent an infinite loop.
             */
            done    = TRUE;
            closing = TRUE;
#endif
        }


    } while ( ! done );



    if ( ! closing )
    {
        /* Create the sentinel nodes for the LFSLL of properties */

        /* Allocating and initializing the positive sentinel node of the LFSLL */
        pos_sentinel = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
        if ( ! pos_sentinel )
            fprintf(stderr, "\nNew property allocation failed."); 

        /* Initalize property fields */
        pos_sentinel->tag = H5P_MT_PROP_TAG;

        pos_next.ptr          = NULL;
        pos_next.deleted      = FALSE;
        pos_next.dummy_bool_1 = FALSE;
        pos_next.dummy_bool_2 = FALSE;
        pos_next.dummy_bool_3 = FALSE;
        atomic_store(&(pos_sentinel->next), pos_next);

        pos_sentinel->sentinel      = TRUE;
        pos_sentinel->in_prop_class = FALSE;

        /* ref_count is only used if the property is in a property class. */
        atomic_store(&(pos_sentinel->ref_count), 0);

        pos_sentinel->chksum = LLONG_MAX;

        pos_sentinel->name = (char *)"pos_sentinel";

        pos_value.ptr  = NULL;
        pos_value.size = 0;

        atomic_store(&(pos_sentinel->value), pos_value);
        
        atomic_store(&(pos_sentinel->create_version), 1);
        atomic_store(&(pos_sentinel->delete_version), 0); 


        /* Allocating and initializing the negative sentinel node of the LFSLL */
        neg_sentinel = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
        if ( ! neg_sentinel )
            fprintf(stderr, "\nNew property allocation failed."); 

        /* Initalize property fields */
        neg_sentinel->tag = H5P_MT_PROP_TAG;

        neg_next.ptr          = pos_sentinel;
        neg_next.deleted      = FALSE;
        neg_next.dummy_bool_1 = FALSE;
        neg_next.dummy_bool_2 = FALSE;
        neg_next.dummy_bool_3 = FALSE;
        atomic_store(&(neg_sentinel->next), neg_next);

        neg_sentinel->sentinel      = TRUE;
        neg_sentinel->in_prop_class = FALSE;

        /* ref_count is only used if the property is in a property class. */
        atomic_store(&(neg_sentinel->ref_count), 0);

        neg_sentinel->chksum = LLONG_MIN;

        neg_sentinel->name = (char *)"neg_sentinel";

        neg_value.ptr  = NULL;
        neg_value.size = 0;

        atomic_store(&(neg_sentinel->value), neg_value);
        
        atomic_store(&(neg_sentinel->create_version), 1);
        atomic_store(&(neg_sentinel->delete_version), 0); /* Set to 0 because it's not deleted */



        /* Allocate memory for the new property list */
        new_list = (H5P_mt_list_t *)malloc(sizeof(H5P_mt_list_t));
        if ( ! new_list )
        {
            fprintf(stderr, "\nNew list allocation failed.\n");
            return NULL;
        }

        /* Initialize list fields */
        new_list->tag = H5P_MT_LIST_TAG;

        parent_id = atomic_load(&(parent->id));    
        new_list->pclass_id = parent_id;

        new_list->pclass_ptr = parent;
        new_list->pclass_version = parent_version;

        atomic_store(&(new_list->plist_id), H5I_INVALID_HID);
        atomic_store(&(new_list->curr_version), 1);
        atomic_store(&(new_list->next_version), 2);

        new_list->lkup_tbl = NULL;

        new_list->nprops_inherited = 0;
        atomic_store(&(new_list->nprops_added), 0);
        atomic_store(&(new_list->nprops), 0);

        new_list->pl_head = neg_sentinel;
        
        atomic_store(&(new_list->log_pl_len),  0);
        atomic_store(&(new_list->phys_pl_len), 2);

        /**
         * NOTE: will need to set this up correctly when I add callbacks.
         */
        atomic_store(&(new_list->class_init), TRUE);

        list_thrd.count   = 1;
        list_thrd.opening = TRUE;
        list_thrd.closing = FALSE;
        atomic_store(&(new_list->thrd), list_thrd);

        fl_next.ptr = NULL;
        fl_next.sn  = 0;
        atomic_store(&(new_list->fl_next), fl_next);

        
        H5P__init_lkup_tbl(parent, parent_version, new_list);


        atomic_store(&(new_list->thrd), list_thrd); 

        /* Initialize stats */
        atomic_init(&(new_list->list_lkup_tbl_entry_modified),      0ULL);
        atomic_init(&(new_list->list_num_prop_freed),               0ULL);
        atomic_init(&(new_list->list_num_props_modified),           0ULL);
        atomic_init(&(new_list->list_wait_for_curr_version_to_inc), 0ULL);
        
        /* H5P_mt_active_thread_count_t stats */
        atomic_init(&(new_list->list_num_thrd_count_update_cols), 0ULL);
        atomic_init(&(new_list->list_num_thrd_count_update),      0ULL);
        atomic_init(&(new_list->list_num_thrd_closing_flag_set),  0ULL);
        atomic_init(&(new_list->list_num_thrd_opening_flag_set),  0ULL);

        /* H5P__insert_prop_list() function stats */
        atomic_init(&(new_list->list_num_insert_prop_cols),        0ULL);
        atomic_init(&(new_list->list_num_insert_prop_success),     0ULL);
        atomic_init(&(new_list->list_insert_nodes_visited),        0ULL);
        atomic_init(&(new_list->H5P__insert_prop_list__num_calls), 0ULL);

        /* H5P__find_mod_point() function stats */
        atomic_init(&(new_list->list_num_prop_chksum_cols),      0ULL);
        atomic_init(&(new_list->list_num_prop_name_cols),        0ULL);
        atomic_init(&(new_list->H5P__find_mod_point__num_calls), 0ULL);

        /* H5P__create_prop() function stats */
        atomic_init(&(new_list->H5P__create_prop__num_calls), 0ULL);


        /* Update Parent class derived counts */
        done = FALSE;

        do
        {
            parent_ref = atomic_load(&(parent->ref_count));
            update_ref = parent_ref;
            update_ref.pl++;

            /* Attempt to atomically update the parent class's count of derived classes */
            if ( ! atomic_compare_exchange_strong(&(parent->ref_count), &parent_ref, update_ref) )
            {
                /* update stats */
                atomic_fetch_add(&(parent->class_num_ref_count_cols), 1);
            }
            else /* Attempt was successful */
            {
                /* update stats */
                atomic_fetch_add(&(parent->class_num_ref_count_update), 1);

                done = TRUE;

            }

        } while ( ! done );

        assert(done);


        /**
         * Update the thrd structures of the new list and parent class.
         * 
         * NOTE: This update is done with an atomic_compare_exchage_strong() as a safety
         * measure, even through we have the earlier safety measure of other threads
         * waiting for the opening thread to be set to FALSE before incrementing the
         * count.
         * 
         * If the stats and testing show this is unnecessary it can be changed to a more 
         * simple atomic_store().
         */
        done = FALSE;

        do
        {
        
            list_thrd = atomic_load(&(new_list->thrd));
            
            update_thrd = list_thrd;
            update_thrd.count--;
            update_thrd.opening = FALSE;

            if ( ! atomic_compare_exchange_strong(&(new_list->thrd), 
                                                &list_thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(new_list->list_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeed, update stats and continue */
                atomic_fetch_add(&(new_list->list_num_thrd_count_update), 1);
                
                done = TRUE;
            }

        } while ( ! done );


        done = FALSE;
        do
        {
            parent_thrd = atomic_load(&(parent->thrd));
            update_thrd = parent_thrd;
            update_thrd.count--;

            if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                    &parent_thrd, update_thrd) )
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeed, update stats and continue */
                atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        } while ( ! done );

        assert(done);


    } /* end if ( ! closing )*/

    ret_value = new_list;

    return(ret_value);

} /* H5P__mt_create_list() */



/****************************************************************************************
 * Function:    H5P__init_lkup_tbl
 *
 * Purpose:     Allocates and initializes an array of H5P_mt_list_table_entry_t structs
 *              for a list, called lkup_tbl. 
 * 
 *              The function first iterates through the LFSLL of the list's parent class,
 *              and counts the number of valid properties. 
 *              Two things make a property invalid:
 *                  1) any property that has a create_version greater than the parent 
 *                     class's version from which the new class is derived.
 *                  2) any property that has a delete_version less than or equal to the
 *                     parent class's version from which the new class is derived.
 *              
 *              With the number of valid properties for the new list, the lkup_tbl is
 *              allocated. The parent class's LFSLL is iterated again, and the lkup_tbl's
 *              entries are setup to the valid properties. The chksum and *name fields in
 *              the lkup_tbl are copied from the class's property. The base.ptr is set
 *              to point to the property in the parent class's LFSLL, and base.ver is set
 *              to the intial version of the list (which is 1). base_delete_version is 
 *              initialized to 0, and curr.ptr and curr.ver is initialized to NULL and 0
 *              respectively. Lastly, the parent's property increments it's ref_count, to
 *              count the new list having a pointer to that property. 
 * 
 *
 * Return:      void   
 *
 ****************************************************************************************
 */
void
H5P__init_lkup_tbl(H5P_mt_class_t *parent, uint64_t version, H5P_mt_list_t *list)
{
    H5P_mt_list_table_entry_t * entry;
    H5P_mt_list_prop_ref_t      base;
    H5P_mt_list_prop_ref_t      curr;
    uint64_t                    base_delete_version = 0;
    uint64_t                    i                   = 0;
    H5P_mt_prop_t             * first_prop;
    H5P_mt_prop_t             * second_prop;
    H5P_mt_prop_aptr_t          next_ptr;
    uint64_t                    prop_ref_count;
    uint64_t                    update_ref_count;
    uint64_t                    prop_create_ver;
    uint64_t                    prop_delete_ver;
    uint32_t                    cmp_result;
    uint32_t                    nprops;
    hbool_t                     done = FALSE;


    first_prop  = atomic_load(&(parent->pl_head));
    next_ptr    = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);


    /* Iterate the parent class's LFSLL to count the valid properties */
    while ( ! second_prop->sentinel ) 
    {
        prop_create_ver = atomic_load(&(second_prop->create_version));
        prop_delete_ver = atomic_load(&(second_prop->delete_version));

        if ( ( prop_create_ver <= version ) && 
           ( ( prop_delete_ver == 0 ) || ( prop_delete_ver >= version ) ) )
        {
            if ( first_prop->chksum == second_prop->chksum )
            {
                cmp_result = strcmp(first_prop->name, second_prop->name);

                if ( cmp_result == 0 )
                {
                    assert(first_prop->create_version > second_prop->create_version);
                }
                else
                {
                    fprintf(stderr, "\nDifferent props have same chksum.\n");
                }
            }
            else
            {
                list->nprops_inherited++;
            }

        }

        /* iterate LFSLL */
        first_prop  = second_prop;
        next_ptr    = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));

    } /* end while ( ! second_prop->sentinel ) */


    /* Allocate the lkup_tbl array */

    assert(list->nprops_inherited > 0);

    nprops = list->nprops_inherited;

    atomic_store(&(list->nprops), nprops);

    list->lkup_tbl = (H5P_mt_list_table_entry_t *)malloc(list->nprops_inherited * 
                                                    sizeof(H5P_mt_list_table_entry_t));
    if ( ! list->lkup_tbl )
    {
        fprintf(stderr, "\nLkup_tbl allocation failed.\n");
    }

    first_prop  = atomic_load(&(parent->pl_head));
    next_ptr    = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_ptr.ptr));

    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);
    assert(first_prop->sentinel);

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);

    /* Set up each lkup_tbl entry to the valid properties in the parent's LFSLL */
    while ( ! second_prop->sentinel )
    {
        prop_create_ver = atomic_load(&(second_prop->create_version));
        prop_delete_ver = atomic_load(&(second_prop->delete_version));

        if ( ( prop_create_ver <= version ) && 
           ( ( prop_delete_ver == 0 ) || ( prop_delete_ver >= version ) ) )
        {
            if ( first_prop->chksum == second_prop->chksum )
            {
                cmp_result = strcmp(first_prop->name, second_prop->name);

                if ( cmp_result == 0 )
                {
                    assert(first_prop->create_version > second_prop->create_version);
                }
                else
                {
                    fprintf(stderr, "\nDifferent props have same chksum.\n");
                }
            }
            else /* first_prop->chksum != second_prop->chksum */
            {
                entry = &list->lkup_tbl[i];

                entry->chksum = second_prop->chksum;
                entry->name   = second_prop->name;
                
                base.ptr = atomic_load(&(second_prop));
                base.ver = 1;
                atomic_store(&(entry->base), base);

                do
                {
                    done = FALSE;

                    prop_ref_count = atomic_load(&(second_prop->ref_count));
                    update_ref_count = prop_ref_count;
                    update_ref_count++;

                    if ( ! atomic_compare_exchange_strong(&(second_prop->ref_count),
                                                        &prop_ref_count, update_ref_count))
                    {
                        /* update stats */
                        atomic_fetch_add(&(parent->class_prop_ref_count_cols), 1);
                    }
                    else
                    {
                        /* update stats */
                        atomic_fetch_add(&(parent->class_prop_ref_count_update), 1);

                        done = TRUE;
                    }


                } while ( ! done );
                

                atomic_store(&(entry->base_delete_version), base_delete_version);

                curr.ptr = NULL;
                curr.ver = 0;
                atomic_store(&(entry->curr), curr);

                i++;
                assert(i <= list->nprops_inherited);

            } /* end else ( first_prop->chksum != second_prop->chksum )*/

        } /* end if () */

        /* increment LFSLL */
        first_prop  = second_prop;
        next_ptr    = atomic_load(&(first_prop->next));
        second_prop = atomic_load(&(next_ptr.ptr));
    
    } /* end while ( ! second_prop->sentinel ) */

} /* H5P__init_lkup_tbl() */



/****************************************************************************************
 * Function:    H5P__insert_prop_list
 *
 * Purpose:     Inserts a new property (H5P_mt_prop_t struct) into the LFSLL of a 
 *              property list list (H5P_mt_list_t).
 *
 *              NOTE: When 'modifying' a property is this multithread safe version of 
 *              H5P, a new property must be created with a new create_version. Because
 *              of this, when a entirely new property is created, or a new property is
 *              created for the 'modified' property, this function is called to handle
 *              either case.
 * 
 *              This function firstly calls H5P__create_prop() to create the new
 *              H5P_mt_prop_t struct to store the property info. Secondly, this function 
 *              passes pointers to the void function H5P__find_mod_point() which passes 
 *              back the pointers that have been updated to the two properties in the 
 *              LFSLL where the new property will be inserted. Pointers to values needed 
 *              for stats keeping and a value used for comparing the name field of the 
 *              H5P_mt_prop_t structs is passed for if the properties had the same chksum
 *              field are also passed to H5P__find_mod_point(). 
 * 
 *              If the new_prop falls between first_prop and second_prop then it is 
 *              atomically inserted between them. 
 * 
 *              Lists have an extra step here, that classes do not. In case this is a 
 *              'modification' to an existing property, the list must iterate it's 
 *              lkup_tbl and if there is another version of the new property being 
 *              inserted in the lkup_tbl, it must update the curr.ptr to point to the
 *              new property, and update curr.ver to the version of the new property. 
 * 
 *              NOTE: See the comment description above struct H5P_mt_prop_t in 
 *              H5Ppkg_mt.h for details on how the LFSLL are sorted.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the list, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_list_t in H5Ppkg_mt.h
 *              for more details.
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t
H5P__insert_prop_list(H5P_mt_list_t *list, const char *name, void *value, size_t size)
{
    H5P_mt_prop_t    * new_prop;
    H5P_mt_prop_t    * first_prop;
    H5P_mt_prop_t    * second_prop;
    H5P_mt_prop_aptr_t next;
    H5P_mt_prop_aptr_t new_prop_next;
    H5P_mt_prop_aptr_t updated_next;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_list_table_entry_t  * entry;
    H5P_mt_list_prop_ref_t       curr;
    H5P_mt_list_prop_ref_t       new_curr;
    uint64_t           curr_version  = 0;
    uint64_t           next_version  = 0;
    int32_t            deletes       = 0;
    int32_t            nodes_visited = 0;
    int32_t            thrd_cols     = 0;
    int32_t            cmp_result    = 0;
    hbool_t            done          = FALSE;
    hbool_t            closing       = FALSE;

    herr_t             ret_value     = SUCCEED;

    assert(list);
    assert(list->tag == H5P_MT_LIST_TAG);

    assert(name);
    assert((size > 0 && value != NULL) || (size == 0));

    /* update stats */
    atomic_fetch_add(&(list->H5P__insert_prop_list__num_calls), 1);

    /* Ensure the list isn't opening or closing and increment thread count */
    do 
    {
        thrd = atomic_load(&(list->thrd));

        if ( thrd.closing )
        {
            /* update stats */
            atomic_fetch_add(&(list->list_num_thrd_closing_flag_set), 1);

            done    = TRUE;
            closing = TRUE;
        }
        else if ( ! thrd.opening )
        {
            update_thrd = thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(list->thrd), 
                                                   &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(list->list_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(list->list_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            atomic_fetch_add(&(list->list_num_thrd_opening_flag_set), 1);

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            done    = TRUE;
            closing = TRUE;

#endif
        }

    } while ( ! done );


    if ( ! closing )
    {
        /**
         * First thing is we must ensure there are no other modifies, inserts, or deletes
         * other threads are doing in this property list. We do this by using 
         * atomic_fetch_add() on list->next_version to get the next_version and increment 
         * it. Then list->curr_version is read and curr_version and next_version are 
         * compared. If curr_version is one less than next_version we proceed. However, if 
         * next_version is more than one greater than curr_version, then at least one other 
         * thread is performing an operation on this property list's LFSLL. We must 
         * enter a loop to sleep and then read curr_version from the list until it's been 
         * updated so curr_version is only one less than the next_version this thread 
         * previously read.
         * 
         * NOTE: this is a temporary solution to simultaneous threads editing the LFSLL and 
         * future iterations of this code will improve how this is handled.
         */
        curr_version = atomic_load(&(list->curr_version));
        next_version = atomic_fetch_add(&(list->next_version), 1);

        assert(curr_version < next_version); 

        while ( curr_version + 1 < next_version )
        {
            atomic_fetch_add(&(list->list_wait_for_curr_version_to_inc), 1);

            sleep(1);

            curr_version = atomic_load(&(list->curr_version));

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            curr_version++;      
#endif
        } /* end while ( curr_version + 1 < next_version ) */

        if ( curr_version > next_version )
        {
            fprintf(stderr, 
                   "\nsimulanious modify, insert, and/or delete was done out of order.");
        }   

        assert(curr_version + 1 ==  next_version);


        /* Now that this thread is good to proceed, create the new property first */
        new_prop = H5P__create_prop(name, value, size, TRUE, next_version);

        assert(new_prop);
        assert(new_prop->tag == H5P_MT_PROP_TAG);

        /* update stats */
        atomic_fetch_add(&(list->H5P__create_prop__num_calls), 1);


        /* Finds the position in the class's LFSLL to insert the new property. */

        /** 
         * NOTE: As long as H5P__find_mod_point() works correctly 
         * the do-while can probably be removed.
         */
        done = FALSE;
        do 
        {
            first_prop  = NULL;
            second_prop = NULL;

            /** 
             * The current implementation of H5P__find_mod_point() should either succeed or 
             * trigger an assertion -- thus no need to check return value at present.
             */
            H5P__find_mod_point(NULL,
                                list,         
                                &first_prop,     
                                &second_prop,
                                &deletes,
                                &nodes_visited,
                                &thrd_cols,
                                new_prop,
                                &cmp_result);

            assert(first_prop);
            assert(second_prop);

            assert(first_prop->tag == H5P_MT_PROP_TAG);
            assert(second_prop->tag == H5P_MT_PROP_TAG);

            
            if ( first_prop->chksum <= new_prop->chksum )
            {
                if (( second_prop->chksum > new_prop->chksum ) ||
                (( second_prop->chksum == new_prop->chksum ) && ( cmp_result > 0 )) ||
                (( second_prop->chksum == new_prop->chksum ) && ( cmp_result == 0 ) &&
                    ( second_prop->create_version < new_prop->create_version ) ) )
                {  
                    /* prep new_prop to be inserted between first_prop and second_prop */
                    new_prop_next.ptr          = second_prop;
                    new_prop_next.deleted      = FALSE;
                    new_prop_next.dummy_bool_1 = FALSE;
                    new_prop_next.dummy_bool_2 = FALSE;
                    new_prop_next.dummy_bool_3 = FALSE;
                    atomic_store(&(new_prop->next), new_prop_next);

                    /** Attempt to atomically insert new_prop 
                     * 
                     * NOTE: If this fails, another thread modified the LFSLL and we must 
                     * update stats and restart to ensure new_prop is correctly inserted.
                     */
                    next = atomic_load(&(first_prop->next));

                    assert(next.ptr == second_prop);

                    updated_next.ptr          = new_prop;
                    updated_next.deleted      = FALSE;
                    updated_next.dummy_bool_1 = FALSE;
                    updated_next.dummy_bool_2 = FALSE;
                    updated_next.dummy_bool_3 = FALSE;

                    if ( ! atomic_compare_exchange_strong(&(first_prop->next),
                                                            &next, updated_next) )
                    {
                        /* update stats */
                        atomic_fetch_add(&(list->list_num_insert_prop_cols), 1);
                        
                        continue;
                    }
                    else /* The attempt was successful update stats mark done */
                    {
                        /* update stats */
                        atomic_fetch_add(&(list->list_num_insert_prop_success), 1);

                        done = TRUE;
                    }

                } /* end if () */

            } /* end if ( first_prop->chksum <= new_prop->chksum ) */


        } while ( ! done );

        /* Check if the new prop is a 'modification' to a prop in the lkup_tbl */


        for ( uint32_t i = 0; i < list->nprops_inherited; i++ )
        {
            entry = &list->lkup_tbl[i];

            /* Checking if the table entry has the same chksum */
            if ( new_prop->chksum == entry->chksum )
            {
                
                /* Ensure the properties are in fact the same by checking their names */
                cmp_result = strcmp(entry->name, new_prop->name);

                if ( cmp_result == 0 )
                {
                    done = FALSE;

                    do
                    {
                        curr = atomic_load(&(entry->curr));

                        new_curr.ptr = new_prop;
                        new_curr.ver = next_version;

                        if ( ! atomic_compare_exchange_strong(&(entry->curr), 
                                                              &curr, new_curr))
                        {
                            /* attempt failed, update stats and try again */
                            atomic_fetch_add(&(list->list_update_tbl_entry_curr_cols), 1);
                        }
                        else
                        {
                            /* attempt succeeded, update stats and continue */
                            atomic_fetch_add(&(list->list_update_tbl_entry_curr_success), 1);

                            done = FALSE;
                        }

                    } while ( ! done );


                    assert(done);
                    
                    
                } /* end if ( target_prop->name == entry->name) */

            } /* end if ( target_prop->chksum == entry->chksum ) */

        } /* end for ( uint32_t i = 0; i < list->nprops_inherited; i++ ) */



        assert(deletes >= 0);
        assert(nodes_visited >= 0);
        assert(thrd_cols >= 0);

        /* update stats */
        atomic_fetch_add(&(list->list_insert_nodes_visited), nodes_visited);
        atomic_fetch_add(&(list->list_num_insert_prop_cols), thrd_cols);

        /* inc physical and logical length of the LFSLL, and nprops_added and nprops */
        atomic_fetch_add(&(list->log_pl_len),   1);
        atomic_fetch_add(&(list->phys_pl_len),  1);
        atomic_fetch_add(&(list->nprops_added), 1);
        atomic_fetch_add(&(list->nprops),       1);

        /* Update curr_version */
        atomic_store(&(list->curr_version), next_version);

        /* Update the list's current version */
        atomic_store(&(list->curr_version), next_version);

        /* Decrement the number of threads that are currently active in the host struct */
        thrd = atomic_load(&(list->thrd));

        update_thrd = thrd;
        update_thrd.count--;

        if ( ! atomic_compare_exchange_strong(&(list->thrd), &thrd, update_thrd))
        {
            /* update stats */
            atomic_fetch_add(&(list->list_num_thrd_count_update_cols), 1);
        }
        else
        {
            /* update stats */
            atomic_fetch_add(&(list->list_num_thrd_count_update), 1);
        }

    } /* end if ( ! closing ) */

    return(ret_value);


} /* H5P__insert_prop_list() */



/****************************************************************************************
 * Function:    H5P__delete_prop_list
 *
 * Purpose:     Sets the delete_version of a property in a property list.
 *
 *              NOTE: Current implementation does not physically or logically delete 
 *              H5P_mt_prop_t structs from the LFSLL of a H5P_mt_list_t. Currently the
 *              delete_version field of the structure is set.
 *              
 *              H5P__delete_prop_list() first iterates the lkup_tbl to find the property
 *              to delete. If the property is in the lkup_tbl we must check if curr.ptr 
 *              is NULL. If it is then we set the base_delete_version and are done. If 
 *              curr.ptr is not NULL we must check curr.ver. If it is a valid version
 *              then we go to that property and set it's delete_version.
 * 
 *              If the property, or at least not a valid version of the property, to 
 *              delete is not in the lkup_tbl then, this function calls the function 
 *              H5P__find_mod_point() and passes pointers for the class and property 
 *              being deleted, two double pointers of H5P_mt_prop_t that are used to 
 *              iterate the LFSLL, variables used for stats keeping, and a variable for 
 *              the result of strcmp() if the chksum values of the properties are the 
 *              same.
 * 
 *              If the second_prop pointer points to a property that is equal to the 
 *              target_prop, then  it will have it's delete_version set to the class's
 *              next_version and the curr_version of the class will be set to the
 *              next_version as well.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the class, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_class_t in H5Ppkg_mt.h
 *              for more details.
 * 
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t
H5P__delete_prop_list(H5P_mt_list_t *list, H5P_mt_prop_t *target_prop)
{
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    H5P_mt_prop_aptr_t next;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_list_table_entry_t  * entry;
    H5P_mt_list_prop_ref_t       curr;
    uint64_t        curr_version    = 0;
    uint64_t        next_version    = 0;
    uint64_t        delete_version  = 0;
    int32_t         deletes         = 0;
    int32_t         nodes_visited   = 0;
    int32_t         thrd_cols       = 0;
    int32_t         cmp_result      = 0;
    hbool_t         done            = FALSE;
    hbool_t         closing         = FALSE;

    herr_t          ret_value = SUCCEED;

    assert(list);
    assert(list->tag == H5P_MT_LIST_TAG);

    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);


    /* update stats */
    atomic_fetch_add(&(list->H5P__delete_prop_list__num_calls), 1);

    /* Ensure the list isn't opening or closing and increment thread count */
    do 
    {
        thrd = atomic_load(&(list->thrd));

        if ( thrd.closing )
        {
            /* update stats */
            atomic_fetch_add(&(list->list_num_thrd_closing_flag_set), 1);

            done    = TRUE;
            closing = TRUE;
        }
        else if ( ! thrd.opening )
        {
            update_thrd = thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(list->thrd), 
                                                   &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(list->list_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(list->list_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            atomic_fetch_add(&(list->list_num_thrd_opening_flag_set), 1);

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            done    = TRUE;
            closing = TRUE;
#endif
        }

    } while ( ! done );


    if ( ! closing )
    {
        done = FALSE;

        /**
         * For details on why the following while loop is being used, see the comment above
         * the same while loop in the function H5P__insert_prop_class().
         */
        next_version = atomic_fetch_add(&(list->next_version), 1);
        curr_version = atomic_load(&(list->curr_version));

        assert(curr_version < next_version);

        assert( (target_prop->delete_version == 0) || 
                (target_prop->delete_version > list->pclass_version) );

        while ( curr_version + 1 < next_version )
        {
            atomic_fetch_add(&(list->list_wait_for_curr_version_to_inc), 1);

            sleep(1);

            curr_version = atomic_load(&(list->curr_version));

/** 
 * This is for testing that this MT safty net gets trigged while running
 * tests in single thread, to prevent an infinite loop.
 */
#if H5P_MT_SINGLE_THREAD_TESTING

            curr_version++;      
#endif
        } /* end while ( curr_version + 1 < next_version ) */

        if ( curr_version > next_version )
        {
            fprintf(stderr, 
                   "\nsimulanious modify, insert, and/or delete was done out of order.");
        }

        assert(curr_version + 1 == next_version);


        /* Check the lkup_tbl to see if target_prop is there */

        for ( uint32_t i = 0; i < list->nprops_inherited; i++ )
        {
            entry = &list->lkup_tbl[i];

            /* Checking if the table entry has the same chksum */
            if ( target_prop->chksum == entry->chksum )
            {
                
                /* Ensure the properties are in fact the same by checking their names */
                cmp_result = strcmp(entry->name, target_prop->name);

                if ( cmp_result == 0 )
                {
                    curr = atomic_load(&(entry->curr));

                    /**
                     * If curr.ptr is not NULL, then the property has been modified, and
                     * there is a more recent version in the LFSLL of the property list.
                     */
                    if ( curr.ptr )
                    {
                        /**
                         * If the curr.ver is less than target_prop's version we set 
                         * that properties delete_version to the next_version and mark
                         * done.  
                         */
                        if ( curr.ver < next_version )
                        {
                            first_prop = curr.ptr;

                            delete_version = atomic_load(&(first_prop->delete_version));

                            assert(delete_version == 0);

                            atomic_store(&(first_prop->delete_version), next_version);

                            /* update stats */
                            atomic_fetch_add(&(list->list_num_prop_delete_version_set), 1);

                            done = TRUE;
                        }
                        /**
                         * Else if curr.ver is more current than target_prop's version,
                         * we must break out of the for loop and move into the next while
                         * loop to iterate the LFSLL to find the property with the 
                         * version we're looking for.
                         * 
                         * NOTE: at the moment this shouldn't happen because only one 
                         * thread can modify a list's LFSLL at a time due to the check of
                         * the list's curr_version being one less than the next_version 
                         * they're assigned. However, since this is only setting the 
                         * delete_version and not logically or physically deleting the
                         * property. Future versions will probably not require only one
                         * thread for setting the delete_version but, the actual deleting 
                         * of the property will require that.
                         */
                        else
                        {
                            /* update stats */
                            atomic_fetch_add(&(list->list_num_delete_older_version_than_curr), 1);

                            break;
                        }
                    }
                    /**
                     * If curr.ptr is NULL, then this property has not been modified in
                     * this list, and the most recent version still points to it's parent
                     * class's property.
                     */
                    else
                    {
                        delete_version = atomic_load(&(entry->base_delete_version));

                        assert(delete_version == 0);

                        atomic_store(&(entry->base_delete_version), next_version);

                        done = TRUE;

                        /* update stats */
                        atomic_fetch_add(&(list->list_num_base_delete_version_set), 1);
                    }

                } /* end if ( target_prop->name == entry->name) */

            } /* end if ( target_prop->chksum == entry->chksum ) */

        } /* end for ( uint32_t i = 0; i < list->nprops_inherited; i++ ) */


        /** 
         * If done is FALSE, then the target_prop at the correct version, was not in the
         * lkup_tbl, and we must iterate the LFSLL to find it.
         * 
         * NOTE: As long as H5P__find_mod_point() works correctly 
         * the while can probably be turned to an if statment. 
         */
        while ( ! done )
        {
            first_prop  = NULL;
            second_prop = NULL;

            /** 
             * The current implementation of H5P__find_mod_point() should either succeed or 
             * trigger an assertion -- thus no need to check return value at present.
             */
            H5P__find_mod_point(NULL,
                                list,
                                &first_prop,
                                &second_prop,
                                &deletes,
                                &nodes_visited,
                                &thrd_cols,
                                target_prop,
                                &cmp_result);

            assert(first_prop);
            assert(second_prop);

            assert(first_prop->tag == H5P_MT_PROP_TAG);
            assert(second_prop->tag == H5P_MT_PROP_TAG);


            if ( first_prop->chksum <= target_prop->chksum )
            {
                /* If true, then we have found target property and can now delete it. */
                if (( second_prop->chksum == target_prop->chksum ) && 
                        ( cmp_result == 0 ) && 
                        ( second_prop->create_version == target_prop->create_version ) )
                {

                    delete_version = atomic_load(&(second_prop->delete_version));

                    assert(delete_version == 0);

                    /* Set the prop's delete_version */
                    atomic_store(&(target_prop->delete_version), next_version);

                    done = TRUE;

                    /* update stats */
                    atomic_fetch_add(&(list->list_num_prop_delete_version_set), 1);

                }
                else
                {
                    fprintf(stderr, "\nWrong prop pointers passed to delete_prop_list\n");
                } 
            
            } /* end if ( first_prop->chksum <= target_prop->chksum )*/

        } /* end while ( ! done ) */

        /* Update the list's current version */
        atomic_store(&(list->curr_version), next_version);

        /* Decrement the number of threads that are currently active in the host struct */
        thrd = atomic_load(&(list->thrd));

        update_thrd = thrd;
        update_thrd.count--;

        if ( ! atomic_compare_exchange_strong(&(list->thrd), &thrd, update_thrd))
        {
            /* update stats */
            atomic_fetch_add(&(list->list_num_thrd_count_update_cols), 1);
        }
        else
        {
            /* update stats */
            atomic_fetch_add(&(list->list_num_thrd_count_update), 1);
        }

    } /* end if ( ! closing ) */

    return(ret_value);  

} /* H5P__delete_prop_list() */



/****************************************************************************************
 * Function:    H5P__clear_mt_list
 *
 * Purpose:     Frees all properties in a property list and then frees the list itself. 
 *
 *              This function first makes sure that no other thread is accessing this
 *              list struct, and then itsets the tag to H5P_MT_LIST_INVALID_TAG to mark 
 *              that this list is no longer valid for threads to access. 
 * 
 *              Then we set the pclass_ptr to NULL and iterate the lkup_tbl and set all 
 *              pointers to NULL, and decrement the parent's property ref_count. When all
 *              entries are set for deletion, we free the lkup_tbl. Then we iterate the 
 *              LFSLL, set the property's tag to H5P_MT_PROP_INVALID_TAG, set all 
 *              pointers to NULL, and free the property. When all pointers are set to 
 *              NULL, and all properties, lkup_tbl, and the list are freed, we access the
 *              parent class, and decrement it's reference count for derived property 
 *              lists.
 * 
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t
H5P__clear_mt_list(H5P_mt_list_t *list)
{
    H5P_mt_class_t             * parent;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t parent_thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_active_thread_count_t closing_thrd;
    H5P_mt_list_prop_ref_t       prop_ref;
    H5P_mt_list_table_entry_t  * entry;
    H5P_mt_prop_t              * first_prop;
    H5P_mt_prop_aptr_t           next_ptr;
    H5P_mt_prop_aptr_t           nulls_prop_ptr;
    H5P_mt_prop_value_t          nulls_value = {NULL, 0ULL};
    H5P_mt_prop_value_t          prop_value;
    H5P_mt_class_ref_counts_t    pclass_ref_count;
    H5P_mt_class_ref_counts_t    update_ref_count;
    hid_t                        null_list_id = 0;
    uint32_t                     phys_pl_len;
    uint32_t                     i;
    hbool_t                      done = FALSE;


    nulls_prop_ptr.ptr = NULL;
    nulls_prop_ptr.deleted = FALSE;
    nulls_prop_ptr.dummy_bool_1 = FALSE;
    nulls_prop_ptr.dummy_bool_2 = FALSE;
    nulls_prop_ptr.dummy_bool_3 = FALSE;

    /* Increment the thead count for active threads in the parent class */
    parent = list->pclass_ptr;

    do
    {
        parent_thrd = atomic_load(&(parent->thrd));
        update_thrd = parent_thrd;
        update_thrd.count++;

        if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                &parent_thrd, update_thrd) )
        {
            /* attempt failed, update stats and try again */
            atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
        }
        else
        {
            /* attempt succeed, update stats and continue */
            atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

            done = TRUE;
        }
    } while ( ! done );

    assert(done);



    /* Free the structures inside the list and then the list itself */

    done = FALSE;
    do 
    {
        thrd = atomic_load(&(list->thrd));

        assert(thrd.count == 0);
        assert(thrd.opening == FALSE);
        assert(thrd.closing == FALSE);

        closing_thrd = thrd;
        closing_thrd.closing = TRUE;

        if ( ! atomic_compare_exchange_strong(&(list->thrd), &thrd, closing_thrd))
        {
            fprintf(stderr, "\nUpdating list thrd struct for freeing failed.");
        }
        else
        {
            /* Set lists fields for deletion */
            list->tag = H5P_MT_LIST_INVALID_TAG;

            list->pclass_id  = H5I_INVALID_HID;
            list->pclass_ptr = NULL;
            
            atomic_store(&(list->plist_id), null_list_id);

            /* Set lkup_tbl up to be deleted */
            for ( i = 0; i < list->nprops_inherited; i++ )
            {
                entry = &list->lkup_tbl[i];

                entry->chksum = 0;
                entry->name = NULL;

                prop_ref = atomic_load(&(entry->base));

                first_prop = prop_ref.ptr;

                atomic_fetch_sub(&(first_prop->ref_count), 1);
                assert( 0 <= (atomic_load(&(first_prop->ref_count))));

                prop_ref.ptr = NULL;
                prop_ref.ver = 0;

                entry->base_delete_version = 0;

                prop_ref = atomic_load(&(entry->curr));

                prop_ref.ptr = NULL;
                prop_ref.ver = 0;

            } /* end for ( i = 0; i < list->nprops_inherited; i++ ) */

            list->lkup_tbl = NULL;

            free(list->lkup_tbl);

            /* Iterate the LFSLL and free all properties including sentinels */
            phys_pl_len = atomic_load(&(list->phys_pl_len));

            for ( i = 0; i < ( phys_pl_len ); i++ )
            {
                first_prop = list->pl_head;
                assert(first_prop);
                assert(first_prop->tag == H5P_MT_PROP_TAG);

                next_ptr = atomic_load(&(first_prop->next));
                
                list->pl_head = next_ptr.ptr;

                first_prop->tag = H5P_MT_PROP_INVALID_TAG;

                first_prop->name = NULL;

                prop_value = atomic_load(&(first_prop->value));

                //free(prop_value.ptr);

                atomic_store(&(first_prop->next), nulls_prop_ptr);
                atomic_store(&(first_prop->value), nulls_value);

                if ( first_prop->sentinel || next_ptr.deleted  )
                {
                    atomic_fetch_sub(&(list->phys_pl_len), 1);

                }
                else
                {
                    atomic_fetch_sub(&(list->phys_pl_len), 1);

                    atomic_fetch_sub(&(list->log_pl_len), 1);
                }

                free(first_prop);

                /* update stats */
                atomic_fetch_add(&(list->list_num_prop_freed), 1);

            } /* end for() */


            /* Ensure the list is empty, then free the list */
            first_prop = list->pl_head;
            assert( ! first_prop);

            assert( 0 == (atomic_load(&(list->log_pl_len))));
            assert( 0 == (atomic_load(&(list->phys_pl_len))));

            free(list);

            /* decrement the parent's ref_count for derived property lists */
            done = FALSE;
            do 
            {
                pclass_ref_count = atomic_load(&(parent->ref_count));
                update_ref_count = pclass_ref_count;
                update_ref_count.pl--;

                if ( ! atomic_compare_exchange_strong(&(parent->ref_count), 
                                                    &pclass_ref_count, update_ref_count))
                {
                    /* attempt failed, update stats and try again */
                    atomic_fetch_add(&(parent->class_num_ref_count_cols), 1);
                }
                else
                {
                    /* attempt succeeded, update stats and continue */
                    atomic_fetch_add(&(parent->class_num_ref_count_update), 1);

                    done = TRUE;
                }

            } while ( ! done );
        
        } /* end else */

        /* Decrement the thread count of active threads in the parent */
        done = FALSE;
        do
        {
            parent_thrd = atomic_load(&(parent->thrd));
            update_thrd = parent_thrd;
            update_thrd.count--;

            if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                    &parent_thrd, update_thrd) )
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeed, update stats and continue */
                atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        } while ( ! done );

        assert(done);

    } while ( ! done );

} /* H5P__clear_mt_list() */



herr_t
H5P__clear_mt_class(H5P_mt_class_t *class)
{
    H5P_mt_class_t             * parent;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t parent_thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_active_thread_count_t closing_thrd;
    H5P_mt_list_prop_ref_t       prop_ref;
    H5P_mt_list_table_entry_t  * entry;
    H5P_mt_prop_t              * first_prop;
    H5P_mt_prop_aptr_t           next_ptr;
    H5P_mt_prop_aptr_t           nulls_prop_ptr;
    H5P_mt_prop_value_t          nulls_value = {NULL, 0ULL};
    H5P_mt_prop_value_t          prop_value;
    H5P_mt_class_ref_counts_t    ref_count;
    H5P_mt_class_ref_counts_t    update_ref_count;
    hid_t                        null_list_id = 0;
    uint32_t                     phys_pl_len;
    uint32_t                     i;
    hbool_t                      done = FALSE;


    nulls_prop_ptr.ptr = NULL;
    nulls_prop_ptr.deleted = FALSE;
    nulls_prop_ptr.dummy_bool_1 = FALSE;
    nulls_prop_ptr.dummy_bool_2 = FALSE;
    nulls_prop_ptr.dummy_bool_3 = FALSE;

    if ( class->name == "root" )
    {
        done = TRUE;
    }
    else
    {
        /* Increment the thead count for active threads in the parent class */
        parent = class->parent_ptr;


        while ( ! done )
        {
            parent_thrd = atomic_load(&(parent->thrd));
            update_thrd = parent_thrd;
            update_thrd.count++;

            if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                    &parent_thrd, update_thrd) )
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeed, update stats and continue */
                atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                done = TRUE;
            }
        }
    }

    assert(done);



    /* Free the structures inside the class and then the class itself */

    done = FALSE;
    do 
    {
        thrd = atomic_load(&(class->thrd));

        assert(thrd.count == 0);
        assert(thrd.opening == FALSE);
        assert(thrd.closing == FALSE);

        closing_thrd = thrd;
        closing_thrd.closing = TRUE;

        if ( ! atomic_compare_exchange_strong(&(class->thrd), &thrd, closing_thrd))
        {
            fprintf(stderr, "\nUpdating list thrd struct for freeing failed.");
        }
        else
        {
            ref_count = atomic_load(&(class->ref_count));
            assert(ref_count.pl  == 0);
            assert(ref_count.plc == 0);
        
    
            /* Set lists fields for deletion */
            class->tag = H5P_MT_CLASS_INVALID_TAG;

            class->parent_id  = H5I_INVALID_HID;
            class->parent_ptr = NULL;
            
            class->name = NULL;

            atomic_store(&(class->id), null_list_id);


            /* Iterate the LFSLL and free all properties including sentinels */
            phys_pl_len = atomic_load(&(class->phys_pl_len));

            for ( i = 0; i < ( phys_pl_len ); i++ )
            {
                first_prop = class->pl_head;
                assert(first_prop);
                assert(first_prop->tag == H5P_MT_PROP_TAG);

                next_ptr = atomic_load(&(first_prop->next));
                
                class->pl_head = next_ptr.ptr;

                first_prop->tag = H5P_MT_PROP_INVALID_TAG;

                first_prop->name = NULL;

                prop_value = atomic_load(&(first_prop->value));

                //free(prop_value.ptr);

                atomic_store(&(first_prop->next), nulls_prop_ptr);
                atomic_store(&(first_prop->value), nulls_value);

                if ( first_prop->sentinel || next_ptr.deleted  )
                {
                    atomic_fetch_sub(&(class->phys_pl_len), 1);

                }
                else
                {
                    atomic_fetch_sub(&(class->phys_pl_len), 1);

                    atomic_fetch_sub(&(class->log_pl_len), 1);
                }

                free(first_prop);

                /* update stats */
                atomic_fetch_add(&(class->class_num_prop_freed), 1);

            } /* end for() */


            /* Ensure the LFSLL is empty, then free the class */
            first_prop = class->pl_head;
            assert( ! first_prop);

            assert( 0 == (atomic_load(&(class->log_pl_len))));
            assert( 0 == (atomic_load(&(class->phys_pl_len))));

            free(class);
        
        } /* end else */

        /* If the class is the root class it has no parent */
        if ( class->name == "root" )
        {
            done = TRUE;
        }
        /* Otherwise, decrement the parent's ref_count and active threads count */
        else
        {
            /* decrement the parent's ref_count for derived property list classes */                
            done = FALSE;
            do 
            {
                ref_count = atomic_load(&(parent->ref_count));
                update_ref_count = ref_count;
                update_ref_count.plc--;

                if ( ! atomic_compare_exchange_strong(&(parent->ref_count), 
                                                      &ref_count, update_ref_count))
                {
                    /* attempt failed, update stats and try again */
                    atomic_fetch_add(&(parent->class_num_ref_count_cols), 1);
                }
                else
                {
                    /* attempt succeeded, update stats and continue */
                    atomic_fetch_add(&(parent->class_num_ref_count_update), 1);

                    done = TRUE;
                }

            } while ( ! done );

            /* Decrement the thread count of active threads in the parent */
            done = FALSE;
            do
            {
                parent_thrd = atomic_load(&(parent->thrd));
                update_thrd = parent_thrd;
                update_thrd.count--;

                if ( ! atomic_compare_exchange_strong(&(parent->thrd), 
                                                        &parent_thrd, update_thrd) )
                {
                    /* attempt failed, update stats and try again */
                    atomic_fetch_add(&(parent->class_num_thrd_count_update_cols), 1);
                }
                else
                {
                    /* attempt succeed, update stats and continue */
                    atomic_fetch_add(&(parent->class_num_thrd_count_update), 1);

                    done = TRUE;
                }
            } while ( ! done );

            assert(done);

        } /* end else () */

    } while ( ! done );

} /* H5P__clear_mt_class() */


//#endif
