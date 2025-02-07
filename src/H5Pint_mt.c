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


typedef enum {
    CLASS_TAG = H5P_MT_CLASS_TAG,
    LIST_TAG  = H5P_MT_LIST_TAG

} H5P_mt_type_t;



/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/
H5P_mt_class_t * H5P__mt_create_class(H5P_mt_class_t *parent, uint64_t parent_version, 
                                      const char *name, H5P_plist_type_t type);

H5P_mt_list_t * H5P__mt_create_list(H5P_mt_class_t *parent, uint64_t parent_version);

void H5P__init_lkup_tbl(H5P_mt_class_t *parent, uint64_t version, H5P_mt_list_t *list);

H5P_mt_prop_t * H5P__create_sentinels(hbool_t in_prop_class);

H5P_mt_prop_t * H5P__create_prop(const char *name, void *value_ptr, size_t value_size, 
                                 bool in_prop_class, uint64_t create_version);

herr_t H5P__insert_prop_setup(void *param, const char *name, void *value, size_t size);

void H5P__insert_prop(H5P_mt_prop_t *pl_head, H5P_mt_prop_t *new_prop, 
                      int32_t *deletes_ptr, int32_t *nodes_visited_ptr, 
                      int32_t *thrd_cols_ptr);

herr_t H5P__set_delete_version(void *param, H5P_mt_prop_t *target_prop);

H5P_mt_prop_t * H5P__search_prop(void *param, H5P_mt_prop_t *target_prop);

void H5P__find_mod_point(H5P_mt_prop_t *pl_head, H5P_mt_prop_t **first_ptr_ptr, 
                         H5P_mt_prop_t **second_ptr_ptr, int32_t *deletes_ptr, 
                         int32_t *nodes_visited_ptr, int32_t *thrd_cols_ptr, 
                         H5P_mt_prop_t *target_prop, int32_t *cmp_result_ptr);

herr_t H5P__clear_mt_class(H5P_mt_class_t *class);

herr_t H5P__clear_mt_list(H5P_mt_list_t *list);


uint64_t H5P__mt_version_check(void *param, uint64_t curr_version, 
                               uint64_t next_vesrion);

herr_t H5P__inc_thrd_count(void *param);

herr_t H5P__dec_thrd_count(void *param);

herr_t H5P__inc_ref_count(H5P_mt_class_t *parent, hbool_t plc);

herr_t H5P__dec_ref_count(H5P_mt_class_t *parent, hbool_t plc);



int64_t H5P__calc_checksum(const char *name);






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
    herr_t           ret_value = SUCCEED;


    /* Allocating and Initializing the root property list class */

    H5P_mt_rootcls_g = H5P__mt_create_class(NULL, 0, "root", H5P_TYPE_ROOT);
    if ( ! H5P_mt_rootcls_g )
    {
        fprintf(stderr, "root class allocation failed.\n");
        return FAIL;
    }

    return(ret_value);
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
    H5P_mt_class_t             * new_class = NULL;
    hid_t                        parent_id;
    H5P_mt_class_ref_counts_t    ref_count;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_class_sptr_t          fl_next;
    H5P_mt_class_sptr_t          new_fl_next;
    H5P_mt_prop_t              * parent_prop;
    H5P_mt_prop_aptr_t           parent_next;
    H5P_mt_prop_t              * prev_prop;
    H5P_mt_prop_t              * new_prop;
    H5P_mt_prop_aptr_t           new_next;
    const char                 * prop_name;
    H5P_mt_prop_value_t          value;
    hbool_t                      done  = FALSE;
    uint64_t                     phys_pl_len;
    uint64_t                     log_pl_len;
    int32_t                      deletes = 0;
    int32_t                      nodes_visited = 0;
    int32_t                      thrd_cols = 0;
    int32_t                      cmp_result;

    H5P_mt_class_t * ret_value = NULL;



    /**
     * If parent is NULL then this is the root class, and there isn't a parent class to 
     * increment. Otherwise, increment the thrd count in the parent class.
     */
    if ( parent != NULL )
    {
        assert(parent->tag == H5P_MT_CLASS_TAG);

        if ( H5P__inc_thrd_count(parent) < 0 )
            return NULL;
    }



    /* Allocate memory for the new property list class */

    new_class = (H5P_mt_class_t *)malloc(sizeof(H5P_mt_class_t));
    if ( ! new_class )
    {
        fprintf(stderr, "\nNew class allocation failed.\n");
        return NULL;
    }

    /* Initialize class fields */
    new_class->tag = H5P_MT_CLASS_TAG;

    /* If root class then there is no parent */
    if ( parent != NULL )
    {
        parent_id = atomic_load(&(parent->id));
        new_class->parent_id      = parent_id;
    }
    else
    {
        new_class->parent_id = H5I_INVALID_HID;
    }

    new_class->parent_ptr     = parent;
    new_class->parent_version = parent_version;

    new_class->name = name;

    /** NOTE: Needs to be actually set after creation */
    atomic_store(&(new_class->id), H5I_INVALID_HID);
    new_class->type = type;

    atomic_store(&(new_class->curr_version), 1);
    atomic_store(&(new_class->next_version), 2);

    atomic_store(&(new_class->log_pl_len),  0);
    
    /* Creates the sentinel nodes and sets the negative sentinel as the head */
    new_class->pl_head = H5P__create_sentinels(TRUE);

    new_prop = new_class->pl_head;
    assert(new_prop);
    assert(new_prop->tag == H5P_MT_PROP_TAG);
    assert(new_prop->sentinel == TRUE);

    new_next = atomic_load(&(new_prop->next));
    new_prop = new_next.ptr;
    assert(new_prop);
    assert(new_prop->tag == H5P_MT_PROP_TAG);
    assert(new_prop->sentinel == TRUE);

    atomic_store(&(new_class->phys_pl_len), 2);


    /**
     * Copy the valid props from the parent into the new_class.
     * If root class then there is no parent. 
     */
    if ( parent != NULL )
    {
        phys_pl_len = atomic_load(&(parent->phys_pl_len));

        /*If the phy_pl_len of the parent's LFSLL is 2, there are not props to copy */
        if ( phys_pl_len > 2 )
        {
            parent_prop = parent->pl_head;
            assert(parent_prop);
            assert(parent_prop->tag == H5P_MT_PROP_TAG);
            assert(parent_prop->sentinel);

            parent_next = atomic_load(&(parent_prop->next));
            parent_prop = parent_next.ptr;
            assert(parent_prop);
            assert(parent_prop->tag == H5P_MT_PROP_TAG);
            assert( !parent_prop->sentinel);

            /* reset phys_pl_len because it'll be used for the new_class */
            phys_pl_len = 2;
            log_pl_len  = 0;

            while ( ! ( parent_prop->sentinel ) )
            {
                if (( parent_prop->create_version <= parent_version ) &&
                    (( parent_prop->delete_version > parent_version ) || 
                    ( parent_prop->delete_version == 0 ) ) )
                {
                    prop_name = parent_prop->name;
                    value = atomic_load(&(parent_prop->value));

                    new_prop = H5P__create_prop(prop_name, value.ptr, value.size, TRUE, 1);
                    if ( ! new_prop )
                        return NULL;

                    H5P__insert_prop(new_class->pl_head,
                        new_prop,
                        &deletes,
                        &nodes_visited,
                        &thrd_cols);

                    assert(deletes >= 0);
                    assert(nodes_visited >= 0);
                    assert(thrd_cols >= 0);

                    /**
                     * NOTE: I'm not currently updating the stats for deletes, notes_visited, and
                     * thrd_cols here, because this class is being created, so nodes_visited
                     * doesn't matter yet, and it shouldn't be visible yet to other threads, so 
                     * there shouldn't be any cols.  
                     */

                    phys_pl_len++;
                    log_pl_len++;
                }

                /**
                 * Iterate the parent's LFSLL and if the next prop has the same chksum
                 * double check that it is an older version, and iterate the LFSLL again.
                 */
                do
                {
                    prev_prop = parent_prop;
                    parent_next = atomic_load(&(parent_prop->next));
                    parent_prop = parent_next.ptr;
                    assert(parent_prop);
                    assert(parent_prop->tag == H5P_MT_PROP_TAG);

                    /**
                     * If there are multiple versions of a property, we must make sure we
                     * only copy the most recent but still valid property.
                     */
                    if ( parent_prop->chksum == prev_prop->chksum )
                    {
                        cmp_result = strcmp(prev_prop->name, parent_prop->name);

                        if ( cmp_result == 0 )
                        {
                            assert( (atomic_load(&(prev_prop->create_version))) > 
                                    (atomic_load(&(parent_prop->create_version))) );

                            if ( ((atomic_load(&(prev_prop->create_version))) > parent_version ) && 
                                 ((atomic_load(&(parent_prop->create_version))) <= parent_version ) )
                            {
                                done = TRUE;
                            }
                        }
                        else 
                        {
                            fprintf(stderr, "\nDifferent props have same chksum.\n");
                        }
                    }
                    else
                    {
                        done = TRUE;
                    }

                } while ( ! done );

            
            } /* while ( ! ( parent_prop->sentinel ) ) */

            assert(parent_prop->sentinel);
    
            new_next = atomic_load(&(new_prop->next));
            assert(new_next.ptr->sentinel);
        
            /* update physical and logical lengths */
            atomic_store(&(new_class->phys_pl_len), phys_pl_len);
            atomic_store(&(new_class->log_pl_len), log_pl_len);
        
        }/* end if ( phys_pl_len > 2 ) */

    } /* end if ( parent != NULL ) */

    
    /* Continue intializing the class's fields */

    ref_count.pl      = 0;
    ref_count.plc     = 0;
    ref_count.deleted = FALSE;
    atomic_store(&(new_class->ref_count), ref_count);

    thrd.count   = 0;
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
    atomic_init(&(new_class->class_max_num_phys_props),           phys_pl_len);
    atomic_init(&(new_class->class_max_num_log_props),            log_pl_len);

    /* H5P_mt_class_t stats */
    atomic_init(&(new_class->class_insert_total_nodes_visited),   0ULL);
    atomic_init(&(new_class->class_delete_total_nodes_visited),   0ULL);
    atomic_init(&(new_class->class_insert_max_nodes_visited),     0ULL);
    atomic_init(&(new_class->class_delete_max_nodes_visited),     0ULL);
    atomic_init(&(new_class->class_wait_for_curr_version_to_inc), 0ULL);

    /* H5P_mt_active_thread_count_t stats */
    atomic_init(&(new_class->num_thrd_count_update_cols),   0ULL);
    atomic_init(&(new_class->num_thrd_count_update),        0ULL);
    atomic_init(&(new_class->num_thrd_closing_flag_set),    0ULL);
    atomic_init(&(new_class->num_thrd_opening_flag_set),    0ULL);

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




    /**
     * TODO: will probably add the code to insert this class into the index here.
     */


    /* update thrd struct of the new_class */
    thrd.count   = 0;
    thrd.opening = FALSE;
    thrd.closing = FALSE;

    atomic_store(&(new_class->thrd), thrd);

    /* update parent's ref count and thrd count */
    if ( parent != NULL )
    {
        H5P__inc_ref_count(parent, TRUE);
        H5P__dec_thrd_count(parent);
    }


done:

    ret_value = new_class;

    return(ret_value);
   
} /* H5P__create_class() */



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
    H5P_mt_list_t              * new_list = NULL;
    hid_t                        parent_id;
    H5P_mt_active_thread_count_t list_thrd;
    H5P_mt_list_sptr_t           fl_next;

    H5P_mt_list_t * ret_value = NULL;

    assert(parent);
    assert(parent->tag == H5P_MT_CLASS_TAG);

    
    /* Increment parent's thrd count */
    if ( H5P__inc_thrd_count(parent) < 0 )
        return NULL;



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

    /* Creates the sentinel nodes and sets teh negative sentinel as the head */
    new_list->pl_head = H5P__create_sentinels(FALSE);
    
    atomic_store(&(new_list->log_pl_len),  0);
    atomic_store(&(new_list->phys_pl_len), 2);

    /**
     * NOTE: will need to set this up correctly when I add callbacks.
     */
    atomic_store(&(new_list->class_init), TRUE);

    list_thrd.count   = 0;
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
    atomic_init(&(new_list->num_thrd_count_update_cols), 0ULL);
    atomic_init(&(new_list->num_thrd_count_update),      0ULL);
    atomic_init(&(new_list->num_thrd_closing_flag_set),  0ULL);
    atomic_init(&(new_list->num_thrd_opening_flag_set),  0ULL);

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


    /**
     * TODO: will probably add the code to insert this class into the index here.
     */


    /* update thrd struct of the new_list */
    list_thrd.count   = 0;
    list_thrd.opening = FALSE;
    list_thrd.closing = FALSE;

    atomic_store(&(new_list->thrd), list_thrd);

    /* update parent's ref count and thrd count */
    H5P__inc_ref_count(parent, FALSE);
    H5P__dec_thrd_count(parent);


done:

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
    uint32_t                    nprops;
    int32_t                     cmp_result;
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

    /* Allocates the number of entries needed in the lkup_tbl */
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
            entry = &list->lkup_tbl[i];

            entry->chksum = second_prop->chksum;
            entry->name   = second_prop->name;
            
            base.ptr = atomic_load(&(second_prop));
            base.ver = 1;
            atomic_store(&(entry->base), base);

            done = FALSE;
            do
            {
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

            //} /* end else ( first_prop->chksum != second_prop->chksum )*/

        } /* end if () */


        /* Iterate the parent's LFSLL and check if the next prop has the same chksum */
        done = FALSE;
        do
        {
            first_prop  = second_prop;
            next_ptr    = atomic_load(&(first_prop->next));
            second_prop = next_ptr.ptr;
            assert(second_prop);
            assert(second_prop->tag == H5P_MT_PROP_TAG);

            /**
             * If there are multiple versions of a property, we must make sure we
             * only copy the most recent but still valid property.
             */
            if ( first_prop->chksum == second_prop->chksum )
            {
                cmp_result = strcmp(first_prop->name, second_prop->name);

                if ( cmp_result == 0 )
                {
                    assert( (atomic_load(&(first_prop->create_version))) > 
                            (atomic_load(&(second_prop->create_version))) );

                    if ( ((atomic_load(&(first_prop->create_version))) > version ) && 
                            ((atomic_load(&(second_prop->create_version))) <= version ) )
                    {
                        done = TRUE;
                    }
                }
                else 
                {
                    fprintf(stderr, "\nDifferent props have same chksum.\n");
                }
            }
            else
            {
                done = TRUE;
            }

        } while ( ! done );
    
    } /* end while ( ! second_prop->sentinel ) */

} /* H5P__init_lkup_tbl() */



/****************************************************************************************
 * Function:    H5P__create_sentinels
 *
 * Purpose:     Creates the two sentinel nodes for a class's or lists LFSLL            
 *
 * Return:      Success: Returns a pointer to 'neg_sentinel' (H5P_mt_prop_t)
 * 
 *              Failure: NULL
 *
 ****************************************************************************************
 */
H5P_mt_prop_t * 
H5P__create_sentinels(hbool_t in_prop_class)
{
    H5P_mt_prop_t              * pos_sentinel;
    H5P_mt_prop_aptr_t           pos_next;
    H5P_mt_prop_value_t          pos_value;
    H5P_mt_prop_t              * neg_sentinel;
    H5P_mt_prop_aptr_t           neg_next;
    H5P_mt_prop_value_t          neg_value;

    H5P_mt_prop_t * ret_value = NULL;

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
    pos_sentinel->in_prop_class = in_prop_class;

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
    neg_sentinel->in_prop_class = in_prop_class;

    /* ref_count is only used if the property is in a property class. */
    atomic_store(&(neg_sentinel->ref_count), 0);

    neg_sentinel->chksum = LLONG_MIN;

    neg_sentinel->name = (char *)"neg_sentinel";

    neg_value.ptr  = NULL;
    neg_value.size = 0;

    atomic_store(&(neg_sentinel->value), neg_value);
    
    atomic_store(&(neg_sentinel->create_version), 1);
    atomic_store(&(neg_sentinel->delete_version), 0); /* Set to 0 because it's not deleted */


    return(neg_sentinel);

} /* H5P__create_setinels() */



/****************************************************************************************
 * Function:    H5P__create_prop
 *
 * Purpose:     Creates a new property (H5P_mt_prop_t)
 *
 *              This function is called by the H5P__insert_prop() functions to allocate 
 *              and initialize a new property.             
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
    //size_t              name_len;

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

/**
 * TODO: Fix the undefined reference to H5_checksum_fletcher32
 */
#if 0 

    name_len = strlen(name);

    new_prop->chksum = H5_checksum_fletcher32(name, name_len);

#else

    new_prop->chksum = H5P__calc_checksum(name);

#endif

    assert( new_prop->chksum > LLONG_MIN && new_prop->chksum < LLONG_MAX );

    new_prop->name = (char *)name;

    value.ptr  = value_ptr;
    value.size = value_size;
    atomic_store(&(new_prop->value), value);
    
    atomic_store(&(new_prop->create_version), version);
    atomic_store(&(new_prop->delete_version), 0);

    ret_value = new_prop;

    return ret_value;

} /* H5P__create_prop() */



/****************************************************************************************
 * Function:    H5P__insert_prop_setup
 *
 * Purpose:     Inserts a new property (H5P_mt_prop_t struct) into the LFSLL of a 
 *              property list (H5P_mt_list_t) or property list class (H5P_mt_class_t).
 *
 *              NOTE: When 'modifying' a property is this multithread safe version of 
 *              H5P, a new property must be created with a new create_version. Because
 *              of this, when an entirely new property is created, or a new property is
 *              created for the new version of a property, this function is called to 
 *              handle either case.
 * 
 *              This function first checks if it is dealing with a class or a list, and
 *              calls H5P__create_prop() to create the new
 *              H5P_mt_prop_t struct. 
 * 
 *              The primary purpose of this function is handling incrementing and 
 *              decrementing thread count, updating the version of the host structure 
 *              of the LFSLL after the property was inserted, and updating stats. 
 * 
 *              The actual insertion of the property is handled by H5P__insert_prop().
 * 
 *              Lists have an extra step here, that classes do not. In case this is a 
 *              'modification' to an existing property, the list must iterate it's 
 *              lkup_tbl and if there is another version in the lkup_tbl of the new
 *              property, it must update the curr.ptr to point to the new property, and 
 *              update curr.ver to the version of the new property. 
 * 
 *              NOTE: See the comment description above struct H5P_mt_prop_t in 
 *              H5Ppkg_mt.h for details on how the LFSLL are sorted.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the list, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_list_t or 
 *              H5P_mt_class_t in H5Ppkg_mt.h for more details.
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
herr_t
H5P__insert_prop_setup(void *param, const char *name, void *value, size_t size)
{
    H5P_mt_prop_t    * new_prop;
    H5P_mt_prop_t    * pl_head;
    uint64_t           curr_version  = 0;
    uint64_t           next_version  = 0;
    int32_t            deletes       = 0;
    int32_t            nodes_visited = 0;
    int32_t            thrd_cols     = 0;
    int32_t            cmp_result    = 0;
    hbool_t            done          = FALSE;
    H5P_mt_type_t      type;
    H5P_mt_class_t   * class = NULL;
    H5P_mt_list_t    * list  = NULL;
    H5P_mt_list_table_entry_t * entry;
    H5P_mt_list_prop_ref_t      curr;
    H5P_mt_list_prop_ref_t      new_curr;

    herr_t             ret_value     = SUCCEED;


    assert(name);
    assert((size > 0 && value != NULL) || (size == 0));


    type = *((H5P_mt_type_t *) param);

    if ( type == CLASS_TAG )
    {
        class = (H5P_mt_class_t *)param;

        assert(class);
        assert(class->tag == H5P_MT_CLASS_TAG);

        atomic_fetch_add(&(class->H5P__insert_prop_class__num_calls), 1);

        /* Increment thread count */
        if ( (ret_value = (H5P__inc_thrd_count(class)) ) < 0 )
            return FAIL;


        curr_version = atomic_load(&(class->curr_version));
        next_version = atomic_fetch_add(&(class->next_version), 1);

        /* Ensure another thread isn't modifying the LFSLL*/
        if ( (curr_version + 1 ) < next_version )
            if ( 0 == (curr_version = 
                       H5P__mt_version_check(class, curr_version, next_version)) )
                return FAIL;


        /* This thread can now proceed and create the new property */
        new_prop = H5P__create_prop(name, value, size, TRUE, next_version);
        
        /* update stats */
        atomic_fetch_add(&(class->H5P__create_prop__num_calls), 1);

        pl_head = class->pl_head;
    } 
    else if ( type == LIST_TAG )
    {
        list = (H5P_mt_list_t *)param;

        assert(list);
        assert(list->tag == H5P_MT_LIST_TAG);

        atomic_fetch_add(&(list->H5P__insert_prop_list__num_calls), 1);

        /* Increment thread count */
        if ( (ret_value = (H5P__inc_thrd_count(list)) ) < 0 )
            return FAIL;


        curr_version = atomic_load(&(list->curr_version));
        next_version = atomic_fetch_add(&(list->next_version), 1);

        /* Ensure another thread isn't modifying the LFSLL*/
        if ( (curr_version + 1 ) < next_version )
            if ( 0 == (curr_version = 
                       H5P__mt_version_check(list, curr_version, next_version)) )
                return FAIL;


        /* This thread can now proceed and create the new property */
        new_prop = H5P__create_prop(name, value, size, FALSE, next_version);

        /* update stats */
        atomic_fetch_add(&(list->H5P__create_prop__num_calls), 1);

        pl_head = list->pl_head;
    } 
    else
    {
        fprintf(stderr, "Error with passed type.\n");

        return FAIL;
    }

    assert(curr_version + 1 ==  next_version); 

    assert(new_prop);
    assert(new_prop->tag == H5P_MT_PROP_TAG);

    assert(pl_head);
    assert(pl_head->tag == H5P_MT_PROP_TAG);
    assert(pl_head->sentinel);



    H5P__insert_prop(pl_head,
                    new_prop,
                    &deletes,
                    &nodes_visited,
                    &thrd_cols);



    /* If this is a list, check if this is a 'modification' to a prop in the lkup_tbl */
    if ( ! class )
    {
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

                            done = TRUE;
                        }

                    } while ( ! done );

                    assert(done);
                    
                } /* end if ( target_prop->name == entry->name) */

            } /* end if ( target_prop->chksum == entry->chksum ) */

        } /* end for ( uint32_t i = 0; i < list->nprops_inherited; i++ ) */
    
    } /* end if ( ! class ) */ 

    assert(deletes >= 0);
    assert(nodes_visited >= 0);
    assert(thrd_cols >= 0);

    if ( class )
    {
        /* update stats */
        atomic_fetch_add(&(class->class_insert_nodes_visited), nodes_visited);
        atomic_fetch_add(&(class->class_num_insert_prop_cols), thrd_cols);

        /* inc physical and logical length of the LFSLL, and nprops_added and nprops */
        atomic_fetch_add(&(class->log_pl_len),   1);
        atomic_fetch_add(&(class->phys_pl_len),  1);

        /* Update curr_version */
        atomic_store(&(class->curr_version), next_version);

        /* Update the class's current version */
        atomic_store(&(class->curr_version), next_version);

        ret_value = H5P__dec_thrd_count(class);
    }
    else
    {
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

        ret_value = H5P__dec_thrd_count(list);
    }


done:

    return(ret_value);

} /* H5P__insert_prop_setup() */



/****************************************************************************************
 * Function:    H5P__insert_prop
 *
 * Purpose:     Inserts a new property (H5P_mt_prop_t struct) into the LFSLL of a 
 *              property list (H5P_mt_list_t) or property list class (H5P_mt_class_t).
 *
 *              This function passes pointers to H5P__find_mod_point() to find the 
 *              location to insert the new property. Then inserts the new property 
 *              between the properties returned by H5P__find_mod_point().
 *
 * Return:      SUCCEED/FAIL    
 *
 ****************************************************************************************
 */
void
H5P__insert_prop(H5P_mt_prop_t *pl_head, 
                 H5P_mt_prop_t *new_prop, 
                 int32_t *deletes_ptr,
                 int32_t *nodes_visited_ptr,
                 int32_t *thrd_cols_ptr)
{
    H5P_mt_prop_t    * first_prop;
    H5P_mt_prop_t    * second_prop;
    H5P_mt_prop_aptr_t next;
    H5P_mt_prop_aptr_t updated_next;
    int32_t            deletes       = 0;
    int32_t            nodes_visited = 0;
    int32_t            thrd_cols     = 0;
    int32_t            cmp_result    = 0;
    hbool_t            done          = FALSE;

    herr_t             ret_value     = SUCCEED;


    assert(pl_head);
    assert(pl_head->tag == H5P_MT_PROP_TAG);
    assert(pl_head->sentinel);
    
    assert(new_prop);
    assert(new_prop->tag == H5P_MT_PROP_TAG);


    do
    {
        first_prop  = NULL;
        second_prop = NULL;

        /** 
         * The current implementation of H5P__find_mod_point() should either succeed or 
         * trigger an assertion -- thus no need to check return value at present.
         */

        H5P__find_mod_point(pl_head,       
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

        /** 
         * Double check that properties returned by H5P__find_mod_point() 
         * are correct for insertion.
         */
        if ( first_prop->chksum <= new_prop->chksum )
        {
            if (( second_prop->chksum > new_prop->chksum ) ||
            (( second_prop->chksum == new_prop->chksum ) && ( cmp_result > 0 )) ||
            (( second_prop->chksum == new_prop->chksum ) && ( cmp_result == 0 ) &&
                ( second_prop->create_version < new_prop->create_version ) ) )
            {

                /* prep new_prop to be inserted between first_prop and second_prop */
                next = atomic_load(&(first_prop->next));

                assert(next.ptr == second_prop);

                atomic_store(&(new_prop->next), next);

                /** Attempt to atomically insert new_prop 
                 * 
                 * NOTE: If this fails, another thread modified the LFSLL and we must 
                 * update stats and restart to ensure new_prop is correctly inserted.
                 */

                updated_next.ptr          = new_prop;
                updated_next.deleted      = FALSE;
                updated_next.dummy_bool_1 = FALSE;
                updated_next.dummy_bool_2 = FALSE;
                updated_next.dummy_bool_3 = FALSE;

                if ( ! atomic_compare_exchange_strong(&(first_prop->next),
                                                        &next, updated_next) )
                {
                    thrd_cols++;                    
                }
                else /* The attempt was successful update stats mark done */
                {
                    done = TRUE;
                }

            } /* end if () */
            else
            {
                fprintf(stderr, "\nWrong prop pointers passed to insert_prop\n");
                return;
            }

        } /* end if ( first_prop->chksum <= new_prop->chksum ) */
        else
        {
            fprintf(stderr, "\nWrong prop pointers passed to insert_prop\n");
            return;
        }

    } while ( ! done );

    /* Update pointers for stats collecting passed into the function */
    *deletes_ptr       = deletes;   
    *nodes_visited_ptr = nodes_visited;
    *thrd_cols_ptr     = thrd_cols;

    return;

} /* H5P__insert_prop() */



/****************************************************************************************
 * Function:    H5P__set_delete_version
 *
 * Purpose:     Sets the delete_version of a property (H5P_mt_prop_t) in a property list
 *              (H5P_mt_list_t) or in a property list class (H5P_mt_class_t);
 *
 *              NOTE: Current implementation does not physically or logically delete 
 *              H5P_mt_prop_t structs from the LFSLL.
 *              
 *              H5P__set_delete_version() first determines if it dealing with a list or a 
 *              class. If it's a list it iterates the lkup_tbl searching for the property
 *              to delete. If the property is in the lkup_tbl we must check if curr.ptr 
 *              is NULL. If it is then we set the base_delete_version and are done. 
 * 
 *              If curr.ptr is not NULL we must check curr.ver. If curr.ver is less than 
 *              next_version, we set the delete_version of the property curr.ptr points 
 *              to and are done.
 * 
 *              If curr.ver is greater than next_version (which means that there are
 *              multiple modifications to the list waiting for their turn), we must
 *              search the LFSLL for the target_property, which we do by calling the 
 *              function H5P__find_mod_point().
 * 
 *              We double check that the property returned by H5P__find_mod_point() is
 *              the target_prop and if so then set its delete_version, and update the
 *              list's or class's curr_version to next_version.
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
H5P__set_delete_version(void *param, H5P_mt_prop_t *target_prop)
{
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    H5P_mt_prop_t * pl_head;
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
    H5P_mt_type_t      type;
    H5P_mt_class_t   * class = NULL;
    H5P_mt_list_t    * list  = NULL;

    herr_t          ret_value = SUCCEED;


    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);


    type = *((H5P_mt_type_t *) param);

    if ( type == CLASS_TAG )
    {
        class = (H5P_mt_class_t *)param;

        assert(class);
        assert(class->tag == H5P_MT_CLASS_TAG);

        atomic_fetch_add(&(class->H5P__delete_prop_class__num_calls), 1);

        if ( (ret_value = (H5P__inc_thrd_count(class)) ) < 0 )
            return FAIL;

        curr_version = atomic_load(&(class->curr_version));
        next_version = atomic_fetch_add(&(class->next_version), 1);

        /* Ensure another thread isn't modifying the LFSLL*/
        if ( (curr_version + 1 ) < next_version )
            if ( 0 == (curr_version = 
                       H5P__mt_version_check(class, curr_version, next_version)) )
                return FAIL;

        pl_head = class->pl_head;
    }
    else if ( type == LIST_TAG )
    {
        list = (H5P_mt_list_t *)param;

        assert(list);
        assert(list->tag == H5P_MT_LIST_TAG);

        /* update stats */
        atomic_fetch_add(&(list->H5P__delete_prop_list__num_calls), 1);

        if ( (ret_value = (H5P__inc_thrd_count(list)) ) < 0 )
            return FAIL;

        curr_version = atomic_load(&(list->curr_version));
        next_version = atomic_fetch_add(&(list->next_version), 1);

        /* Ensure another thread isn't modifying the LFSLL*/
        if ( (curr_version + 1 ) < next_version )
            if ( 0 == (curr_version = 
                       H5P__mt_version_check(list, curr_version, next_version)) )
                return FAIL;

        pl_head = list->pl_head;
    }
    else
    {
        fprintf(stderr, "Error with passed type.\n");

        return FAIL;
    }



    /* If this is a list, check the lkup_tbl for the target_prop */
    do
    {
        if ( ! class )
        {
            for ( uint32_t i = 0; i < list->nprops_inherited; i++ )
            {
                entry = &list->lkup_tbl[i];

                /* Checking if the table entry has the same chksum */
                if ( target_prop->chksum == entry->chksum )
                {
                    /* Ensure the properties are in fact the same by checking their names */
                    cmp_result = strcmp(entry->name, target_prop->name);

                    /* The target_prop is in the lkup_tbl */
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
                             * If the curr.ver is less than next_version we set that properties
                             * delete_version to the next_version and mark done.  
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
                         * this list, and the most recent version is the base pointer that
                         * still points to it's parent class's property.
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

        } /* end if ( ! class ) */




        /* If not a list or not in the list's lkup_tbl search the LFSLL */

        while ( ! done )
        {
            first_prop  = NULL;
            second_prop = NULL;

            /** 
             * The current implementation of H5P__find_mod_point() should either succeed or 
             * trigger an assertion -- thus no need to check return value at present.
             */            
            H5P__find_mod_point(pl_head,         
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
                    if ( class )
                        atomic_fetch_add(&(class->class_num_prop_delete_version_set), 1);
                    else
                        atomic_fetch_add(&(list->list_num_prop_delete_version_set), 1);

                }
                else
                {
                    fprintf(stderr, "\nWrong prop pointers passed to delete_prop_list\n");
                } 
            
            } /* end if ( first_prop->chksum <= target_prop->chksum )*/
            else
            {
                fprintf(stderr, "\nWrong prop pointers passed to delete_prop_list\n");
            } 

        } /* end while ( ! done ) */

    } while ( ! done );


    assert(deletes >= 0);
    assert(nodes_visited >= 0);
    assert(thrd_cols >= 0);

    if ( class )
    {
        /* update stats */
        atomic_fetch_add(&(class->class_delete_nodes_visited), nodes_visited);
        atomic_fetch_add(&(class->class_num_delete_prop_cols), thrd_cols);

        /* Update the class's current version */
        atomic_store(&(class->curr_version), next_version);

        ret_value = H5P__dec_thrd_count(class);
    }
    else
    {
        /* update stats */
        atomic_fetch_add(&(list->list_insert_nodes_visited), nodes_visited);
        atomic_fetch_add(&(list->list_num_insert_prop_cols), thrd_cols);

        /* Update the list's current version */
        atomic_store(&(list->curr_version), next_version);

        ret_value = H5P__dec_thrd_count(list);
    }

    return(ret_value);

} /* H5P__set_delete_version() */



/**
 * 
 */
H5P_mt_prop_t *
H5P__search_prop(void *param, H5P_mt_prop_t *target_prop)
{
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    H5P_mt_prop_t * pl_head;
    H5P_mt_prop_aptr_t next;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    H5P_mt_list_table_entry_t  * entry;
    H5P_mt_list_prop_ref_t       curr;
    H5P_mt_list_prop_ref_t       base;
    uint64_t        curr_version    = 0;
    uint64_t        next_version    = 0;
    uint64_t        create_version  = 0;
    uint64_t        delete_version  = 0;
    int32_t         deletes         = 0;
    int32_t         nodes_visited   = 0;
    int32_t         thrd_cols       = 0;
    int32_t         cmp_result      = 0;
    hbool_t         done            = FALSE;
    H5P_mt_type_t      type;
    H5P_mt_class_t   * class = NULL;
    H5P_mt_list_t    * list  = NULL;

    H5P_mt_prop_t * ret_value = NULL;


    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);


    type = *((H5P_mt_type_t *) param);

    if ( type == CLASS_TAG )
    {
        class = (H5P_mt_class_t *)param;

        assert(class);
        assert(class->tag == H5P_MT_CLASS_TAG);

        /** update stats TODO: add stats for search_prop() */
        //atomic_fetch_add(&(class->H5P__delete_prop_class__num_calls), 1);

        if (  (H5P__inc_thrd_count(class) ) < 0 )
            return NULL;

        pl_head = class->pl_head;
    }
    else if ( type == LIST_TAG )
    {
        list = (H5P_mt_list_t *)param;

        assert(list);
        assert(list->tag == H5P_MT_LIST_TAG);

        /** update stats TODO: add stats for search_prop() */
        //atomic_fetch_add(&(list->H5P__delete_prop_list__num_calls), 1);

        if ( (H5P__inc_thrd_count(list) ) < 0 )
            return NULL;

        curr_version = atomic_load(&(list->curr_version));

        pl_head = list->pl_head;
    }
    else
    {
        fprintf(stderr, "Error with passed type.\n");

        return NULL;
    }



    /* If this is a list, check the lkup_tbl for the target_prop */
    do 
    {
        if ( ! class )
        {
            for ( uint32_t i = 0; i < list->nprops_inherited; i++ )
            {
                entry = &list->lkup_tbl[i];

                /* Checking if the table entry has the same chksum */
                if ( target_prop->chksum == entry->chksum )
                {
                    /* Ensure the properties are in fact the same by checking their names */
                    cmp_result = strcmp(entry->name, target_prop->name);

                    /* The target_prop is in the lkup_tbl */
                    if ( cmp_result == 0 )
                    {
                        curr = atomic_load(&(entry->curr));

                        /**
                         * If curr.ptr is not NULL, then the property has been modified, and
                         * there is a more recent version in the LFSLL of the property list.
                         */
                        if ( curr.ptr )
                        {
                            create_version = atomic_load(&(target_prop->create_version));

                            /**
                             * If the curr.ver equals target_prop's create_version then this
                             * is the correct version, set done and return a pointer to this
                             * property.
                             */
                            if ( curr.ver == create_version )
                            {
                                ret_value = curr.ptr;

                                done = TRUE;
                            }
                            /**
                             * If curr.ver doesn't equal target_prop's create_version we must
                             * search the list's LFSLL for the property. 
                             */
                            else
                            {
                                /* update stats */
                                //atomic_fetch_add(&(list->list_num_delete_older_version_than_curr), 1);

                                break;
                            }
                        }
                        /**
                         * If curr.ptr is NULL, then this property has not been modified in
                         * this list, and the most recent version is the base pointer that
                         * points to it's parent class's property.
                         */
                        else
                        {
                            ret_value = base.ptr;

                            done = TRUE;

                            /* update stats */
                            //atomic_fetch_add(&(list->list_num_base_delete_version_set), 1);
                        }

                    } /* end if ( target_prop->name == entry->name) */

                } /* end if ( target_prop->chksum == entry->chksum ) */

            } /* end for ( uint32_t i = 0; i < list->nprops_inherited; i++ ) */

        } /* end if ( ! class ) */
        


        /* If not a list or not in the list's lkup_tbl search the LFSLL */

        while ( ! done )
        {
            first_prop  = NULL;
            second_prop = NULL;

            /** 
             * The current implementation of H5P__find_mod_point() should either succeed or 
             * trigger an assertion -- thus no need to check return value at present.
             */            
            H5P__find_mod_point(pl_head,         
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
                /* If true, then we have found target property and can return it. */
                if (( second_prop->chksum == target_prop->chksum ) && 
                        ( cmp_result == 0 ) && 
                        ( second_prop->create_version == target_prop->create_version ) )
                {

                    ret_value = second_prop;

                    done = TRUE;

                    /** TODO: need to add stats for search_prop() */
                    /* update stats */
                    //if ( class )
                        //atomic_fetch_add(&(class->class_num_prop_delete_version_set), 1);
                    //else
                        //atomic_fetch_add(&(list->list_num_prop_delete_version_set), 1);

                }
                else
                {
                    fprintf(stderr, "\nWrong prop pointers passed to search_prop\n");
                } 
            
            } /* end if ( first_prop->chksum <= target_prop->chksum )*/
            else
            {
                fprintf(stderr, "\nWrong prop pointers passed to search_prop\n");
            } 

        } /* end while ( ! done ) */

    } while ( ! done );


    return(ret_value);

} /* H5P__search_prop() */



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
H5P__find_mod_point(H5P_mt_prop_t  *pl_head,
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



    assert(pl_head->sentinel);
    
    next_prop = atomic_load(&(pl_head->next));
    second_prop = next_prop.ptr;
    assert(pl_head != second_prop);

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



    first_prop = atomic_load(&(pl_head));
        
    assert(first_prop);
    assert(first_prop->tag == H5P_MT_PROP_TAG);

    next_prop = atomic_load(&(first_prop->next));
    second_prop = next_prop.ptr;

    assert(second_prop);
    assert(second_prop->tag == H5P_MT_PROP_TAG);

    /* Iterate through the LFSLL of properties */
    do 
    {
        assert(first_prop->chksum <= target_prop->chksum);

        /* If true then we're done, and can return the current pointers */
        if ( second_prop->chksum > target_prop->chksum )
        {
            done = TRUE;
        }
        /* If the chksums equal we must compare the property names */
        else if ( second_prop->chksum == target_prop->chksum )
        {
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
    hbool_t                      plc = FALSE;

    herr_t                       ret_value = SUCCEED;


    nulls_prop_ptr.ptr          = NULL;
    nulls_prop_ptr.deleted      = FALSE;
    nulls_prop_ptr.dummy_bool_1 = FALSE;
    nulls_prop_ptr.dummy_bool_2 = FALSE;
    nulls_prop_ptr.dummy_bool_3 = FALSE;

    /* Increment the thead count for active threads in the parent class */
    parent = list->pclass_ptr;


    if ( (ret_value = (H5P__inc_thrd_count(parent)) ) < 0 )
        return FAIL;



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
            done = TRUE;

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

            H5P__dec_ref_count(parent, plc);

        
        } /* end else */

        ret_value = H5P__dec_thrd_count(parent);


    } while ( ! done );

done:

    return(ret_value);

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
    hbool_t                      plc = TRUE;
    const char                 * name;

    herr_t                       ret_value = SUCCEED;


    nulls_prop_ptr.ptr = NULL;
    nulls_prop_ptr.deleted = FALSE;
    nulls_prop_ptr.dummy_bool_1 = FALSE;
    nulls_prop_ptr.dummy_bool_2 = FALSE;
    nulls_prop_ptr.dummy_bool_3 = FALSE;

    name = class->name;

    if ( name == "root" )
    {
        done = TRUE;
    }
    else
    {
        /* Increment the thead count for active threads in the parent class */
        parent = class->parent_ptr;

        if ( (ret_value = (H5P__inc_thrd_count(parent)) ) < 0 )
            return FAIL;

        done = TRUE;
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
            done = TRUE;
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
        if ( name == "root" )
        {
            done = TRUE;
        }
        /* Otherwise, decrement the parent's ref_count and active threads count */
        else
        {
            H5P__dec_ref_count(parent, plc);

            ret_value = H5P__dec_thrd_count(parent);

        } /* end else () */

    } while ( ! done );

done:

    return(ret_value);

} /* H5P__clear_mt_class() */



/****************************************************************************************
 * Function:    H5P__mt_version_check
 *
 * Purpose:     Compares the curr_version to the next_version. 
 *
 *              To ensure true atomicity, any modification to either a class's or list's
 *              LFSLL must be done one thread at a time. So, the curr_version must be one
 *              less than next_version, and if it is less than one it sleeps and checks
 *              again. 
 * 
 *              NOTE: sleeping is a temporary solution and future iterations of this 
 *              code will improve how this is handled.
 * 
 *              NOTE: for more details on this process, see the description comment for
 *              H5P_mt_class_t in H5Ppkg_mt.h.
 *
 * Return:      Success: Returns curr_version than is one less than next_version
 * 
 *              Failure: 0
 *
 ****************************************************************************************
 */
uint64_t
H5P__mt_version_check(void *param, uint64_t curr_version, uint64_t next_version)
{
    H5P_mt_type_t    type;
    H5P_mt_class_t * class = NULL;
    H5P_mt_list_t  * list  = NULL;


    type = *((H5P_mt_type_t *) param);

    if ( type == CLASS_TAG )
    {
        class = (H5P_mt_class_t *)param;
    }
    else if ( type == LIST_TAG )
    {
        list = (H5P_mt_list_t *)param;
    }
    else
    {
        fprintf(stderr, "Error with passed type.\n");

        return 0;
    }


    while ( (curr_version + 1) < next_version )
    {
        sleep(1);

        if ( class )
        {
            atomic_fetch_add(&(class->class_wait_for_curr_version_to_inc), 1);
            curr_version = atomic_load(&(class->curr_version));
        }
        else
        {
            atomic_fetch_add(&(list->list_wait_for_curr_version_to_inc), 1);
            curr_version = atomic_load(&(list->curr_version));
        }

#if H5P_MT_SINGLE_THREAD_TESTING
        curr_version++;
#endif
        
    } /* end while ( curr_version + 1 < next_version ) */

    if ( curr_version >= next_version )
    {
        fprintf(stderr, "Versions were not operated in correct version.\n");

        return 0;
    }

    assert((curr_version + 1) == next_version);

    return(curr_version);

} /* H5P__mt_version_check() */



/****************************************************************************************
 * Function:    H5P__inc_thrd_count
 *
 * Purpose:     Increments the count field in the thrd struct in either H5P_mt_class_t
 *              or H5P_mt_list_t, to track the number of threads currently accessing the
 *              class or list structure.
 *
 *              First determine if we're dealing with a class or a list, and then make 
 *              sure it's not opening or closing. As long as neither are TRUE increment
 *              thrd.count and return TRUE, allowing the function that called this one to
 *              continue.
 * 
 *              If opening is TRUE, then we sleep and check again. A class or list while
 *              opening is only to visible briefly to other threads before completing the
 *              opening process and setting opening to FALSE.
 * 
 *              If closing is TRUE, throw an error, a thread should not be accessing a
 *              closing structure.
 * 
 *              NOTE: sleeping is a temporary solution and future iterations of this 
 *              code will improve how this is handled.
 * 
 *              NOTE: for more details on this process, see the description comment for
 *              H5P_mt_class_t in H5Ppkg_mt.h.
 *
 * Return:      SUCCEED/FAIL
 *
 ****************************************************************************************
 */
herr_t
H5P__inc_thrd_count(void *param)
{
    H5P_mt_type_t                type;
    H5P_mt_class_t             * class = NULL;
    H5P_mt_list_t              * list  = NULL;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    hbool_t                      done    = FALSE;
    hbool_t                      opening = FALSE;

    herr_t                       ret_value = SUCCEED;



    type = *((H5P_mt_type_t *) param);

    if ( type == CLASS_TAG )
    {
        class = (H5P_mt_class_t *)param;
    }
    else if ( type == LIST_TAG )
    {
        list = (H5P_mt_list_t *)param;
    }
    else
    {
        fprintf(stderr, "Error with passed type.\n");

        return FAIL;
    }


    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        if ( class )
            thrd = atomic_load(&(class->thrd));
        else
            thrd = atomic_load(&(list->thrd));

        if ( thrd.closing )
        {
            /* update stats */
            if ( class )
                atomic_fetch_add(&(class->num_thrd_closing_flag_set), 1);
            else
                atomic_fetch_add(&(list->num_thrd_closing_flag_set), 1);

            fprintf(stderr, "Trying to access class when closing flag is set.\n");

            return FAIL;
        }
        else if ( thrd.opening )
        {
            /* update stats */
            if ( class )
                atomic_fetch_add(&(class->num_thrd_opening_flag_set), 1);
            else
                atomic_fetch_add(&(list->num_thrd_opening_flag_set), 1);

#if H5P_MT_SINGLE_THREAD_TESTING

            /** 
             * This is for testing that this MT safty net gets trigged while running
             * tests in single thread, to prevent an infinite loop.
             */
            return FAIL;
#else

            sleep(1);

#endif
        }
        else
        {
            update_thrd = thrd;
            update_thrd.count++;

            if ( class )
            {
                if ( ! atomic_compare_exchange_strong(&(class->thrd), 
                                                        &thrd, update_thrd))
                {
                    /* attempt failed, update stats and try again */
                    atomic_fetch_add(&(class->num_thrd_count_update_cols), 1);
                }
                else
                {
                    /* attempt succeded update stats and set done */
                    atomic_fetch_add(&(class->num_thrd_count_update), 1);

                    done = TRUE;
                }
            }
            else
            {
                if ( ! atomic_compare_exchange_strong(&(list->thrd), 
                                                        &thrd, update_thrd))
                {
                    /* attempt failed, update stats and try again */
                    atomic_fetch_add(&(list->num_thrd_count_update_cols), 1);
                }
                else
                {
                    /* attempt succeded update stats and set done */
                    atomic_fetch_add(&(list->num_thrd_count_update), 1);

                    done = TRUE;
                }  
            }

        } /* end else */


    } while ( ! done );

done:

    return(ret_value);

} /* H5P__inc_thrd_count() */



/****************************************************************************************
 * Function:    H5P__dec_thrd_count
 *
 * Purpose:     Decrements the count field in the thrd struct in either H5P_mt_class_t
 *              or H5P_mt_list_t, to track the number of threads currently accessing the
 *              class or list structure.
 *
 *              First determine if we're dealing with a class or a list, and then 
 *              decrement thrd.count and return TRUE, allowing the function that called 
 *              this one to continue.
 * 
 *              We do not check if the struct is opening or closing, because 
 *              H5P__inc_thrd_count() is called before this function so, opening must be
 *              FALSE. Closing cannot be set to TRUE as long as a thrd.count is > 1, so
 *              it also cannot be TRUE until after this function is completed. 
 * 
 *              NOTE: for more details on this process, see the description comment for
 *              H5P_mt_class_t in H5Ppkg_mt.h.
 *
 * Return:      SUCCEED/FAIL
 *
 ****************************************************************************************
 */
herr_t
H5P__dec_thrd_count(void *param)
{
    H5P_mt_type_t                tag;
    H5P_mt_class_t             * class = NULL;
    H5P_mt_list_t              * list  = NULL;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t update_thrd;
    hbool_t                      done    = FALSE;
    hbool_t                      opening = FALSE;

    herr_t                       ret_value = SUCCEED;



    tag = *((H5P_mt_type_t *) param);

    if ( tag == CLASS_TAG )
    {
        H5P_mt_class_t *type = (H5P_mt_class_t *)param;

        class = type;

    }
    else if ( tag == LIST_TAG )
    {
        H5P_mt_list_t *type = (H5P_mt_list_t *)param;

        list = type;
    }
    else
    {
        fprintf(stderr, "Error with passed type.\n");

        ret_value = FAIL;
    }


    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        if ( class )
        {
            thrd = atomic_load(&(class->thrd));
        }
        else
        {
            thrd = atomic_load(&(list->thrd));   
        }


        update_thrd = thrd;
        update_thrd.count--;

        if ( class )
        {
            if ( ! atomic_compare_exchange_strong(&(class->thrd), 
                                                    &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(class->num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(class->num_thrd_count_update), 1);

                done = TRUE;
            }
        }
        else
        {
            if ( ! atomic_compare_exchange_strong(&(list->thrd), 
                                                    &thrd, update_thrd))
            {
                /* attempt failed, update stats and try again */
                atomic_fetch_add(&(list->num_thrd_count_update_cols), 1);
            }
            else
            {
                /* attempt succeded update stats and set done */
                atomic_fetch_add(&(list->num_thrd_count_update), 1);

                done = TRUE;
            }  
        }


    } while ( ! done );


    return(ret_value);

} /* H5P__dec_thrd_count() */



/****************************************************************************************
 * Function:    H5P__inc_ref_count
 *
 * Purpose:     Increments the reference count of a class for either plc or pl, depending
 *              on if the the new struct derived from the parent is a H5P_mt_class_t or a
 *              H5P_mt_list_t. 
 * 
 *              NOTE: for more details on this process, see the description comment for
 *              H5P_mt_class_t in H5Ppkg_mt.h.
 *
 * Return:      SUCCEED/FAIL
 *
 ****************************************************************************************
 */
herr_t 
H5P__inc_ref_count(H5P_mt_class_t *parent, hbool_t plc)
{
    H5P_mt_class_ref_counts_t ref_count;
    H5P_mt_class_ref_counts_t update_ref;
    hbool_t                   done = FALSE;

    herr_t                    ret_value = SUCCEED;

    do
    {    
        ref_count = atomic_load(&(parent->ref_count));
        update_ref = ref_count;

        if ( plc )
        {
            update_ref.plc++;
        }
        else
        {
            update_ref.pl++;
        }

        /* Attempt to atomically update the parent class's count of derived classes */
        if ( ! atomic_compare_exchange_strong(&(parent->ref_count), &ref_count, update_ref) )
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

    return(ret_value);

} /* H5P__inc_ref_count () */



/****************************************************************************************
 * Function:    H5P__dec_ref_count
 *
 * Purpose:     Decrements the reference count of a class for either plc or pl, depending
 *              on if the derived struct being deleted is a H5P_mt_class_t or a
 *              H5P_mt_list_t. 
 * 
 *              NOTE: for more details on this process, see the description comment for
 *              H5P_mt_class_t in H5Ppkg_mt.h.
 *
 * Return:      SUCCEED/FAIL
 *
 ****************************************************************************************
 */
herr_t
H5P__dec_ref_count(H5P_mt_class_t *parent, hbool_t plc)
{
    H5P_mt_class_ref_counts_t ref_count;
    H5P_mt_class_ref_counts_t update_ref;
    hbool_t                   done = FALSE;

    herr_t                    ret_value = SUCCEED;

    do
    {    
        ref_count = atomic_load(&(parent->ref_count));
        update_ref = ref_count;

        if ( plc )
        {
            update_ref.plc--;
        }
        else
        {
            update_ref.pl--;
        }

        /* Attempt to atomically update the parent class's count of derived classes */
        if ( ! atomic_compare_exchange_strong(&(parent->ref_count), &ref_count, update_ref) )
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

    return(ret_value);

} /* H5P__dec_ref_count() */



//#endif
