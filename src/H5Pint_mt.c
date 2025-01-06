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

#ifdef H5_HAVE_MULTITHREAD

#define H5P_MT_DEBUG 0


/******************/
/* Local Typedefs */
/******************/

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/
H5P_mt_class_t * H5P__create_class(H5P_mt_class_t *parent, uint64_t parent_version,
                                   const char *name, H5P_plist_type_t type);

int64_t H5P__calc_checksum(const char *name);

H5P_mt_prop_t *
    H5P__create_prop(const char *name, void *value_ptr, size_t value_size, 
                     bool in_prop_class, uint64_t create_version);

herr_t H5P__insert_prop_class(H5P_mt_class_t *class, const char *name, 
                              size_t size, void *value);

herr_t H5P__delete_prop_class(H5P_mt_prop_t *prop, H5P_mt_class_t *class);

H5P_mt_prop_t * H5P__search_prop_class(H5P_mt_prop_t *target_prop, H5P_mt_class_t *class);

void H5P__find_mod_point(H5P_mt_class_t *class, H5P_mt_prop_t **first_ptr_ptr, 
                    H5P_mt_prop_t **second_ptr_ptr, int32_t *deletes_ptr, 
                    int32_t *nodes_visited_ptr, int32_t *thrd_cols_ptr,
                    H5P_mt_prop_t *target_prop, int32_t cmp_result);


herr_t H5P__insert_prop_list(H5P_mt_prop_t prop, H5P_mt_list_t list);
herr_t H5P__delete_prop_list(H5P_mt_prop_t prop, H5P_mt_list_t list);
H5P_mt_prop_t H5P__search_prop_list(H5P_mt_prop_t prop, H5P_mt_list_t list);


/*********************/
/* Package Variables */
/*********************/

hid_t           H5P_CLS_ROOT_ID_g = H5I_INVALID_HID;
H5P_mt_class_t *H5P_mt_rootcls_g    = NULL;

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
    H5P_mt_prop_t  * neg_sentinel;
    H5P_mt_prop_t  * pos_sentinel;

    H5P_mt_class_t * root_class;

    herr_t           ret_value = SUCCEED;


    /* Allocating and initializing the two sentinel nodes for the LFSLL in the class */
    neg_sentinel = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! neg_sentinel )
        printf(stderr, "\nNew property allocation failed."); 

    /* Initalize property fields */
    neg_sentinel->tag = H5P_MT_PROP_TAG;

    atomic_store(&(neg_sentinel->next.ptr),     NULL);
    atomic_store(&(neg_sentinel->next.deleted), FALSE);

    neg_sentinel->sentinel      = TRUE;
    neg_sentinel->in_prop_class = TRUE;

    /* ref_count is only used if the property is in a property class. */
    atomic_store(&(neg_sentinel->ref_count), 0);

    neg_sentinel->chksum = LLONG_MIN;

    neg_sentinel->name = (char *)"neg_sentinel";

    atomic_store(&(neg_sentinel->value.ptr),  0);
    atomic_store(&(neg_sentinel->value.size), 0);
    
    atomic_store(&(neg_sentinel->create_version), 0);
    atomic_store(&(neg_sentinel->delete_version), 0); /* Set to 0 because it's not deleted */


    pos_sentinel = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! pos_sentinel )
        printf(stderr, "\nNew property allocation failed."); 

    /* Initalize property fields */
    pos_sentinel->tag = H5P_MT_PROP_TAG;

    atomic_store(&(pos_sentinel->next.ptr),     NULL);
    atomic_store(&(pos_sentinel->next.deleted), FALSE);

    pos_sentinel->sentinel      = TRUE;
    pos_sentinel->in_prop_class = TRUE;

    /* ref_count is only used if the property is in a property class. */
    atomic_store(&(pos_sentinel->ref_count), 0);

    pos_sentinel->chksum = LLONG_MAX;

    pos_sentinel->name = (char *)"pos_sentinel";

    atomic_store(&(pos_sentinel->value.ptr),  0);
    atomic_store(&(pos_sentinel->value.size), 0);
    
    atomic_store(&(pos_sentinel->create_version), 0);
    atomic_store(&(pos_sentinel->delete_version), 0); /* Set to 0 because it's not deleted */


    atomic_store(&(neg_sentinel->next.ptr), pos_sentinel);


    /* Allocating and Initializing the root property list class */

#if 0 /* root property list class is now a global struct */

    /* Allocate memory for the root property list class */
    root_class = (H5P_mt_class_t *)malloc(sizeof(H5P_mt_class_t));
    if ( ! root_class )
        printf(stderr, "New class allocation failed.");
#endif

    /* Initialize class fields */
    H5P_mt_rootcls_g->tag = H5P_MT_CLASS_TAG;

    H5P_mt_rootcls_g->parent_id      = 0;
    H5P_mt_rootcls_g->parent_ptr     = NULL;
    H5P_mt_rootcls_g->parent_version = 0;

    H5P_mt_rootcls_g->name              = "root";
    atomic_store(&(H5P_mt_rootcls_g->id), H5I_INVALID_HID);
    H5P_mt_rootcls_g->type              = H5P_TYPE_ROOT;

    atomic_store(&(H5P_mt_rootcls_g->curr_version), 1);
    atomic_store(&(H5P_mt_rootcls_g->next_version), 2);

    H5P_mt_rootcls_g->pl_head                    = neg_sentinel;
    atomic_store(&(H5P_mt_rootcls_g->log_pl_len),  0);
    atomic_store(&(H5P_mt_rootcls_g->phys_pl_len), 2);

    atomic_store(&(H5P_mt_rootcls_g->ref_count.pl),      0);
    atomic_store(&(H5P_mt_rootcls_g->ref_count.plc),     0);
    atomic_store(&(H5P_mt_rootcls_g->ref_count.deleted), FALSE);

    atomic_store(&(H5P_mt_rootcls_g->thrd.count),   1);
    atomic_store(&(H5P_mt_rootcls_g->thrd.opening), TRUE);
    atomic_store(&(H5P_mt_rootcls_g->thrd.closing), FALSE);

    atomic_store(&(H5P_mt_rootcls_g->fl_next.ptr), NULL);
    atomic_store(&(H5P_mt_rootcls_g->fl_next.sn),  0);


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
H5P__create_class(H5P_mt_class_t *parent, uint64_t parent_version, 
                  const char *name, H5P_plist_type_t type)
{
    H5P_mt_class_t * new_class;

    H5P_mt_class_t * ret_value = NULL;

    /* Allocate memory for the new property list class */
    new_class = (H5P_mt_class_t *)malloc(sizeof(H5P_mt_class_t));
    if ( ! new_class )
        printf(stderr, "New class allocation failed.");

    /* Initialize class fields */
    new_class->tag = H5P_MT_CLASS_TAG;

    new_class->parent_id      = parent->id;
    new_class->parent_ptr     = parent;
    new_class->parent_version = parent_version;

    new_class->name = name;
    atomic_store(&(new_class->id), H5I_INVALID_HID);
    new_class->type = type;

    atomic_store(&(new_class->curr_version), 1);
    atomic_store(&(new_class->next_version), 2);

    new_class->pl_head = NULL;
    atomic_store(&(new_class->log_pl_len),  0);
    atomic_store(&(new_class->phys_pl_len), 2);

    atomic_store(&(new_class->ref_count.pl),      0);
    atomic_store(&(new_class->ref_count.plc),     0);
    atomic_store(&(new_class->ref_count.deleted), FALSE);

    atomic_store(&(new_class->thrd.count),   1);
    atomic_store(&(new_class->thrd.opening), TRUE);
    atomic_store(&(new_class->thrd.closing), FALSE);

    atomic_store(&(new_class->fl_next.ptr), NULL);
    atomic_store(&(new_class->fl_next.sn),  0);


} /* H5P__create_class() */



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
    H5P_mt_prop_t * new_prop;

    H5P_mt_prop_t * ret_value = NULL;

    assert(name);

    /* Allocate memory for the new property */
    new_prop = (H5P_mt_prop_t *)malloc(sizeof(H5P_mt_prop_t));
    if ( ! new_prop )
        printf(stderr, "New property allocation failed."); 

    /* Initalize property fields */
    new_prop->tag = H5P_MT_PROP_TAG;

    atomic_store(&(new_prop->next.ptr),     NULL);
    atomic_store(&(new_prop->next.deleted), FALSE);

    new_prop->sentinel      = FALSE;
    new_prop->in_prop_class = in_prop_class;

    /* ref_count is only used if the property is in a property class. */
    atomic_store(&(new_prop->ref_count), 0);

    new_prop->chksum = H5P__calc_checksum(name);

    assert(0 != new_prop->chksum);

    new_prop->name = (char *)name;

    atomic_store(&(new_prop->value.ptr),  value_ptr);
    atomic_store(&(new_prop->value.size), value_size);
    
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
 *              This function firstly calls H5P__create_prop() to create the 
 *              H5P_mt_prop_t struct to store the property. Secondly this function passes
 *              pointers to the void function H5P__find_mod_point() which passes back the 
 *              pointers that have been updated to the two properties in the LFSLL where 
 *              the new property will be inserted. Pointers to values needed for stats 
 *              keeping and a value used for comparing the name field of the 
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
H5P__insert_prop_class(H5P_mt_class_t *class, const char *name, size_t size, void *value)
{
    H5P_mt_prop_t * new_prop;
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    uint64_t        curr_version  = 0;
    uint64_t        next_version  = 0;
    int32_t       * deletes       = 0;
    int32_t       * nodes_visited = 0;
    int32_t       * thrd_cols     = 0;
    int32_t         cmp_result    = 0;
    hbool_t         done;

    herr_t          ret_value = SUCCEED;

    assert(class);
    assert(!class->ref_count.deleted);
    assert(class->tag == H5P_MT_CLASS_TAG);

    assert(name);
    assert((size > 0 && value != NULL) || (size == 0));

    /* update stats */
    /* H5P__insert_prop_class__num_calls */

    
    curr_version = atomic_load(&(class->curr_version));

    new_prop = H5P__create_prop(name, value, size, TRUE, curr_version);

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
        H5P__find_mod_point(class,
                            &first_prop,
                            &second_prop,
                            &deletes,
                            &nodes_visited,
                            &thrd_cols,
                            &new_prop,
                            cmp_result);

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
                    printf(stderr, "\nsimulanious modify, insert, and/or delete was done out of order.");
                }   

                assert(curr_version += next_version);             

                /* prep new_prop to be inserted between first_prop and second_prop */
                atomic_store(&(new_prop->next.ptr), second_prop);

                /** Attempt to atomically insert new_prop 
                 * 
                 * NOTE: If this fails, another thread modified the LFSLL and we must 
                 * update stats and restart to ensure new_prop is correctly inserted.
                 */
                if ( ! atomic_compare_exchange_strong(&(first_prop->next.ptr),
                                                        &second_prop, new_prop) )
                {
                    /* update stats */
                    /* num_insert_prop_class_cols */
                    
                    continue;
                }
                else /* The attempt was successful */
                {
                    atomic_fetch_add(&(class->log_pl_len), 1);
                    atomic_fetch_add(&(class->phys_pl_len), 1);

                    atomic_store(&(class->curr_version), next_version);

                    done = TRUE;

                    /* update stats */
                    /* num_insert_prop_class_success */
                }
            }
            else /* second_prop->chksum <= new_prop->chksum */
            {
                if ( cmp_result == 0 && 
                     second_prop->create_version == new_prop->create_version )
                {
                    printf(stderr, "\nNew property already exists in property class.");

                    ret_value = -1;
                }
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

done: 
    if ( ret_value < 0 )
        free(new_prop);

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
 *              This function calls the function H5P__find_mod_point() and passes the 
 *              class the property is being deleted from, two double pointers of 
 *              H5P_mt_prop_t that are used to iterate the LFSLL, variables used for 
 *              stats keeping, and a variable for the result of strcmp() if the chksum
 *              values of the properties are the same.
 * 
 *              If the second_prop pointer points to a property that is equal to the 
 *              target_prop, then that is the target_prop and will have it's 
 *              delete_version set and the curr_version of the class will be incremented.
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
H5P__delete_prop_class(H5P_mt_prop_t *target_prop, H5P_mt_class_t *class)
{
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    uint64_t        curr_version    = 0;
    uint64_t        next_version    = 0;
    uint64_t        delete_version  = 0;
    int32_t       * deletes         = 0;
    int32_t       * nodes_visited   = 0;
    int32_t       * thrd_cols       = 0;
    int32_t         cmp_result      = 0;
    hbool_t         done;

    herr_t          ret_value = SUCCEED;

    assert(class);
    assert(!class->ref_count.deleted);
    assert(class->tag == H5P_MT_CLASS_TAG);

    assert(target_prop);
    assert(target_prop->tag == H5P_MT_PROP_TAG);
    assert(target_prop->delete_version == 0);

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
                            &target_prop,
                            cmp_result);

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
                printf(stderr, "\nTarget property to delete doesn't exist in property class.");

                done = TRUE;
            }
            /** 
             * If true, then we have found target property and can now delete it.
             * 
             * NOTE: The current implementation H5P_mt_prop_t structs (aka properties) 
             * are not truly being deleted. We simply set the property's delete version 
             * to 1+ the current version, meaning any class or list derived from this 
             * class or any version of this class that is >= the properties delete 
             * version will not contain that property.
             */
            else if (( second_prop->chksum == target_prop->chksum ) && 
                     ( cmp_result == 0 ) && 
                     ( second_prop->create_version == target_prop->create_version ) )
            {
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
                    printf(stderr, "\nsimulanious modify, insert, and/or delete was done out of order.");
                }

                assert(curr_version + 1 == next_version);

                delete_version = atomic_load(&(second_prop->delete_version));

                /* If true, then second_prop (thus target_prop) has already been deleted */
                if ( delete_version != 0 )
                {
                    /* update stats */
                    /* num_delete_prop_class_already_deleted */
   
                }
                else /* delete_version == 0 */
                {
                    atomic_store(&(target_prop->delete_version), next_version);
                    atomic_store(&(class->curr_version), next_version);

                    done = TRUE;

                    /* update stats */
                    /* num_props_delete_version_set */
                }
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

        } /* end if ( first_prop->chksum <= target_prop->chksum ) */


    } while ( ! done );
    

} /* end H5P__delete_prop_class() */



/****************************************************************************************
 * Function:    H5P__find_mod_point
 *
 * Purpose:     Iterates the lock free singly linked list (LFSLL) of a H5P_mt_class_t.
 * 
 *              This function is called when the LFSLL of a H5P_mt_class_t is needed to 
 *              be iterated to find the point where an insert needs to happen, or find
 *              the H5P_mt_prop_t (property struct) that needs to be deleted or modified.
 * 
 *              
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
                    int32_t         cmp_result)
{
    bool done = FALSE;
    bool retry = FALSE;
    int32_t thrd_cols = 0;
    int32_t deletes = 0;
    int32_t nodes_visited = 0;
    H5P_mt_prop_t * first_prop;
    H5P_mt_prop_t * second_prop;
    H5P_mt_prop_t * next_prop;


    assert(class);
    assert(class->tag == H5P_MT_CLASS_TAG);
    assert(!class->ref_count.deleted);
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
    /* H5P__insert_prop_class__num_calls */


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
    do
    {
        assert(!done);

        retry = FALSE;

        first_prop = class->pl_head;
        assert(first_prop->sentinel);
        assert(first_prop->tag == H5P_MT_PROP_TAG);
                
        second_prop = atomic_load(&(first_prop->next.ptr));
        assert(first_prop != second_prop);
        assert(second_prop->tag == H5P_MT_PROP_TAG);

        do 
        {

/** 
 * The code in here is work started on logically and/or physically deleting a property
 * in a class or list, and currently is not being used. Though it may be added later,
 * so keeping what was already written.
 * 
 * This iteration we are not deleting properties other than setting a delete_version on
 * the property to be one higher than the class's or list's current version, and then 
 * incrementing the current version of that class or lsit. 
 */
#if NODELPROP

            while (second_prop->next.deleted)
            {
                if ( second_prop->ref_count != 0 )
                {
                    /* Update stats */
                    /* num_prop_marked_deleted_but_still_referenced */


                }
                else /* second_prop->ref_count == 0 */
                {

                    assert(first_prop->next.ptr == second_prop);
                    assert(second_prop->next.ptr != NULL);

                    next_prop = atomic_load(&(second_prop->next.ptr));
                    assert(next_prop->tag == H5P_MT_PROP_TAG);

                    if ( ! atomic_compare_exchange_strong(&(first_prop->next.ptr), 
                                                            &second_prop, next_prop) )
                    {
                        thrd_cols++;
                        retry = TRUE;
                        break;
                    }
                    else 
                    {
                        atomic_fetch_sub(&(class->phys_pl_len), 1);
                        deletes++;
                        nodes_visited++;

                        second_prop = next_prop;
                        next_prop = atomic_load(&(second_prop->next.ptr));

                        assert(first_prop);
                        assert(first_prop->tag == H5P_MT_PROP_TAG);
                        assert(second_prop);
                        assert(second_prop->tag == H5P_MT_PROP_TAG);
                    }
                } /* end else ( second_prop->ref_count == 0 ) */

            } /* end while ( second_prop->next.deleted ) */
#endif

/* retry is a boolean used for the above #if NODELPROP code, and left in for convience */
            if ( ! retry )
            {
                assert(first_prop);
                assert(first_prop->tag == H5P_MT_PROP_TAG);

                assert(second_prop);
                assert(second_prop->tag == H5P_MT_PROP_TAG);

                assert(first_prop->chksum <= target_prop->chksum);

                /**
                 * If true then we're done, and can return the current pointers, because
                 * a new property will be inserted between first_prop and second_prop or
                 * for deletes or searches, it means the target_prop is not in the LFSLL.
                 */
                if ( second_prop->chksum > target_prop->chksum )
                {
                    done = TRUE;
                }
                /* If the chksums equal we need to compare the names */
                else if ( second_prop->chksum == target_prop->chksum )
                {
                    /* update stats */
                    /* num_insert_prop_class_chksum_cols */

                    int32_t cmp_result;
                    hbool_t str_cmp_done = FALSE;

                    do
                    {
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
                            str_cmp_done = TRUE;
                            done         = TRUE;
                            break;
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
                                str_cmp_done = TRUE;
                                done         = TRUE;
                                break;                            
                            }
                        }

                        /**
                         * If we get here then we may need another loop in the do-while.
                         * 
                         * First we check if the next property in the LFSLL has the same
                         * chksum as target_prop. If not then we're done, we have the 
                         * correct position.
                         * 
                         * If they are the same chksum, start the loop over.
                         */
                        next_prop = atomic_load(&(second_prop->next.ptr));

                        assert(next_prop);
                        assert(next_prop->tag == H5P_MT_PROP_TAG);
                        assert(next_prop->chksum >= target_prop->chksum);

                        nodes_visited++;

                        first_prop = second_prop;
                        second_prop = next_prop;

                        if ( next_prop->chksum != target_prop->chksum )
                        {
                            str_cmp_done = TRUE;
                            done         = TRUE;
                        }
                    
                    } while ( ! str_cmp_done );
                        
                } /* else if ( second_prop->chksum == target_prop->chksum ) */

                /* If true, then update the pointers to iterate the LFSLL */
                if ( ! done )
                {
                    first_prop = second_prop;
                    next_prop = atomic_load(&(second_prop->next.ptr));
                    second_prop = next_prop;

                    nodes_visited++;
                }
            } /* end if ( ! retry ) */
        
        } while ( ( ! done ) && ( ! retry ) ); 

        assert( ! ( done && retry ) );

    } while ( retry );

    assert(done);
    assert(retry);

    assert(first_prop->chksum <= target_prop->chksum);
    assert(second_prop->chksum >= target_prop->chksum);

    /**
     * Update the pointers passed into the function with the pointers for 
     * the correct positions in the LFSLL, and the stats while iterating.
     */
    *first_ptr_ptr = first_prop;
    *second_ptr_ptr = second_prop;
    *thrd_cols_ptr += thrd_cols;
    *deletes_ptr += deletes;
    *nodes_visited_ptr += nodes_visited;

    return;

} /* H5P__find_mod_point() */







#if 0 /* old code just wanted to save it for a bit */

    /** 
     * Iterates through the LFSLL of H5P_mt_prop_t (properites), to find the correct
     * location to insert the new property. chksum is used to determine insert location
     * by increasing value. 
     * 
     * If there is a chksum collision then property names in lexicographical order. 
     * 
     * If there again is a collision with names, then version number is used in 
     * decreasing order (newest version (higher number) first, oldest version last).
     */
    while (!done)
    {
        prev_prop == class->pl_head;
        assert(prev_prop->sentinel);
        
        atomic_store(&(curr_prop), prev_prop->next.ptr);
        assert(prev_prop != curr_prop);
        assert(curr_prop->tag == H5P_MT_PROP_TAG);

        if ( curr_prop->chksum > new_prop->chksum )
        {
            /* prep new_prop to be inserted between prev_prop and curr_prop */
            atomic_store(&(new_prop->next.ptr), curr_prop);
            
            /** Attempt to atomically insert new_prop 
             * 
             * NOTE: If this fails, another thread modified the LFSLL and we must 
             * update stats and restart to ensure new_prop is correctly inserted.
             */
            if ( !atomic_compare_exchange_strong(&(prev_prop->next.ptr), 
                                                 &curr_prop, new_prop) )
            {
                /* update stats */
                /* num_insert_prop_class_cols */

                continue;
            }
            /* The attempt was successful. Update lengths and stats */
            else
            {
                atomic_fetch_add(&(class->log_pl_len), 1);
                atomic_fetch_add(&(class->phys_pl_len), 1);

                done = TRUE;

                /* update stats */
                /* num_insert_prop_class_success */
            }
        }
        else if ( curr_prop->chksum == new_prop->chksum )
        {
            int32_t        cmp_result;
            H5P_mt_prop_t *next_prop;
            bool           str_cmp_done = FALSE;

            while ( ! str_cmp_done )
            { 
                /* update stats */
                /* num_insert_prop_class_chksum_cols */

                cmp_result = strcmp(curr_prop->name, new_prop->name);

                /* new_prop is less than curr_prop lexicographically */
                if ( cmp_result > 0 )
                {
                    /* prep new_prop to insert between prev_prop and curr_prop */
                    atomic_store(&(new_prop->next.ptr), curr_prop);

                    /** Attempt to atomically insert new_prop 
                     * 
                     * NOTE: If this fails, another thread modified the LFSLL and we must 
                     * update stats and restart to ensure new_prop is correctly inserted.
                    */
                    if ( !atomic_compare_exchange_strong(&(prev_prop->next.ptr), 
                                                         &curr_prop, new_prop) )
                    {
                        /* update stats */
                        /* num_insert_prop_class_cols */

                        break;

                    }
                    /* The attempt was successful. Update lengths and stats */
                    else 
                    {
                        atomic_fetch_add(&(class->log_pl_len), 1);
                        atomic_fetch_add(&(class->phys_pl_len), 1);

                        done = TRUE;
                        str_cmp_done = TRUE;

                        /* update stats */
                        /* num_insert_prop_class_success */
                    }
                }
                else if ( cmp_result < 0 )
                {

                    atomic_store(&(next_prop), curr_prop->next.ptr);

                    assert(next_prop->chksum >= curr_prop->chksum);

                    /** 
                     * If the next property in the LFSLL doesn't have the same chksum,
                     * then new_prop gets inserted between curr_prop and new_prop since
                     * chksum is used to sort first.
                     */
                    if ( next_prop->chksum != curr_prop->chksum )
                    {
                        /* Prep new_prop to be inserted between curr_prop and next_prop */
                        atomic_store(&(new_prop->next.ptr), next_prop);

                        /** Attempt to atomically insert new_prop 
                         * 
                         * NOTE: If this fails, another thread modified the LFSLL and we must 
                         * update stats and restart to ensure new_prop is correctly inserted.
                        */
                        if ( !atomic_compare_exchange_strong(&(curr_prop->next.ptr), 
                                                            &next_prop, new_prop) )
                        {
                            /* update stats */
                            /* num_insert_prop_class_success */

                            break;

                        }
                        /* The attempt was successful. Update lengths and stats */
                        else
                        {
                            atomic_fetch_add(&(class->log_pl_len), 1);
                            atomic_fetch_add(&(class->phys_pl_len), 1);

                            done = TRUE;
                            str_cmp_done = TRUE;

                            /* update stats */
                            /* num_insert_prop_class_success */
                        }
                    } /* end if ( next_prop->chksum != curr_prop->chksum ) */
  
                } 
                else /* cmp_results == 0 */
                {
                    /* update stats */
                    /* num_insert_prop_class_string_cols */

                    /**
                     * If the name's of curr_prop and new_prop are the same, we must
                     * move on to using version number to determine insert location.
                     */
                    if ( new_prop->create_version > curr_prop->create_version )
                    {
                        atomic_store(&(new_prop->next.ptr), curr_prop);

                        if ( !atomic_compare_exchange_strong(&(prev_prop->next.ptr), 
                                                             &curr_prop, new_prop) )
                        {
                            /* update stats */
                            /* num_insert_prop_class_cols */

                            break;
                        }
                        /* The attempt was successful. Update lengths and stats */
                        else 
                        {
                            atomic_fetch_add(&(class->log_pl_len), 1);
                            atomic_fetch_add(&(class->phys_pl_len), 1);

                            done = TRUE;
                            str_cmp_done = TRUE;

                            /* update stats */
                            /* num_insert_prop_class_success */
                        }
                        
                    } /* end if ( new_prop->create_version > curr_prop->create_version ) */
                    else
                    {
                        /* Property is already in the LFSLL, update stats and exit */
                        if ( new_prop->create_version == curr_prop->create_version )
                        {
                            /* update stats */
                            /* num_insert_prop_class_alread_in_LFSLL */
                            
                            done = TRUE;
                            str_cmp_done = TRUE;

                            break;
                        }
                        
                        atomic_store(&(next_prop), curr_prop->next.ptr);

                        assert(next_prop->chksum >= curr_prop->chksum);

                        if ( new_prop->chksum != next_prop->chksum )
                        {
                            /* Prep new_prop to be inserted between curr_prop and next_prop */
                            atomic_store(&(new_prop->next.ptr), next_prop);

                            /** Attempt to atomically insert new_prop 
                             * 
                             * NOTE: If this fails, another thread modified the LFSLL and we must 
                             * update stats and restart to ensure new_prop is correctly inserted.
                            */
                            if ( !atomic_compare_exchange_strong(&(curr_prop->next.ptr), 
                                                                &next_prop, new_prop) )
                            {
                                /* update stats */
                                /* num_insert_prop_class_success */

                                break;

                            }
                            /* The attempt was successful. Update lengths and stats */
                            else
                            {
                                atomic_fetch_add(&(class->log_pl_len), 1);
                                atomic_fetch_add(&(class->phys_pl_len), 1);

                                done = TRUE;
                                str_cmp_done = TRUE;

                                /* update stats */
                                /* num_insert_prop_class_success */
                            }
                        } /* end if ( new_prop->chksum != next_prop->chksum ) */

                    } /* end else */

                } /* end else ( cmp_results == 0 ) */ 

                /** 
                 * Must update prev_prop and curr_prop to compare next_prop
                 * with new_prop to find the correct insert location.
                 */
                atomic_store(&(prev_prop), curr_prop);
                atomic_store(&(curr_prop), next_prop);


            } /* end while ( ! str_cmp_done ) */
            

        } /* end else if ( curr_prop->chksum == new_prop->chksum ) */
    
    } /* end while (!done) */

#endif

#endif
