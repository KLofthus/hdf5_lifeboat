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
 * Purpose: This file contains declarations which are visible only within
 *          the H5P package.  Source files outside the H5P package should
 *          include H5Pprivate.h instead.
 */


#include <assert.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdatomic.h>

#ifndef H5Ppkg_mt_H
#define H5Ppkg_mt_H


/* Get package's private header */
#include "H5Pprivate.h"

/* Other private headers needed by this file */
#include "H5SLprivate.h" /* Skip lists				*/


/**************************/
/* Package Private Macros */
/**************************/

/****************************/
/* Package Private Typedefs */
/****************************/


/* Structures for Properties */

typedef struct H5P_mt_prop_t H5I_mt_prop_t; /* Forward declaration */

/****************************************************************************************
 *
 * Structure:   H5P_mt_prop_aptr_t
 *
 * Description:
 *
 * Struct H5P_mt_prop_aptr_t is a structure designed to contain a pointer to an instance
 * of H5P_mt_prop_t and a deleted flag in a single atomic structure. This is necessary,
 * as instances of H5P_mt_prop_t will typically appear in lock free singly linked lists.
 *
 * For correct operation, these lists require the next pointer and the deleted flag to
 * be accessed and modified in a single atomic operations.
 *
 * With padding, this structure is 128 bits, which allows true atomic operation on
 * many (most?) modern CPUs. However, it this becomes a problem, we can obtain the
 * same effect by stealing the low order bit of the pointer for a deleted bit -- which
 * works on all CPU / C compiler combinations I have tried.
 *
 * Fields:
 * 
 * ptr (struct H5P_mt_prop_t):
 *      Pointer to an instance of H5P_mt_prop_t, or NULL.
 * 
 * deleted (bool):
 *      Boolean flag.  If this instance of H5P_mt_prop_aptr_t appears as a field
 *      in an instance of H5P_mt_prop_t and this flag is TRUE, the instance of 
 *      H5P_mt_prop_t is logically deleted, and not if the flag is FALSE.
 * 
 * dummy_bool_1:
 * dummy_bool_2:
 * dummy_bool_3: The dummy_bool fields exist to pad H5P_mt_prop_aptr_t out to 128 bits  
 *
 ****************************************************************************************
 */
typedef struct H5P_mt_prop_aptr_t 
{
    H5P_mt_prop_t       * ptr;

    bool                  deleted;

    bool                  dummy_bool_1;
    bool                  dummy_bool_2;
    bool                  dummy_bool_3;

} H5P_mt_prop_aptr_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_value_t
 * 
 * Description:
 * 
 * Properties in a property list consist of a void pointer and a size.  To avoid race 
 * conditions, the size and poitner must be set atomically.  This structure exits to
 * facilitate this.
 * 
 * Fields:
 * 
 * ptr (void *):
 *      Void pointer to the value, or NULL if the value is undefined.
 * 
 * size (size_t):
 *      size_t containing the size of the buffer pointed to by the ptr field, or zero
 *      if ptr is NULL.
 * 
 * Note: The above fields will usually have a total size of 128 bits. However, since
 * the size of size_t is not fixed across all 64 bit compilers, there is the potential
 * for occult failures in atomic_compare_exchange_strong() when garbage gets into
 * the un-used space in the structure. (recall that the sum of the sizes of the fields
 * of a structure need not equal the allocation size of the structure.)
 *
 * For now, it should be sufficient to assert that sizeof(size_t) = 8.
 * However, we will have to deal with the issue eventually. For example, I have read
 * that size_t is a 32 bit value on at least some compilers targeting Windows.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_prop_value_t
{
    void * ptr;
    size_t size;

} H5P_mt_prop_value_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_prop_t
 * 
 * Description:
 * 
 * Struct H5P_mt_prop_t is a revised version of H5_genprop_t designed for use in a
 * multi-thread safe version of H5P. The data structures supporting property lists
 * are lock free to the extent practical, and thus instances of H5P_mt_prop_t will
 * typically appear in lock free singly linked list.
 *
 * Further, to support versioning in property list classes,instances of H5P_mt_prop_t
 * in property list classes maintain reference counts of the number of property lists
 * that refer to them for default values, the revision number at which they inserted
 * into the containing property list class or property list, and (if deleted) the
 * revision number at which which the deletion took place.
 *
 * Fields:
 *
 * tag (uint32_t): 
 *      Integer value set to H5P_MT_PROP_TAG when an instance of H5P_mt_prop_t
 *      is allocated from the heap, and to H5P_MT_PROP_INVALID_TAG just before
 *      it is released back to the heap. The field is used to validate pointers
 *      to instances of H5P_mt_prop_t,
 *
 *
 * next (_Atomic H5P_mt_prop_aptr_t): 
 *      Atomic instance of H5P_mt_prop_aptr_t, which combines a pointer to the
 *      next element of the lock free singly linked list with a deleted flag.
 *      If there is no next element, or if the instance of H5P_mt_prop_t is
 *      not in a LFSLL, this field should be set to {NULL, false}.
 *
 * sentinel (bool): 
 *      Boolean flag. When set, this instance of H5P_mt_prop_t is a sentinel
 *      node in the lock free singly linked list -- and therefore does not
 *      represent a property.
 *
 * in_prop_class (bool): 
 *      Boolean flag that is set to TRUE if this instance of H5P_mt_prop_t
 *      resides in a property list class, and false otherwise. Note that
 *      the ref_count field is un-used if this field is false.
 *
 * ref_count (_Atomic uint64_t): 
 *      Atomic integer used to track the number of property list properties
 *      that point to this instance of H5P_mt_prop_t. This field must be
 *      zero if in_prop_class is false.
 *
 *      Note that this ref_count is only increased when a new property list
 *      is created, and is decremented when the property list is discarded.
 *
 *      Thus this instance of H5P_mt_prop_t can be safely deleted if:
 *
 *      1) the ref count drops to zero, and
 *
 *      2) this property has been either deleted or superseded
 *      in the property list class.
 *
 *
 * Property Chksum, Name & Value:
 * 
 * Instances of H5P_mt_prop_t will typically appear in lock free singly linked lists,
 * which must be sorted by property name, and then by decreasing create_version (i.e.
 * highest create_version first).
 *
 * The lock free singly linked list used to store most instances of H5P_mt_prop_t
 * requires sentinels at the beginning and end of the list with values (conceptually)
 * of negative and positive infinity respectively. This is a bit awkward with strings,
 * so for this reason, the (name, creation_version) key is augmented with a 32 bit
 * checksum on the name, converting the key to a (chksum, name, create_version)
 * triplet. 
 * 
 * NOTE: that the check sum is a 32 bit unsigned value, which is stored in an int64_t. 
 * Thus we can use LLONG_MIN and LLONG_MAX as our negative and positive infinity 
 * respectively.
 *
 * The addition of the chksum changes the sorting order to checksum, name, and
 * then decreasing create_version. This ordering, along with the delete_version
 * field, allows us to operate on specific versions of a property list classes and
 * property lists -- thus allowing concurrent operations without introducing
 * corruption.
 *
 * Fields:
 * 
 * chksum (int64_t): 
 *      int64_t containing a 32 bit checksum computed on the name field
 *      below, or LLONG_MIN or LLONG_MAX if either the head or tail
 *      sentinel in the lock free SLL respectively.
 *
 *      Since this field is constant for the life of the instance of
 *      H5P_mt_prop_t, and is set before the instance is visible to more
 *      than one thread, it need not be atomic.
 *
 * name (char *): 
 *      Pointer to a dynamically allocated string containing the name of the
 *      property. This field is not atomic, as the string should be allocated,
 *      and initialized, and the name field set before the instance of
 *      H5P_mt_prop_t is visible to more than one thread. Since the name
 *      is constant for the life of the instance of H5P_mt_prop_t, this should
 *      be sufficient for thread safety.
 * 
 * value (_Atomic H5P_mt_prop_value_t): 
 *      Atomic structure containing the pointer to the buffer containing the
 *      value of the property, and its size.
 *
 * create_version (_Atomic uint64_t): 
 *      Atomic integer which is set to the version of the containing
 *      property list class or property list in which this property was
 *      inserted.
 *
 * delete_version (_Atomic uint64_t):  
 *      Atomic integer which is set to the version of the containing
 *      property list class or property list in which the property was
 *      deleted. If the property has not been deleted, this field contains
 *      zero.
 * 
 * Property Callback Functions:
 *
 * These fields are currently left out to keep the structure more simple for early 
 * testing.
 * 
 ****************************************************************************************
 */

#define H5P_MT_PROP_TAG         0x1010 /* 4112 */
#define H5P_MT_PROP_INVALID_TAG 0x2020 /* 8224 */

typedef struct H5P_mt_prop_t
{
    uint32_t                    tag;
    
    _Atomic H5P_mt_prop_aptr_t  next;

    bool                        sentinel;

    bool                        in_prop_class;
    _Atomic uint64_t            ref_count;

    int64_t                     chksum;
    char                      * name;
    _Atomic H5P_mt_prop_value_t value;
    
    _Atomic uint64_t            create_version;
    _Atomic uint64_t            delete_version;

    /* Callbacks are currently left out for early testing */

} H5P_mt_prop_t;




/* Structures for Property List Classes  */


/****************************************************************************************
 * 
 * Structure: H5P_mt_active_thread_count_t
 * 
 * Description:
 *
 * Struct H5P_mt_active_thread_count_t is structure designed to contain a counter of the
 * number of threads currently active in the host structure and opening and closing flags
 * in a single atomic structure. The objectives are to prevent access to the containing
 * structure during setup, and to provide a mechanism for delaying the discard of the
 * containing structure until all threads currently active in the structure have exited.
 *
 * The possibility of access to a property list class or property list that is in the
 * process of being set stems from two points.
 *
 * First, some callbacks that must be called during setup require the ID of the host
 * property list. This requires that the property list be inserted into the index,
 * which in turn makes the incomplete property list accessible to other threads.
 *
 * Even in the absence of these callbacks, both property list classes and property lists
 * must be inserted into the index before they are completely set up. While it is
 * improbable, this makes them accessible to other threads via iterations on the host
 * indexes.
 *
 * To prevent this, the opening flag in the contained instance of
 * H5P_mt_active_thread_count_t is initialized to TRUE, and not set to FALSE until setup
 * completes. Any thread that wants to access the host property list much check this flag
 * on entry, and fail if it is TRUE.
 *
 * In principle, it should be impossible for any thread to access a property list class
 * or property list that is in the process of being taken down. However, it seems prudent
 * to have a mechanism to detect the case where it does, and to manage it gracefully. 
 * Note that in debug builds we should throw an assertion failure whenever a circumstance
 * thatis forbidden occurs. One could argue that in production builds we should log the 
 * issue and handle it gracefully – I am not sure I agree, but this is a discussion for 
 * another time.
 *
 * In the typical case of a thread that reads or modifies the host data structure, it 
 * must first do an atomic fetch on the associated instance of 
 * H5P_mt_active_thread_count_t and fail if either the opening or closing flag is set. If
 * neither flag is set, it must increment the thread counter in the local copy, and 
 * attempt to overwrite the shared copy with the local copy using a call to 
 * atomic_compare_exchange_strong(). If this fails, it must repeat the procedure until 
 * successful, or until the closing flag is set. When the thread is done with host data 
 * structure, it must again load the associated instance of H5P_mt_active_thread_count_t,
 * decrement the thread count in the local copy, and attempt to overwrite the shared copy
 * with the local copy with another call to atomic_compare_exchange_strong() – repeating 
 * the procedure until successful. Note that the flags are ignored in this case.
 *
 * Similarly, a thread that is about to discard the host structure must first do an 
 * atomic fetch on the associated instance of H5P_mt_active_thread_count_t and fail 
 * either the opening or closing flag is set – preferably with an assertion failure. If 
 * the closing flag is not set, it must set it in the local copy, and attempt to 
 * overwrite the shared copy with the local copy using a call to 
 * atomic_compare_exchange_strong(). If this fails, it must repeat the procedure until 
 * successful. Once the closing flag is set, it must verify that no threads are active in
 * the host structure – either throwing an error or waiting until the thread count drops 
 * to zero as appropriate.
 * 
 * Fields:
 * 
 * count (uint64_t):
 *      Number of threads currently active in the host structure.
 * 
 * opening (bool):
 *      Boolean flag that is set to TRUE while the host property list class or property
 *      list is in the process of being setup. It must be set to FALSE once setup is 
 *      complete.
 * 
 * closing (bool):
 *      Boolean flag that is set to TRUE iff the host structure is about to be discarded.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_active_thread_count_t
{
    uint64_t    count;
    bool        opening;
    bool        closing;

} H5P_mt_active_thread_count_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_class_ref_counts_t
 * 
 * Description:
 *
 * Property list classes (instances on H5P_mt_class_t in the new implementation) need to
 * maintain reference counts on the number of derived property list classes, the number
 * derived property lists, and whether the property list class still exists in the index.
 *
 * One can argue that these three ref counts should be combined into a single reference
 * count. For now, at least, I am inclined to retain this design feature for the
 * following reasons:
 *
 * First, maintaining these reference counts separately seems likely to have some
 * debugging benefits, in that it provides more information about the current derivatives
 * of the property list class than a single reference count.
 *
 * Second, given that we must replicate the behavior of the current implementation quite
 * closely in the single thread case, it seems to me that gratuitous design changes
 * should be avoided.
 *
 * This, however raises the issue of how to keep the different reference counts
 * synchronized, and in particular, how to avoid the case in which the combined
 * reference counts drop to zero, discard is initialized, and another thread comes
 * in and tries to increment one of the reference counts.
 *
 * This is solved by combining the various reference counts into a single atomic 
 * structure, and not allowing any reference count to be incremented once all the 
 * reference counts have dropped to zero.
 *
 * This structure is intended to fulfill this role. The individual fields are discussed
 * below. Observe that the size of the structure is less that 128 bits, which should
 * allow true atomic operation on most modern machines. 
 * 
 * Fields:
 * 
 * pl (uint64_t):
 *      Number of property lists immediately derived from this property list class, and 
 *      still extant.
 * 
 * plc (uint32_t):
 *      Number of property list classes immediately derived from this property list 
 *      class, and still extant.
 * 
 * deleted (bool):
 *      Boolean flag indicating whether this property list class has been deleted from 
 *      the index. This field is set to FALSE on creation, and set to TRUE when the 
 *      reference count on the property list class in the index drops to zero.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_class_ref_counts_t
{
    uint64_t pl;
    uint32_t plc;
    bool     deleted;

} H5P_mt_class_ref_counts_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_class_sptr_t
 * 
 * Description:
 * 
 * The H5P_mt_class_sptr_t combines a pointer to H5P_mt_class_t with a serial number
 * in a 128 bit package. It is intended to allow instances of H5P_mt_class_t to be
 * linked together in a singly linked list – specifically in a free list.
 *
 * This combination of a pointer and a serial number is needed to prevent ABA
 * bugs.
 * 
 * Fields:
 * 
 * ptr (H5P_mt_class_t *):
 *      Pointer to an instance of H5P_mt_class_t.
 * 
 * sn (uint64_t):
 *      Serial number that should be incremented by 1 each time a new value is assigned
 *      to ptr.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_class_t H5P_mt_class_t; /* Forward declaration */

typedef struct H5P_mt_class_sptr_t
{
    H5P_mt_class_t * ptr;
    uint64_t         sn;

} H5P_mt_class_sptr_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_class_t
 * 
 * Description:
 * 
 * Revised version of H5P_genclass_t designed for use in a multi-thread safe version
 * of the HDF5 property list module (H5P).
 *
 * At the conceptual level, a property list class is simply a template for constructing
 * a default version of a member of a class of property lists, with the default 
 * properties, each with that property's default value.
 *
 * This simple concept is complicated by the requirement that modifications to property
 * list classes not effect preexisting derived property lists or property list classes.
 *
 * The single thread version of H5P addressed this problem by duplicating property list
 * classes with derived property lists and/or property list classes whenever they are
 * modified. The modification is applied to the duplicate, and the duplicate replaces
 * the base version in the index. This approach has a number of problems, not the
 * least being that it makes it possible for multiple versions of the property list
 * class to exist in the index, and thus be visible to the user.
 *
 * Instead, the multi-thread version of property list classes maintains back versions
 * of all properties tagged with the property list class version in which they were
 * created (and possibly deleted). All properties are ref counted with the number of
 * properties in derived property lists that refer to them for default values. Since
 * the ref count on a version of a property can only be incremented when that property
 * version appears in the current version of the property list class, this means that
 * back versions of properties may be safely discarded once their ref counts drop to zero.
 *
 * Note that this no longer need be the case if we allow back version of property list
 * classes to be visible outside of H5P. Note also that this is a semantic change in
 * the H5P API from the single thread version, albeit an obscure one, and to my thinking,
 * very much in the right direction.
 *
 * More importantly, if all operations on a property list address a specific version,
 * and all modifications are effectively atomic, concurrent operations can occur without
 * the potential for data corruption as long as all modifications trigger an increment
 * of the property list class version number.
 *
 * Making modifications to a property list class effectively atomic is slightly tricky,
 * as to give an obvious example, inserting a new property and incrementing the
 * version number can't be made atomic without heroic measures. However, by
 * targeting every operation at a specific version, we can make changes in progress
 * effectively invisible since new or modified properties are represented by new
 * instances of H5P_mt_prop_t with creation property list class versions higher than
 * the current version, and thus don't become visible until the property list class
 * version is incremented to the point that they become visible.
 *
 * However, if multiple modifications to the property list class are in progress
 * simultaneously, there is a race condition between the issue of a new version
 * number to be used to tag a modification, and the increment of the property list
 * class version number when the modification completes.
 *
 * Conceptually, this can be handled by waiting to increment the version number until
 * the current version number is one less than the issued version number. Use of a
 * condition variable is the obvious solution here -- but we will sleep and try again
 * until we settle on a threading package.
 *
 * There is also a potential race condition if a delete and either a modify or an
 * insert on a single property is in progress at the same time. In this case, the
 * operations must proceed in target version issue order.
 *
 * A second fundamental difference between the single thread and the multi-thread
 * implementations of the property list class, is that properties are stored on a
 * lock free singly linked list (LFSLL) instead of a skip list. This LFSLL list is
 * sorted first by a hash on the property name, second by property name (to allow for
 * hash collisions), and finally by creation version in decreasing order.
 *
 * Given that property lists are typically short (less that 25 properties), and that
 * the LFSLL will be searched only on property insert, delete, or modification, the
 * LFSLL should be near optimal for this application. However, if the number of
 * properties (or back versions of same) balloon and cause performance issues, it
 * will be easy enough to replace the LFSLL with a lock free hash table.
 * 
 * Finally, for code simplicity, properties inherited from the parent property list
 * class are copied into the LFSLL of properties in the derived property list class.
 * There is nothing magic about this, and we can revert to the old system if there
 * is some reason to do so.
 *
 * Note, however, that it is still necessary for property list classes to maintain
 * pointers to their parent property list classes due to the requirement that
 * all close functions in ancestor property list classes be called on close.
 *
 * With this outline of H5P_mt_class_t in hand, we now address individual fields.
 *
 *                                                          JRM -- 5/22/24
 * 
 * Fields:
 * 
 * tag (uint32_t):
 *      Integer value set to H5P_MT_CLASS_TAG when an instance of H5P_mt_class_t is
 *      allocated from the heap, and to H5P_MT_CLASS_INVALID_TAG just before it is 
 *      released back to the heap. The field is used to validate pointers to instances
 *      of H5P_mt_class_t.
 * 
 * parent_id (hid_t):
 *      ID assigned to the immediate parent property list class in the index. As the 
 *      parent cannot be deleted until its ref_counts drop to zero, it must exist at
 *      least as long as this property list class.
 * 
 *      This field is not atomic, as it is set before this property list class is 
 *      inserted in the index, thus before it's visible to other threads, and doesn't
 *      change for the life of this instance.
 * 
 * parent_ptr (H5P_mt_class_t *):
 *      Pointer to the instance of H5P_mt_class_t that represents the immediate parent
 *      property list class in the index. As the parent cannot be deleted until its
 *      ref_counts drop to zero, it must exist and this pointer must be valid at least
 *      as long as this property list class exists.
 * 
 *      This field is not atomic, as it is set before this property list class is 
 *      inserted in the index, thus before it's visible to other threads, and doesn't
 *      change for the life of this instance.
 * 
 * perent_version (uint64_t):
 *      Version of the parent property list class from which this property list class
 *      is derived.
 * 
 *      This field is not atomic, as it is set before this property list class is 
 *      inserted in the index, thus before it's visible to other threads, and doesn't
 *      change for the life of this instance.
 * 
 * name (char *):
 *      Pointer to a string containing the name of this property list class.
 * 
 *      This field is not atomic, as it is set before this property list class is 
 *      inserted in the index, thus before it's visible to other threads, and doesn't
 *      change for the life of this instance.
 * 
 * id (_Atomic hid_t *):
 *      Atomic instance of hid_t used to store the id assigned to this property list
 *      class in the index. This field is atomic, as it can't be set until after the
 *      instance of H5P_mt_class_t is registered, and thus visible to other threads.
 *      That said, once set, this field will not change for the life of the property 
 *      list class.
 * 
 * type (H5P_plist_type_t):
 *      Type of the property list class. 
 * 
 *      This field is not atomic, as it is set before this property list class is 
 *      inserted in the index, thus before it's visible to other threads, and doesn't
 *      change for the life of this instance.
 * 
 * curr_version (_Atomic uint64_t):
 *      Atomic uint64_t containing the current version of the property list class. This
 *      version number is incremented each time a modification to the property list class
 *      is completed.
 * 
 *      A uint64_t is used, as at present there is no provision for a roll over. Given
 *      the relative infrequency of modifications to propety list classes, 64 bits is
 *      probably sufficient for all reasonable cases. However, a roll over must never
 *      occur, and an error should be flagged if it does.
 * 
 *      To allow an undefined deletion version, the curr_version must be no less than 1.
 * 
 * next_version (_Atomic uint64_t):
 *      Atomic uint64_t containing the version number to be assigned to the next 
 *      modification of the property list class. When no modifications to the property 
 *      list class are in propgress next_version should be one greater than curr_version.
 * 
 *      When a modification to a property list class begins, it does a fetch and 
 *      increment on next_version, preforms its changes and tags them with the returned
 *      version, and finally increments the curr_version.
 * 
 *      NOTE: To avoid exposure of partial modifications, increments to curr_version must
 *      be executed in next_version issue order. Thus, a thread that modifies the propety
 *      list class, must not increment curr_version until its value is one less than the
 *      version number it obtained when it started.
 * 
 *      Further, if a modify or insert and a delete on the same property are active at
 *      the same time, they must be executed in issue_order.
 * 
 * pl_head (H5P_mt_prop_t *):
 *      Atomic pointer to the head of the LFSLL containing the list of properties (i.e.
 *      instances of H5P_mt_prop_t) associated with the property list class. Other than 
 *      during setup, this field will always point to the first node in teh list whose
 *      value will be negative infinity.
 * 
 *      Entries in this list are sorted first by a hash on the property name, second by
 *      property name (to allow for hash collisions), and finally by creation version in
 *      decreasing order. Other than during setup, the first and last entries in the list
 *      will be sentry nodes with hash values (conceptually) of negative and positive 
 *      infinity respectively.
 * 
 * log_pl_len (_Atomic uint32_t):
 *      Number of properties defined in the property list class at the current version. 
 *      
 *      NOTE: This value will not be correct for all versions of the property list class,
 *      and will be briefly incorrect even for the current version during property 
 *      insertions and deletions. Thus when an exact value is required, the property list
 *      class must be scanned for the correct value for the desired version.
 * 
 * phys_pl_len (_Atomic uint32_t):
 *      Number of instances of H5P_mt_prop_t in the property list class. This number 
 *      includes sentinel nodes, and both current and superseded instances of 
 *      H5P_mt_prop_t. 
 *      
 *      NOTE: This value will be briefly incorrect during property insertions, deletions,
 *      and modifications. Modification of a property cause this value to change, and a
 *      new instance of H5P_mt_prop_t is inserted with the desired changes and a new
 *      creation version.
 * 
 * ref_count (_Atomic H5P_mt_class_ref_counts_t):
 *      Atomic instance of H5P_mt_class_ref_counts_t which combines:
 * 
 *          1) The number of property lists immediately derived from this property list
 *             class, and still extant (ref_count.pl),
 *          2) The number of property list classes immediately derived from this property
 *             list class, and still extant (ref_count.plc),
 *          3) A boolean flag indicating whether this property list class has been
 *             deleted from the index (ref_count.deleted).
 * 
 *      into a single atomic structure - thus ensuring sychronization between these three
 *      different values.
 * 
 *      Once ref_count.pl and ref_count.plc have dropped to zero, and deleted is set to
 *      TRUE, the property list class may be discarded. Further, neither ref_count.pl or
 *      ref_count.plc may be incremented once this condition obtains.
 * 
 *      Further, observe that once this condition holds, the reference counts on all 
 *      versions of all properties in the property list class must be zero.
 * 
 * 
 * 
 * Callbacks are currently left out for early testing
 * 
 * 
 * Free list and shutdown management fields:
 * 
 * thrd (_Atomic H5P_mt_active_thread_count_t):
 *      This field is a structure used to verify that no threads are active in an 
 *      instance of H5P_mt_class_t during setup or takedown of the structure prior to 
 *      discard. See the header comment on H5P_mt_active_thread_count_t for a detailed 
 *      discription of how its fields must be maintained whena thread wants to access the 
 *      host instance of H5P_mt_class_t.
 * 
 * fl_next (_Atomic_H5P_mt_class_sptr_t):
 *      This field is a structure used to contain a pointer to the next instance of 
 *      H5P_mt_class_t in the free list. It is augmented with a serial number to avoid
 *      ABA bugs. This field is included to support a free list of instances of 
 *      H5P_mt_class_t.
 * 
 * 
 * Statistics Fields:
 *  
 ****************************************************************************************
 */
#define H5P_MT_CLASS_TAG            0x1011 /* 4113 */
#define H5P_MT_CLASS_INVALID_TAG    0x2021 /* 8225 */

typedef struct H5P_mt_class_t
{
    uint32_t           tag;

    /* fields related to the parent class */
    hid_t              parent_id;
    H5P_mt_class_t   * parent_ptr;
    uint64_t           parent_version;

    /* Fields related to this class */
    char             * name;
    _Atomic hid_t    * id;
    H5P_plist_type_t   type;
    _Atomic uint64_t   curr_version;
    _Atomic uint64_t   next_version;

    /* List of properties, and related fields */
    H5P_mt_prop_t    * pl_head;
    _Atomic uint32_t   log_pl_len;
    _Atomic uint32_t   phys_pl_len;

    /* reference counts */
    _Atomic H5P_mt_class_ref_counts_t ref_count;

    /* Callback function pointers and info */
    /* Currently left out for simple testing */

    /* Shutdown and free list management fields */
    _Atomic H5P_mt_active_thread_count_t thrd;
    _Atomic H5P_mt_class_sptr_t          fl_next;

    /* Stats */

} H5P_mt_class_t;




/* Structures for Property Lists */




/****************************************************************************************
 * 
 * Structure: H5P_mt_list_sptr_t
 * 
 * Description:
 * 
 * The H5P_mt_list_sptr_t combines a pointer to H5P_mt_list_t with a serial number
 * in a 128 bit package. It is intended to allow instances of H5P_mt_list_t to be
 * linked together in a singly linked list – specifically in a free list.
 *
 * This combination of a pointer and a serial number is needed to prevent ABA
 * bugs.
 * 
 * Fields:
 * 
 * ptr (H5P_mt_list_t *):
 *      Pointer to an instance of H5P_mt_list_t.
 * 
 * sn (uint64_t):
 *      Serial number that should be incremented by 1 each time a new value is assigned
 *      to ptr.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_list_t H5P_mt_list_t; /* Forward declaration */

typedef struct H5P_mt_list_sptr_t
{
    H5P_mt_list_t * ptr;
    uint64_t        sn;

} H5P_mt_list_sptr_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_list_prop_ref_t
 * 
 * Description:
 * 
 * H5P_mt_list_prop_ref_t is a structure designed to contain a pointer to an instance of 
 * H5P_mt_prop_t and a version number in a single atomic structure. This is necessary, as 
 * when an entry in the H5P_mt_list_table_entry_t is updated, we need to update both the 
 * pointer to the instance of H5P_mt_prop_t and the version number in a single atomic 
 * operation.
 *
 * This structure is 128 bits, which allows true atomic operation on many (most?) modern
 * CPUs.
 *
 * The structure is used in two contexts:
 *
 * First to point to the instance of H5P_mt_prop_t in the property list class from
 * which the host property list was derived. In this case, version number should be
 * the initial version of the host property list class.
 *
 * Second, if the default value of the property has been overwritten, to point to an
 * instance of H5P_mt_prop_t in the host property list class's LFSLL of modified or
 * added properties. In this case, the ver field must match the create_version field
 * of the instance of H5P_mt_prop_t pointed to by ptr.
 * 
 * Fields:
 * 
 * ptr (H5P_mt_prop_t *):
 *      Pointer to an instance of H5P_mt_prop_t.
 * 
 * ver (uint64_t):
 *      Version number of the host property list class at which this pointer was set.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_list_prop_ref_t
{
    H5P_mt_prop_t * ptr;
    uint64_t        ver;

} H5P_mt_list_prop_ref_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_list_table_entry_t
 * 
 * Description:
 * 
 * An array of instances of H5P_mt_list_table_entry_t is used to create a look up table
 * for properties inherited from the parent property list class.
 * 
 * Fields:
 * 
 * chksum (int64_t):
 *      int64_t containing a 32-bit checksum coputed on the name field below.
 * 
 * name (char *):
 *      Pointer to a dynamically allocated string containing the name of the property. 
 * 
 *      This field is not atomic, as it is set before this instance of H5P_mt_prop_t is 
 *      visible to other threads, and doesn't change for the life of this instance.
 * 
 * base (_Atomic H5P_mt_list_prop_ref_t):
 *      Atomic structure of H5P_mt_list_prop_ref_t with it's ptr field pointing to the
 *      instance of H5P_mt_prop_t in the parent property list class, and the ver field
 *      should contain the initial version number of the property list.
 * 
 *      NOTE: If the instance of H5P_mt_prop_t in the parent property list class has a 
 *      create callback, we must copy the property into the new property list, and not
 *      use the property in the parent property list class as the initial value of the
 *      property. In this case, base.ptr is set to NULL, and base.ver set to 0, the copy
 *      is inserted into the lock free slignly linked list, and curr.ptr points to the
 *      copy of the property until such time as the value of the property is modified. In 
 *      this case, curr.ver is initialized to the initial version of the property list.
 * 
 * base_delete_version (_Atomic uint64_t):
 *      Property lists derived from a property list class must not modify properties in
 *      the parent property list class. Thus they must maintain their own create and 
 *      delete version. The create version is simply the initial version of the property
 *      list, and is stored in the base.ver field. However, if the property is deleted
 *      from the property list, we must have a delete version to indicate the property 
 *      list version at which this took place.
 * 
 *      The atomic uint64_t base_delete_version exists to serve this purpose. If the base
 *      version of the property has not been deleted, this field will be 0. Once set to
 *      a non-zero value, it will never change for the life of the property list.
 * 
 * curr (_Atomic H5P_mt_list_prop_ref_t):
 *      Atomic instance of H5P_mt_list_prop_ref_t whose ptr and ver fields must be 
 *      initialized to NULL and 0 respectively.
 * 
 *      If the value of the inherited property is modified, a new instance of 
 *      H5P_mt_prop_t is allocated, copying the tag, sentinel, chksum, name, and 
 *      callback fields from the most recent version of the property pointed to by 
 *      either base.ptr or curr.ptr.
 * 
 *      The in_prop_class, and ref_count fields are set to zero, and not used in 
 *      property lists. The create_version is set to the version of the property list in 
 *      which the modified version is set, and the delete_version is set to 0. The value
 *      field is set to point to the new value of the property, and the new instance of 
 *      H5P_mt_prop_t is inserted intot he LFSLL of new / modified properties associated 
 *      with the host property list.
 * 
 *      Next, curr.ptr is set to point to the new instance, and curr.ver is set to its
 *      create_version. Recall that both of these fields are set in a single atomic
 *      operation. 
 * 
 *      Finally, the version of the host property list is incremented to make these 
 *      chagnes visible.
 * 
 ****************************************************************************************
 */
typedef struct H5P_mt_list_table_entry_t
{
    int64_t                         chksum;
    char                          * name;
    _Atomic H5P_mt_list_prop_ref_t  base;
    _Atomic uint64_t                base_delete_version;
    _Atomic H5P_mt_list_prop_ref_t  curr;

} H5P_mt_list_table_entry_t;



/****************************************************************************************
 * 
 * Structure: H5P_mt_list_t
 * 
 * Description:
 * 
 * Revised version of H5P_genlist_t designed for use in a multi-thread safe version of 
 * the HDF5 property list module (H5P).
 * 
 * At the conceptual level, a property list class is simply a list of properties -- i.e.
 * name value pairs.
 * 
 * When a property list is created, it incorporates a list of properties with default 
 * values from its parent property list class at the version at which the creation of the 
 * property list was started. At this time, the number of properties in the property list
 * class is detemined and stored in the nprops_inherited field.
 * 
 * This done, an array of H5P_mt_list_table_entry_t of length nprops_inherited is 
 * allocated, with base.ptr field pointing to the associated instance of H5P_mt_prop_t
 * in the parent property list class's lock free singly linked list (LFSLL) of 
 * properties, and the base.ver field set to the initial version of the property list. 
 * After this array is initialized, it is sorted by chksum and then name, and the 
 * lkup_table field is set to point to the array of H5P_mt_list_table_entry_t instances. 
 * Observe that this table allows a log n lookup of properties inherited from the parent 
 * property list class.
 * 
 * If the value of any inherited property is modified, a new instance of H5P_mt_prop_t
 * is created with the modified value and new creation version, and is inserted into the
 * property list's LFSLL of properties. The curr.ptr field of the appropriate entry
 * in the lookup table is set to point to it, and the curr.ver field is set to the new
 * version of the property. Finally, the property list's curr_version field is 
 * incremented to make this modification visible.
 *
 * If a new property is added to the property list, it is simply inserted into the
 * property list's LFSLL of instances of H5P_mt_prop_t, and the nprops_added field is
 * incremented. On searches, the lookup table is searched first, with the LFSLL being
 * searched only if this first search fails, and nprops_added is positive.
 *
 * As with the multi-thread version of the property list classes, property lists
 * maintain back versions of all properties tagged with the property list version
 * in which they were created (and possibly deleted). Unlike property list classes,
 * the properties are not reference counted.
 *
 * All operations on a property list must address a specific version, which must be no
 * greater than the current version at the start of the operation. As shall be seen,
 * this allows us to make all modifications effectively atomic, which in turn allows
 * concurrent operations to occur without the potential for data corruption.
 *
 * As with property list classes, making modifications to a property list effectively
 * atomic is slightly tricky, but can be handled in much the same way. Since every
 * operation on a property list is targeted at a specific version, we can make changes in
 * progress effectively invisible since new or modified properties are represented by
 * new instances of H5P_mt_prop_t with creation property list versions higher
 * than the current version, and thus don't become visible until the property list
 * version is incremented to the point that they become visible..
 *
 * However, as with property list classes, if multiple modifications to the property list
 * are in progress simultaneously, there is a race condition between the issue of a new
 * version number to be used to tag a modification, and the increment of the property list
 * version number when the modification completes.
 * 
 * As with property list classes, this can be usually be handled by waiting to increment
 * the version number until the current version number is one less than the issued
 * version number. However, if a modify or insert and a delete on the same property are
 * active at the same time, they must be executed in issue order.
 *
 * Also, as per property list classes, modified / new properties are stored on a LFSLL 
 * instead of a skip list. This LFSLL list is sorted first by a hash on the property 
 * name, second by property name (to allow for hash collisions), and finally by creation 
 * version in decreasing order.
 *
 * Since only new / modified properties are stored on this list, it should be shorter
 * than the similar list in property list classes. More importantly, the latest
 * version of each inherited property is pointed to by the appropriate entry in the
 * lookup table. Added properties still require a linear search through the LFSLL.
 * If this proves to be a performance issue, we can either keep added entries in a
 * different list, or allow the lookup table to be extended when new entries are added.
 *
 * Property lists are stored in the index, and should only be accessed via their IDs.
 * Within HDF5, the reference count on the property list ID should be incremented before
 * its pointer is looked up in the index, and should not be decremented until the code
 * in question is done with the property list. If this rule is followed religiously, it 
 * should be impossible for a property list to be deleted out from under a thread, or
 * for any thread to access a property list after its reference count drops to zero and
 * it is removed from the index and discarded.
 *
 * However, since any failure of this mechanism will be hard to diagnose, an instance
 * of H5P_mt_active_thread_count_t is included in H5P_mt_list_t, and must be maintained.
 * The protocol for doing this is discussed in the header comment for
 * H5P_mt_active_thread_count_t. In the context of H5P_mt_list_t, a positive thread
 * count on discard is an error and should trigger an assertion failure.
 *
 * Similarly, H5P_mt_list_t contains an instance of H5P_mt_list_sptr_t to support a
 * free list that shouldn’t be necessary, but which is probably prudent for much the
 * same reason.
 * 
 * Fields:
 * 
 * tag (uint32_t):
 *      Integer value set to H5P_MT_LIST_TAG when an instance of H5P_mt_list_t is 
 *      allocated from the heap, and set to H5P_MT_LIST_INVALID_TAG just before it is
 *      released back to the heap. The field is used to validate pointers to instances
 *      of H5P_mt_list_t.
 * 
 * pclass_id (hid_t):
 *      ID of the instance of H5P_mt_class_t from which the property list was derived.
 * 
 *      This field is not atomic, as it is set before this property list is inserted in
 *      the index, thus before it's visible to other threads, and doesn't change for the
 *      life of this instance.
 * 
 * pclass_ptr (H5P_mt_class_t *):
 *      Pointer to the instance of H5P_mt_class_t from which the property list was 
 *      derived.
 * 
 *      This field is not atomic, as it is set before this property list is inserted in
 *      the index, thus before it's visible to other threads, and doesn't change for the
 *      life of this instance.
 * 
 * pclass_version (uint64_t):
 *      Version of the parent property list class from which this property list was 
 *      derived.
 * 
 * plist_id (hid_t):
 *      ID assigned to this property list. This field must be atomic, because the 
 *      instance of H5P_mt_list_t becomes visible to other threads before this field can 
 *      be set. That said, once it is set, it should not change for the life of the
 *      property list.
 * 
 * curr_version (_Atomic uint64_t):
 *      Atomic uint64_t containing the current version of the propety list. This version
 *      number is incremented each time a modification to the property list is completed.
 *      
 *      A uint64_t is used, as at present there is no provision for a roll over. Given
 *      the relative infrequency of modifications to property lists, 64-bits is probably
 *      sufficient for all reasonable cases. However, a roll over must never occur, and
 *      an error should be flagged if it does.
 * 
 *      To allow an undefined deletion version, the curr_version must be no less than 1.
 * 
 * next_version (_Atomic uint64_t):
 *      Atomic uint64_t containing the version number to be assigned to the next 
 *      modification of the property list.
 * 
 *      When no modificaitons to the property list are in progress, next_version should
 *      be one greater than curr_version.
 * 
 *      When a modification to a property list begins, it does a fetch and increment on
 *      next_version, preforms its changes and tags them with the returned version, and
 *      finally increments the curr_version.
 * 
 *      NOTE: To avoid exposure of partial modifications, increments to curr_version must
 *      be executed in next_version issue order. Thus, a thread that modifies the propery
 *      list, must not increment curr_version until its value is one less than the 
 *      version number it obtained when it started.
 * 
 * lkup_tbl (H5P_mt_list_table_entry_t *):
 *      Pointer to an array of H5P_mt_list_table_entry_t that permits fast lookup of
 *      properties inherited from the parent property list class.
 * 
 *      See the header comment for H5P_mt_list_table_entry_t for further details.
 * 
 * nprops_inherited (uint32_t):
 *      The number of properties inherited from the parent property list class, and also
 *      the number of entries in the lookup table (lkup_tbl) above. 
 * 
 *      NOTE: Any or all of these properties may be deleted in an arbitrary version of
 *      the property list.
 * 
 * nprops_added (_Atomic uint32_t):
 *      The number of properties added to the property list after its creation. 
 *      
 *      NOTE: These properties do not appear in the lkup_tbl, and thus if a search for
 *      a property fails in lkup_tbl and nprops_added is positive, the LFSLL pointed to
 *      by pl_head (below) must also be searched.
 * 
 * nprops (_Atomic uint32_t):
 *      Number of properties defined in the current version of the property list.
 *      
 *      NOTE: This value may be briefly incorrect during property additions or deletions
 *      -- if an accurate value is required, the LFSLL pointed to by pl_head must be 
 *      scanned for the target property list version.
 * 
 * pl_head (_Atomic H5P_mt_prop_t *):
 *      Atomic pointer to the head of the LFSLL containing the list of modified or 
 *      inserted properties (i.e. instances of H5P_mt_prop_t) associated with the 
 *      property list. Other than during setup, this field will always point to the first
 *      node in the list whose value will be negative infinity (conceptually).
 * 
 *      Entries in this list are sorted first by a hash on the property name, second by
 *      property name (to allow for collisions), and finally by creation version in 
 *      decreasing order. Other than during setup, the first and last entries in the list
 *      will be sentinel entries with hash values (conceptually) of negative and positive
 *      infinity respectively.
 * 
 * log_pl_len (_Atomic uint32_t): 
 *      Number of properties defined in the property list at the current version. 
 * 
 *      NOTE: This value will not be correct for all versions of the property list, and
 *      will be briefly incorrect even for the current version during property 
 *      insertions and deletions. Thus when an exact value is required, the property list
 *      must be scanned for the correct value for the desired version.
 * 
 * phys_pl_len (_Atomic uint32_t):
 *      Number of instances of H5P_mt_prop_t in the property list. This number includes
 *      sentinel nodes, and both current and superseded instanced of H5P_mt_prop_t. 
 *  
 *      NOTE: This value will be briefly incorrect during property insertions, deletions,
 *      and modifications. Modifications of a property cause this value to change, and a
 *      new instance of H5P_mt_prop_t is inserted with the desired changed and a new
 *      creation version.
 * 
 * class_init (_Atomic bool): 
 *      True iff the class initialization callback finished successfully.
 * 
 * 
 * Free list and shutdown management fields:
 * 
 * thrd (_Atomic H5P_mt_active_thread_count_t):
 *      This field is a structure used to verify that no threads are active in an 
 *      instance of H5P_mt_class_t during setup or takedown of the structure prior to 
 *      discard. See the header comment on H5P_mt_active_thread_count_t for a detailed 
 *      discription of how its fields must be maintained whena thread wants to access the 
 *      host instance of H5P_mt_list_t.
 * 
 * fl_next (_Atomic H5P_mt_list_sptr_t):
 *      This field is a structure used to contain a pointer to the next instance of
 *      H5P_mt_list_t in the free list. It is augmented with a serial number to avoid
 *      ABA bugs. This field is included to support a free list of instances of
 *      H5P_mt_list_t.
 * 
 * 
 * Statistics Fields     
 * 
 ****************************************************************************************
 */
#define H5P_MT_LIST_TAG         0x1012 /* 4114 */
#define H5P_MT_LIST_INVALID_TAG 0x2022 /* 8226 */

typedef struct H5P_mt_list_t
{
    uint32_t                    tag;

    /* Fields related to the parent class*/
    hid_t                       pclass_id;
    H5P_mt_class_t            * pclass_ptr;
    uint64_t                    pclass_version;

    /* Fields related to this class */
    _Atomic hid_t               plist_id;
    _Atomic uint64_t            curr_version;
    _Atomic uint64_t            next_version;

    H5P_mt_list_table_entry_t * lkup_tbl;

    /* Fields related to number of properties */
    uint32_t                    nprops_inherited;
    _Atomic uint32_t            nprops_added;
    _Atomic uint32_t            nprops;

    /* List of properties in the LFSLL, and related fields */
    H5P_mt_prop_t             * pl_head;
    _Atomic uint32_t            log_pl_len;
    _Atomic uint32_t            phys_pl_len;

    _Atomic bool                class_init;

    /* Shutdown and free list management fields */
    _Atomic H5P_mt_active_thread_count_t thrd;
    _Atomic H5P_mt_list_sptr_t           fl_next;

    /* stats */

} H5P_mt_list_t;



/*****************************/
/* Package Private Variables */
/*****************************/


/*****************************/
/* Package Private Variables */
/*****************************/



#endif /* H5Ppkg_mt_H */