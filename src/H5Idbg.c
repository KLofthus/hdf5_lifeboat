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
 * H5Idbg.c - Debugging routines for handling IDs
 */

/****************/
/* Module Setup */
/****************/

#include "H5Imodule.h" /* This source code file is part of the H5I module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5Dprivate.h"  /* Datasets                                 */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5Gprivate.h"  /* Groups                                   */
#include "H5Ipkg.h"      /* IDs                                      */
#include "H5RSprivate.h" /* Reference-counted strings                */
#include "H5Tprivate.h"  /* Datatypes                                */
#include "H5VLprivate.h" /* Virtual Object Layer                     */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/

static int H5I__id_dump_cb(void *_item, void *_key, void *_udata);

/*********************/
/* Package Variables */
/*********************/

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__id_dump_cb
 *
 * Purpose:     Dump the contents of an ID to stderr for debugging.
 *
 *              Updated for multi-thread.  
 *
 *              Note new code to grab the global mutex for the duration of 
 *              call if it is not already held by the current thread.
 *
 * Return:      H5_ITER_CONT (always)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__id_dump_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    hbool_t                 have_global_mutex = TRUE;  /* Trivially true in single thread builds */
    hbool_t                 drop_global_mutex = FALSE; /* Trivially false in single thread builds */
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_id_info_t       *id_info_ptr   = (H5I_mt_id_info_t *)_item; /* Pointer to the ID node */
    H5I_type_t              type          = *(H5I_type_t *)_udata;  /* User data */
    const H5G_name_t       *path          = NULL;                   /* Path to file object */
    void                   *object        = NULL;                   /* Pointer to VOL connector object */

    FUNC_ENTER_PACKAGE_NOERR

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

        /* eventually, H5TS_have_mutex() should never fail.  But until this 
         * is true, trigger an assertion failure if it does.
         */
        assert(FALSE);

#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

    if ( ! have_global_mutex ) {

        H5_API_LOCK
        drop_global_mutex = TRUE;
    }

    info_k = atomic_load(&(id_info_ptr->k));

    fprintf(stderr, "         id = %" PRIdHID "\n", id_info_ptr->id);
    fprintf(stderr, "         count = %u\n", info_k.count);
    fprintf(stderr, "         obj   = 0x%8p\n", info_k.object);
    fprintf(stderr, "         marked = %d\n", info_k.marked);

    /* Get the group location, so we get get the name */
    switch (type) {
        case H5I_GROUP: {
            const H5VL_object_t *vol_obj = (const H5VL_object_t *)info_k.object;

            object = H5VL_object_data(vol_obj);
            if (H5_VOL_NATIVE == vol_obj->connector->cls->value)
                path = H5G_nameof(object);
            break;
        }

        case H5I_DATASET: {
            const H5VL_object_t *vol_obj = (const H5VL_object_t *)info_k.object;

            object = H5VL_object_data(vol_obj);
            if (H5_VOL_NATIVE == vol_obj->connector->cls->value)
                path = H5D_nameof(object);
            break;
        }

        case H5I_DATATYPE: {
            const H5T_t *dt = (const H5T_t *)info_k.object;

            H5_GCC_CLANG_DIAG_OFF("cast-qual")
            object = (void *)H5T_get_actual_type((H5T_t *)dt);
            H5_GCC_CLANG_DIAG_ON("cast-qual")

            path = H5T_nameof(object);
            break;
        }

        /* TODO: Maps will have to be added when they are supported in the
         *       native VOL connector.
         */
        case H5I_MAP:

        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_FILE:
        case H5I_DATASPACE:
        case H5I_ATTR:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_SPACE_SEL_ITER:
        case H5I_EVENTSET:
        case H5I_NTYPES:
        default:
            break; /* Other types of IDs are not stored in files */
    }

    if (path) {
        if (path->user_path_r)
            fprintf(stderr, "                user_path = %s\n", H5RS_get_str(path->user_path_r));
        if (path->full_path_r)
            fprintf(stderr, "                full_path = %s\n", H5RS_get_str(path->full_path_r));
    }

    if ( drop_global_mutex ) {

        H5_API_UNLOCK
    }

    FUNC_LEAVE_NOAPI(H5_ITER_CONT)
} /* end H5I__id_dump_cb() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_dump_cb
 *
 * Purpose:     Dump the contents of an ID to stderr for debugging.
 *
 * Return:      H5_ITER_CONT (always)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__id_dump_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5I_id_info_t    *info   = (H5I_id_info_t *)_item; /* Pointer to the ID node */
    H5I_type_t        type   = *(H5I_type_t *)_udata;  /* User data */
    const H5G_name_t *path   = NULL;                   /* Path to file object */
    void             *object = NULL;                   /* Pointer to VOL connector object */

    FUNC_ENTER_PACKAGE_NOERR

    fprintf(stderr, "         id = %" PRIdHID "\n", info->id);
    fprintf(stderr, "         count = %u\n", info->count);
    fprintf(stderr, "         obj   = 0x%8p\n", info->object);
    fprintf(stderr, "         marked = %d\n", info->marked);

    /* Get the group location, so we get get the name */
    switch (type) {
        case H5I_GROUP: {
            const H5VL_object_t *vol_obj = (const H5VL_object_t *)info->object;

            object = H5VL_object_data(vol_obj);
            if (H5_VOL_NATIVE == vol_obj->connector->cls->value)
                path = H5G_nameof(object);
            break;
        }

        case H5I_DATASET: {
            const H5VL_object_t *vol_obj = (const H5VL_object_t *)info->object;

            object = H5VL_object_data(vol_obj);
            if (H5_VOL_NATIVE == vol_obj->connector->cls->value)
                path = H5D_nameof(object);
            break;
        }

        case H5I_DATATYPE: {
            const H5T_t *dt = (const H5T_t *)info->object;

            H5_GCC_CLANG_DIAG_OFF("cast-qual")
            object = (void *)H5T_get_actual_type((H5T_t *)dt);
            H5_GCC_CLANG_DIAG_ON("cast-qual")

            path = H5T_nameof(object);
            break;
        }

        /* TODO: Maps will have to be added when they are supported in the
         *       native VOL connector.
         */
        case H5I_MAP:

        case H5I_UNINIT:
        case H5I_BADID:
        case H5I_FILE:
        case H5I_DATASPACE:
        case H5I_ATTR:
        case H5I_VFL:
        case H5I_VOL:
        case H5I_GENPROP_CLS:
        case H5I_GENPROP_LST:
        case H5I_ERROR_CLASS:
        case H5I_ERROR_MSG:
        case H5I_ERROR_STACK:
        case H5I_SPACE_SEL_ITER:
        case H5I_EVENTSET:
        case H5I_NTYPES:
        default:
            break; /* Other types of IDs are not stored in files */
    }

    if (path) {
        if (path->user_path_r)
            fprintf(stderr, "                user_path = %s\n", H5RS_get_str(path->user_path_r));
        if (path->full_path_r)
            fprintf(stderr, "                full_path = %s\n", H5RS_get_str(path->full_path_r));
    }

    FUNC_LEAVE_NOAPI(H5_ITER_CONT)
} /* end H5I__id_dump_cb() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_dump_ids_for_type
 *
 * Purpose:     Dump the contents of a type to stderr for debugging.
 *
 * Return:      SUCCEED/FAIL
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track
 *              the number of threads in H5I.  If
 *              H5I_dump_ids_for_type() is ever called from within
 *              H5I, we will need to add a boolean prameter to control
 *              the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_dump_ids_for_type(H5I_type_t type)
{
    H5I_mt_type_info_t *type_info_ptr = NULL;

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    fprintf(stderr, "Dumping ID type %d\n", (int)type);
    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( type_info_ptr ) {

        H5I_mt_id_info_t *id_info_ptr = NULL;
        unsigned long long int id;
        void * value;

        /* Header */
        fprintf(stderr, "     init_count = %u\n", atomic_load(&(type_info_ptr->init_count)));
        fprintf(stderr, "     reserved   = %u\n", type_info_ptr->cls->reserved);
        fprintf(stderr, "     id_count   = %llu\n", (unsigned long long)atomic_load(&(type_info_ptr->id_count)));
        fprintf(stderr, "     nextid     = %llu\n", (unsigned long long)atomic_load(&(type_info_ptr->nextid)));

        /* List */
        if ( atomic_load(&(type_info_ptr->id_count)) > 0 ) {

            fprintf(stderr, "     List:\n");

            /* Normally we care about the callback's return value
             * (H5I_ITER_CONT, etc.), but this is an iteration over all
             * the IDs so we don't care.
             *
             * XXX: Update this to emit an error message on errors?
             */
            fprintf(stderr, "     (HASH TABLE)\n");

            if ( lfht_get_first(&(type_info_ptr->lfht), &id, &value) ) {

                do {
                    id_info_ptr = (H5I_mt_id_info_t *)value;

                    H5I__id_dump_cb((void *)id_info_ptr, NULL, (void *)&type);

                } while (lfht_get_next(&(type_info_ptr->lfht), id, &id, &value));
            }
        }
    }
    else {

        fprintf(stderr, "Global type info/tracking pointer for that type is NULL\n");
    }

    H5I__exit();

    FUNC_LEAVE_NOAPI(SUCCEED)

} /* end H5I_dump_ids_for_type() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dump_ids_for_type
 *
 * Purpose:     Dump the contents of a type to stderr for debugging.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_dump_ids_for_type(H5I_type_t type)
{
    H5I_type_info_t *type_info = NULL;

    FUNC_ENTER_NOAPI_NOERR

    fprintf(stderr, "Dumping ID type %d\n", (int)type);
    type_info = H5I_type_info_array_g[type];

    if (type_info) {

        H5I_id_info_t *item = NULL;
#ifdef H5_HAVE_MULTITHREAD
        unsigned long long int id;
        void * value;
#else /* H5_HAVE_MULTITHREAD */
        H5I_id_info_t *tmp  = NULL;
#endif /* H5_HAVE_MULTITHREAD */

        /* Header */
        fprintf(stderr, "     init_count = %u\n", type_info->init_count);
        fprintf(stderr, "     reserved   = %u\n", type_info->cls->reserved);
        fprintf(stderr, "     id_count   = %llu\n", (unsigned long long)type_info->id_count);
        fprintf(stderr, "     nextid        = %llu\n", (unsigned long long)type_info->nextid);

        /* List */
        if (type_info->id_count > 0) {
            fprintf(stderr, "     List:\n");
            /* Normally we care about the callback's return value
             * (H5I_ITER_CONT, etc.), but this is an iteration over all
             * the IDs so we don't care.
             *
             * XXX: Update this to emit an error message on errors?
             */
            fprintf(stderr, "     (HASH TABLE)\n");
#ifdef H5_HAVE_MULTITHREAD
            if ( lfht_get_first(&(type_info->lfht), &id, &value) ) {

                do {
                    item = (H5I_id_info_t *)value;

                    H5I__id_dump_cb((void *)item, NULL, (void *)&type);

                } while (lfht_get_next(&(type_info->lfht), id, &id, &value));
            }
#else /* H5_HAVE_MULTITHREAD */
            HASH_ITER(hh, type_info->hash_table, item, tmp)
            {
                H5I__id_dump_cb((void *)item, NULL, (void *)&type);
            }
#endif /* H5_HAVE_MULTITHREAD */
        }
    }
    else
        fprintf(stderr, "Global type info/tracking pointer for that type is NULL\n");

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5I_dump_ids_for_type() */

#endif /* H5_HAVE_MULTITHREAD */

