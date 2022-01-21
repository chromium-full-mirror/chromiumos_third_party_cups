#include "array.h" /* cups_array_func_t */

#ifndef _STL_WRAPPER_H_
#  define _STL_WRAPPER_H_

#  ifdef __cplusplus
extern "C" {
#  endif /* __cplusplus */

/*** stl multiset - wrapper around std::multiset replacing corresponding
 * cupsArray* functions from array.h. It also has:
 * - internal iterator - an iterator pointing to one of the element in the
 *                       container or equaling END (position after the last
 *                       element)
 * - internal stack - stack of valid iterators (!=END), see stlMultisetSave
 *                    and stlMultisetRestore.
 ***/

typedef struct stl_multiset_s {
  void *ptr;
} stl_multiset_t;

/* Initializes given structure by creating empty container with the order
 * defined by the function "compare". Only the first two arguments of this
 * function are used. */
void stlMultisetNew(stl_multiset_t*, cups_array_func_t compare);

/* Frees all memory allocated in the previous function. */
void stlMultisetDelete(stl_multiset_t*);

/* Adds a given element to the container. */
void stlMultisetAdd(stl_multiset_t, void* element);

/* Gets the number of elements in the container. */
int stlMultisetCount(stl_multiset_t);

/* Finds and returns the first element in the container == the given key.
 * If there are no such elements it returns NULL and sets the internal
 * iterator to END. Otherwise, it sets the internal iterator to the first
 * element == the given key and returns this element. */
void* stlMultisetFind(stl_multiset_t, void* key);

/* Works similar as stlMultisetFind(...) but removes the returned element
 * from the container. If the element was found (and removed) the internal
 * iterator is set to the element following the removed one. */
void* stlMultisetRemove(stl_multiset_t, void* key);

/* Set the internal iterator to the first element and returns it.
 * Returns NULL if the internal iterator == END (the container is empty). */
void* stlMultisetFirst(stl_multiset_t);

/* Increase the internal iterator by one and return new current element.
 * It does nothing if the internal iterator == END.
 * Returns NULL if the internal iterator == END. */
void* stlMultisetNext(stl_multiset_t);

/* Set internal iterator to END. */
void stlMultisetIndexEnd(stl_multiset_t);

/* Save the current position of internal iterator to internal stack.
 * Returns 1 if succeds and 0 otherwise.
 * When the internal iterator == END it does nothing and return 0. */
int stlMultisetSave(stl_multiset_t);

/* Pop from the internal stack the last saved position and set the internal
 * iterator to it. Returns the element at this position.
 * Returns NULL is the internal stack is empty. */
void* stlMultisetRestore(stl_multiset_t);

#  ifdef __cplusplus
}
#  endif /* __cplusplus */

#endif /* !_CUPS_STL_WRAPPER_H_ */
