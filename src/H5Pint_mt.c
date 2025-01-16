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

H5P_mt_prop_t * H5P__search_prop_class(H5P_mt_class_t *class, 
                                       H5P_mt_active_thread_count_t thrd,
                                       H5P_mt_prop_t *prop);

void H5P__find_mod_point(H5P_mt_class_t *class, H5P_mt_prop_t **first_ptr_ptr, 
                    H5P_mt_prop_t **second_ptr_ptr, int32_t *deletes_ptr, 
                    int32_t *nodes_visited_ptr, int32_t *thrd_cols_ptr,
                    H5P_mt_prop_t *target_prop, int32_t *cmp_result_ptr);


//herr_t H5P__insert_prop_list(H5P_mt_prop_t prop, H5P_mt_list_t list);
//herr_t H5P__delete_prop_list(H5P_mt_prop_t prop, H5P_mt_list_t list);
//H5P_mt_prop_t H5P__search_prop_list(H5P_mt_prop_t prop, H5P_mt_list_t list);


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
    /* Trying H5I_INVALID_HID gives warning saying it's an 'int' */
    atomic_store(&(H5P_mt_rootcls_g->id), NULL); 
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
    atomic_init(&(H5P_mt_rootcls_g->class_insert_total_nodes_visited), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_delete_total_nodes_visited), 0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_insert_max_nodes_visited),   0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_delete_max_nodes_visited),   0ULL);

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

    /* H5P__find_mod_point() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_chksum_cols),     0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_name_cols),       0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__find_mod_point__num_calls), 0ULL);
    
    /* H5P__create_prop() function stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_prop_created),      0ULL);
    atomic_init(&(H5P_mt_rootcls_g->H5P__create_prop__num_calls), 0ULL);

    /* H5P_mt_prop_t stats */
    atomic_init(&(H5P_mt_rootcls_g->class_num_props_copied),      0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_num_props_modified),    0ULL);
    atomic_init(&(H5P_mt_rootcls_g->class_max_num_prop_modified), 0ULL);

} /* H5P_init() */



/****************************************************************************************
 * Function:    H5P__create_class
 *
 * Purpose:     Function to create a new property list class (H5P_mt_class_t)
 *
 *              Creates a new property list class, drived from an existing property list
 *              class. The new property list class copies the properties from it's 
 *              parent, with the exception of properties that have a delete_version equal
 *              to or less than curr_version of the parent class.
 *              
 *
 * Return:      Success: Returns a pointer to the new H5P_mt_class_t struct 
 *                       (new property list class).
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
    H5P_mt_class_t             * second_class;
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
            /* num_thrd_closing_set */
            atomic_fetch_add(&(parent->class_num_thrd_opening_flag_set), 1);

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
                /* num_thrd_count_update_cols */
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
        }


    } while ( ! done );


    if ( ! closing )
    {

        /* Allocate memory for the new property list class */

        new_class = (H5P_mt_class_t *)malloc(sizeof(H5P_mt_class_t));
        if ( ! new_class )
        {
            fprintf(stderr, "New class allocation failed.");
            return NULL;
        }

        /* Initialize class fields */
        new_class->tag = H5P_MT_CLASS_TAG;

        parent_id = (hid_t)atomic_load(&(parent->id));
        new_class->parent_id      = parent_id;

        new_class->parent_ptr     = parent;
        new_class->parent_version = parent_version;

        new_class->name = name;
        /* Trying H5I_INVALID_HID gives warning saying it's an 'int' */
        atomic_store(&(new_class->id), NULL);
        new_class->type = type;

        atomic_store(&(new_class->curr_version), 1);
        atomic_store(&(new_class->next_version), 2);

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
                /* update stats */
                /* num_class_ref_count_cols */
            }
            else /* Attempt was successful */
            {
                /* update stats */
                /* num_class_ref_count_update */

                done = TRUE;
            }

        } while ( ! done );

        assert(done);

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
                        /* num_class_fl_insert_cols */

                        retry = TRUE;
                    }
                    else /* The attempt was successfule update stats and mark done */
                    {
                        /* update stats */
                        /* num_class_fl_insert */

                        done = TRUE;
                    }
                } /* end else ( ! fl_class_two ) */

            } while ( ( ! done ) && ( ! retry ) );

            assert( ! ( done && retry ) );

        } while ( retry );

        assert(done);
        assert(!retry);

        thrd = atomic_load(&(new_class->thrd));
        thrd.count--;
        thrd.opening = FALSE;

        atomic_store(&(new_class->thrd), thrd);

        atomic_fetch_sub(&(parent_thrd.count), 1);

    } /* end if ( ! closing )*/

    ret_value = new_class;

    return(ret_value);
   
} /* H5P__create_class() */



/**
 * 
 */
H5P_mt_prop_t *
H5P__copy_lfsll(H5P_mt_prop_t *pl_head, uint64_t curr_version, H5P_mt_class_t *new_class)
{
    H5P_mt_prop_t     * new_head;
    H5P_mt_prop_t     * new_prop;
    H5P_mt_prop_t     * parent_prop;
    H5P_mt_prop_aptr_t  parent_first;
    H5P_mt_prop_aptr_t  parent_next;
    H5P_mt_prop_value_t parent_value;
    uint64_t            phys_pl_len = 0;
    uint64_t            log_pl_len = 0;

    H5P_mt_prop_t * ret_value;

    assert(pl_head);
    assert(pl_head->tag ==  H5P_MT_PROP_TAG);
    assert(pl_head->sentinel == TRUE);


    /* Create the head of LFSLL, the first sentinel node */
    new_head = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! new_head )
        fprintf(stderr, "New property allocation failed."); 
    
    parent_next  = atomic_load(&(pl_head->next));
    parent_value = atomic_load(&(pl_head->value));

    assert(parent_next.ptr);
    assert(parent_next.ptr->tag == H5P_MT_PROP_TAG);
    

    new_head->tag            = H5P_MT_PROP_TAG;

    atomic_store(&(new_head->next), parent_next);

    new_head->sentinel       = TRUE;
    new_head->in_prop_class  = pl_head->in_prop_class;

    atomic_store(&(new_head->ref_count), 0);

    new_head->chksum         = pl_head->chksum;
    new_head->name           = pl_head->name;

    atomic_store(&(new_head->value), parent_value);
    atomic_store(&(new_head->create_version), 1);
    atomic_store(&(new_head->delete_version), 0);


    /* Increment physicaly length */
    phys_pl_len++;    

    /* Loop through the parent LFSLL to copy the properties */
    while ( parent_next.ptr != NULL )
    {
        /* update to the next property */
        parent_prop  = atomic_load(&(parent_next.ptr));
        parent_next  = atomic_load(&(parent_prop->next));
        parent_value = atomic_load(&(parent_prop->value));

        /* If the parent property is a valid entry for the new LFSLL copy it in */
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

            atomic_store(&(new_prop->next), parent_next);

            new_prop->sentinel       = parent_prop->sentinel;
            new_prop->in_prop_class  = parent_prop->in_prop_class;

            atomic_store(&(new_prop->ref_count), 0);

            new_prop->chksum         = parent_prop->chksum;
            new_prop->name           = parent_prop->name;

            atomic_store(&(new_prop->value), parent_value);
            atomic_store(&(new_prop->create_version), 1);
            atomic_store(&(new_prop->delete_version), 0);

            /* If it's not a sentinel node increment logicaly length */
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
 *              H5P__insert_prop_list() functions to create a new property (H5P_mt_prop_t)
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

    /* ref_count is only used if the property is in a property class. */
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
 * Purpose:     Inserts a H5P_mt_prop_t (property struct) into the LFSLL of a 
 *              H5P_mt_class_t (property list class struct) that stores it's properties.
 *
 *              This function firstly calls H5P__create_prop() to create the new
 *              H5P_mt_prop_t struct to store the property. Secondly, this function 
 *              passes pointers to the void function H5P__find_mod_point() which passes 
 *              back the pointers that have been updated to the two properties in the 
 *              LFSLL where the new property will be inserted. Pointers to values needed 
 *              for stats keeping and a value used for comparing the name field of the 
 *              H5P_mt_prop_t structs is passed for if the properties had the same chksum
 *              field are also passed to H5P__find_mod_point().
 * 
 *              If the second_prop pointer points to a property that is equal to the
 *              new_prop pointer then the property to insert already exists in the LFSLL,
 *              and the function frees the H5P_mt_prop_t struct for the new property and 
 *              returns SUCCEED.
 * 
 *              If the new_prop falls between first_prop and second_prop then it is 
 *              atomically inserted between them. 
 * 
 *              NOTE: See the comment description above struct H5P_mt_prop_t in 
 *              H5Pint_mt.h for details on how the LFSLL are sorted.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the class, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_class_t for more
 *              details.
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

    herr_t             ret_value = SUCCEED;

    assert(class);
    assert(class->tag == H5P_MT_CLASS_TAG);

    assert(name);
    assert((size > 0 && value != NULL) || (size == 0));

    /* update stats */
    /* H5P__insert_prop_class__num_calls */

    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        thrd = atomic_load(&(class->thrd));

        if ( thrd.closing )
        {
            /* num_thrd_closing_set */

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
                /* num_thrd_count_update_cols */
            }
            else
            {
                /* num_thrd_count_update */

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            /* num_thrd_opening_set */
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
            sleep(1);

            curr_version = atomic_load(&(class->curr_version));
        }

        if ( curr_version > next_version )
        {
            fprintf(stderr, "\nsimulanious modify, insert, and/or delete was done out of order.");
        }   

        assert(curr_version + 1 ==  next_version);  

        
        /* Now that this thread is good to proceed, create the new property first */
        new_prop = H5P__create_prop(name, value, size, TRUE, next_version);

        assert(new_prop);
        assert(new_prop->tag == H5P_MT_PROP_TAG);


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
            H5P__find_mod_point(class,           
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

/**
 * NOTE: Because of the curr_version next_version check this could probably be 
 * changed to a simpler atomic_store() instead of a atomic_compare_exchange_strong().
 */
#if 1 

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
                        /* num_insert_prop_class_cols */
                        
                        continue;
                    }
                    else /* The attempt was successful update stats mark done */
                    {
                        /* update stats */
                        /* num_insert_prop_class_success */

                        done = TRUE;
                    }
#endif
                } /* end if () */

                /** 
                 * NOTE: as long as H5P__find_mod_point() works correctly and we don't try 
                 * to insert a property that already exists in the LFSLL, this else could
                 * probably be removed.
                 */

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
            } /* if ( first_prop->chksum <= new_prop->chksum ) */

        } while ( ! done );

        assert(done);
        assert(deletes >= 0);
        assert(nodes_visited >= 0);
        assert(thrd_cols >= 0);

        /* update stats */
        /* nodes_visited_prop_insert_class += nodes_visited */
        /* num_insert_prop_class_cols += thrd_cols */

        /* Update physical and logical length of the LFSLL */
        atomic_fetch_add(&(class->log_pl_len), 1);
        atomic_fetch_add(&(class->phys_pl_len), 1);

        /* Update curr_version */
        atomic_store(&(class->curr_version), next_version);

        /* Decrement the number of threads that are currently active in the host struct */
        /* Update the class's current version */
        atomic_store(&(class->curr_version), next_version);

        /* Decrement the number of threads that are currently active in the host struct */
        thrd = atomic_load(&(class->thrd));

        update_thrd = thrd;
        update_thrd.count--;

        if ( ! atomic_compare_exchange_strong(&(class->thrd), &thrd, update_thrd))
        {
            /* update stats */
            /* num_thrd_count_update_cols */
        }
        else
        {
            /* num_thrd_count_update */
            
        }

    }

    return(ret_value);

} /* end H5P__insert_prop_class() */



/****************************************************************************************
 * Function:    H5P__delete_prop_class
 *
 * Purpose:     Deletes a H5P_mt_prop_t (property struct) from the LFSLL of a 
 *              H5P_mt_class_t (property list class struct) that stores it's properties.
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
 *              target_prop, then that is the target_prop and will have it's 
 *              delete_version set to the class's next_version and the curr_version of 
 *              the class will be incremented.
 * 
 *              If the target_prop falls between first_prop and second_prop then 
 *              target_prop is not in the LFSLL of the class.
 * 
 *              NOTE: For multiple threads simultaneiously either modifying, inserting,
 *              or deleting a property in the class, there is an ordering that must be 
 *              followed. See the comment description above H5P_mt_class_t for more
 *              details.
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
    /* H5P__delete_prop_class__num_calls */

    /* Ensure the class isn't opening or closing and increment thread count */
    do 
    {
        thrd = atomic_load(&(class->thrd));

        if ( thrd.closing )
        {
            /* num_thrd_closing_set */

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
                /* num_thrd_count_update_cols */
            }
            else
            {
                /* num_thrd_count_update */

                done = TRUE;
            }
        }
        else
        {
            /* update stats and try again */
            /* num_thrd_opening_set */
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
            sleep(1);

            curr_version = atomic_load(&(class->curr_version));
        }

        if ( curr_version > next_version )
        {
            fprintf(stderr, "\nsimulanious modify, insert, and/or delete was done out of order.");
        }

        assert(curr_version + 1 == next_version);


        /* Finds the property to delete in the class's LFSLL. */

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
                /* If any are true, the target property doesn't exist in the LFSLL */
                if (( second_prop->chksum > target_prop->chksum ) ||
                (( second_prop->chksum == target_prop->chksum ) && ( cmp_result > 0 )) ||
                (( second_prop->chksum == target_prop->chksum ) && ( cmp_result == 0 ) &&
                    ( second_prop->create_version < target_prop->create_version ) ) )
                {
                    /* update stats */
                    /* num_delete_prop_class_prop_nonexistant */
                    fprintf(stderr, "\nTarget property to delete doesn't exist in property class.");

                    done = TRUE;
                }
                /* If true, then we have found target property and can now delete it. */
                else if (( second_prop->chksum == target_prop->chksum ) && 
                        ( cmp_result == 0 ) && 
                        ( second_prop->create_version == target_prop->create_version ) )
                {

                    delete_version = atomic_load(&(second_prop->delete_version));

                    assert(delete_version == 0);

                    /* Set the prop's delete_version */
                    atomic_store(&(target_prop->delete_version), next_version);

                    done = TRUE;

                    /* update stats */
                    /* num_props_delete_version_set */
                } 
                /** 
                 * NOTE: as long as H5P__find_mod_point() works correctly and we don't try 
                 * to delete a property that is already deleted in the LFSLL, this else could
                 * probably be removed.
                 */

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

            } /* end if ( first_prop->chksum <= target_prop->chksum ) */


        } while ( ! done );

        /* Update the class's current version */
        atomic_store(&(class->curr_version), next_version);

        /* Decrement the number of threads that are currently active in the host struct */
        thrd = atomic_load(&(class->thrd));

        update_thrd = thrd;
        update_thrd.count--;

        if ( ! atomic_compare_exchange_strong(&(class->thrd), &thrd, update_thrd))
        {
            /* update stats */
            /* num_thrd_count_update_cols */
        }
        else
        {
            /* num_thrd_count_update */

        }
    }


    return(ret_value);    

} /* end H5P__delete_prop_class() */



/****************************************************************************************
 * Function:    H5P__find_mod_point
 *
 * Purpose:     Iterates the lock free singly linked list (LFSLL) of a H5P_mt_class_t.
 * 
 *              This function is called when the LFSLL of a H5P_mt_class_t is needed to 
 *              be iterated to find the point where a new H5P_mt_prop_t (property struct) 
 *              needs to be inserted, or find the H5P_mt_prop_t that needs to be deleted 
 *              or modified.
 * 
 *              Pointers are passed into this function, one is for the H5P_mt_class_t 
 *              that contains the LFSLL we are iterating. Three H5P_mt_prop_t pointers, 
 *              with the target_prop being the property that will be inserted or is being 
 *              searched for in the LFSLL. The first_ptr_ptr and second_ptr_ptr are the 
 *              pointers that will be passed back to the calling function with the 
 *              properties they need. H5P__find_mod_point uses two H5P_mt_prop_t*, 
 *              first_prop and second_prop to iterate the LFSLL, and compare the entries
 *              to target_prop.  
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
 * 
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
                    H5P_mt_prop_t **first_ptr_ptr, 
                    H5P_mt_prop_t **second_ptr_ptr, 
                    int32_t        *deletes_ptr, 
                    int32_t        *nodes_visited_ptr, 
                    int32_t        *thrd_cols_ptr, 
                    H5P_mt_prop_t  *target_prop,
                    int32_t        *cmp_result_ptr)
{
    bool done             = FALSE;
    int32_t thrd_cols     = 0;
    int32_t deletes       = 0;
    int32_t nodes_visited = 0;
    int32_t cmp_result;
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    H5P_mt_prop_aptr_t next_prop;


    assert(class);
    assert(class->tag == H5P_MT_CLASS_TAG);
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

    /* update stats */
    /* H5P__find_mod_point__num_calls */


    /** 
     * Iterates through the LFSLL of H5P_mt_prop_t (properites), to find the correct
     * location to insert the new property in the H5P_mt_class_t or H5P_mt_list_t. chksum
     * is used to determine insert location by increasing value. 
     * 
     * If there is a chksum collision then property names in lexicographical order. 
     * 
     * If there again is a collision with names, then version number is used in 
     * decreasing order (newest version (higher number) first, oldest version last).
     * 
     */
    assert(!done);

    first_prop = class->pl_head;
    assert(first_prop->sentinel);
    
    next_prop = atomic_load(&(first_prop->next));
    second_prop = atomic_load(&(next_prop.ptr));
    assert(first_prop != second_prop);

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
            /* num_insert_prop_class_chksum_cols */

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
                /* num_insert_prop_class_name_cols */

                /**
                 * If true then deletes and searches will have found their
                 * property if they equal and if they don't then their 
                 * target_prop doesn't exist in the LFSLL. While, inserts
                 * will know where to insert, or the property already exists
                 * and it won't be inserted.
                 */
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
     * Update the pointers passed into the function with the pointers for 
     * the correct positions in the LFSLL, and the stats while iterating.
     */
    *first_ptr_ptr      = first_prop;
    *second_ptr_ptr     = second_prop;
    *thrd_cols_ptr     += thrd_cols;
    *deletes_ptr       += deletes;
    *nodes_visited_ptr += nodes_visited;

    return;

} /* H5P__find_mod_point() */



/**
 * 
 */
herr_t
H5P__clear_mt_class(void)
{
    H5P_mt_class_t             * first_class;
    H5P_mt_class_t             * second_class;
    H5P_mt_class_t             * parent;
    H5P_mt_class_sptr_t          next_class;
    H5P_mt_class_sptr_t          nulls_class_ptr = {NULL, 0ULL};
    H5P_mt_prop_t              * first_prop;
    H5P_mt_prop_t              * second_prop;
    H5P_mt_prop_aptr_t           next_prop;
    H5P_mt_prop_aptr_t           nulls_prop_ptr = {NULL, 0ULL};
    H5P_mt_prop_value_t          nulls_value = {NULL, 0ULL};
    H5P_mt_class_ref_counts_t    ref_count;
    H5P_mt_active_thread_count_t thrd;
    H5P_mt_active_thread_count_t closing_thrd;
    hbool_t                      done = FALSE;
    uint32_t                     num_classes = 0;
    uint32_t                     num_classes_freed = 0;
    _Atomic hid_t              * null_id = NULL;

    herr_t ret_value = SUCCEED;

    first_class = H5P_mt_rootcls_g;
    assert(first_class);
    assert(first_class->tag == H5P_MT_CLASS_TAG);

    num_classes++;

    next_class = atomic_load(&(first_class->fl_next));

    second_class = next_class.ptr;
    assert(second_class);
    assert(second_class->tag == H5P_MT_CLASS_TAG);

    /* Iterate through the free list of classes to get the tail class */
    while ( second_class )
    {
        first_class  = second_class;
        next_class   = atomic_load(&(second_class->fl_next));
        second_class = next_class.ptr;

        assert(first_class);
        assert(first_class->tag == H5P_MT_CLASS_TAG);

        num_classes++;
    } 

    parent = first_class->parent_ptr;
    assert(parent);
    assert(parent->tag == H5P_MT_CLASS_TAG);

    while ( parent )
    {
        ref_count = atomic_load(&(first_class->ref_count));
        
        assert(ref_count.pl  == 0);
        assert(ref_count.plc == 0);

        thrd = atomic_load(&(first_class->thrd));
        
        assert(thrd.count == 0);
        assert(thrd.opening == FALSE);
        assert(thrd.closing == FALSE);

        closing_thrd = thrd;
        closing_thrd.closing = TRUE;

        if ( ! atomic_compare_exchange_strong(&(first_class->thrd), 
                                               &thrd, closing_thrd))
        {
            fprintf(stderr, "\nUpdating class thrd struct for freeing failed.");
        }
        else
        {
            first_class->tag = H5P_MT_CLASS_INVALID_TAG;

            while (first_class->pl_head)
            {
                first_prop = first_class->pl_head;
                assert(first_prop);
                assert(first_prop->tag == H5P_MT_PROP_TAG);

                next_prop = atomic_load(&(first_prop->next));
                second_prop = next_prop.ptr;

                first_class->pl_head = second_prop;

                first_prop->tag = H5P_MT_PROP_INVALID_TAG;

                first_prop->name = NULL;

                atomic_store(&(first_prop->next), nulls_prop_ptr);
                atomic_store(&(first_prop->value), nulls_value);               

                free(first_prop);

                /* update stats */
                /* num_prop_structs_freed */

                atomic_fetch_sub(&(first_class->log_pl_len), 1);
                atomic_fetch_sub(&(first_class->phys_pl_len), 1);

            }

            next_class = atomic_load(&(first_class->fl_next));
            assert( ! next_class.ptr );

            first_class->name = NULL;
            
            atomic_store(&(first_class->id), null_id);

            atomic_store(&(first_class->fl_next), nulls_class_ptr);
            atomic_store(&(first_class->parent_ptr), NULL);

            free(first_class);

            num_classes_freed++;

            /* Decrement parent ref_counts */
            ref_count = atomic_load(&(parent->ref_count));
            ref_count.plc--;

            atomic_store(&(parent->ref_count), ref_count);

            /* Update pointers to free the next class on the free list */
            first_class = parent;
            parent = atomic_load(&(first_class->parent_ptr));
            

        }
    } /* end while ( parent ) */

    return(ret_value);

} /* H5P__clear_mt_class() */



//#endif
