#pragma once

#include "alloc.h"

#ifdef __GNUC__
#define LIST_UNUSED __attribute__((unused))
#else
#define LIST_UNUSED /* nothing */
#endif

/** create a new list type and its associated member functions
 *
 * \param name Type name to give the list container
 * \param type Type of the elements the list will store
 */
#define DEFINE_LIST(name, type) DEFINE_LIST_WITH_DTOR(name, type, NULL)

/** \p DEFINE_LIST but with a custom element destructor
 *
 * \param name Type name to give the list container
 * \param type Type of the elements the list will store
 * \param dtor Destructor to be called on elements being released
 */
#define DEFINE_LIST_WITH_DTOR(name, type, dtor)                                \
                                                                               \
  /** list container                                                           \
   *                                                                           \
   * All members of this type are considered private. They should only be      \
   * accessed through the functions below.                                     \
   */                                                                          \
  typedef struct {                                                             \
    type *data;      /* backing storage */                                     \
    size_t size;     /* number of elements in the list */                      \
    size_t capacity; /* available storage slots */                             \
  } name##_t;                                                                  \
                                                                               \
  /** create a new list                                                        \
   *                                                                           \
   * This function is provided for convenience, but it is the equivalent of    \
   * zero initialization, `list_t list = {0}`. In general, the latter should   \
   * be preferred as it can be understood locally.                             \
   *                                                                           \
   * \return A new empty list                                                  \
   */                                                                          \
  static inline LIST_UNUSED name##_t name##_new(void) {                        \
    return name##_t{0};                                                      \
  }                                                                            \
                                                                               \
  /** get the number of elements in a list */                                  \
  static inline LIST_UNUSED size_t name##_size(const name##_t *list) {         \
    assert(list != NULL);                                                      \
    return list->size;                                                         \
  }                                                                            \
                                                                               \
  /** does this list contain no elements? */                                   \
  static inline LIST_UNUSED bool name##_is_empty(const name##_t *list) {       \
    assert(list != NULL);                                                      \
    return name##_size(list) == 0;                                             \
  }                                                                            \
                                                                               \
  static inline LIST_UNUSED void name##_append(name##_t *list, type item) {    \
    assert(list != NULL);                                                      \
                                                                               \
    /* do we need to expand the backing storage? */                            \
    if (list->size == list->capacity) {                                        \
      size_t c = list->capacity == 0 ? 1 : (list->capacity * 2);               \
      list->data = (type*) gv_recalloc(list->data, list->capacity, c, sizeof(type));   \
      list->capacity = c;                                                      \
    }                                                                          \
                                                                               \
    list->data[list->size] = item;                                             \
    ++list->size;                                                              \
  }                                                                            \
                                                                               \
  /** retrieve an element from a list                                          \
   *                                                                           \
   * \param list List to operate on                                            \
   * \param index Element index to get                                         \
   * \return Element at the given index                                        \
   */                                                                          \
  static inline LIST_UNUSED type name##_get(const name##_t *list,              \
                                            size_t index) {                    \
    assert(list != NULL);                                                      \
    assert(index < list->size && "index out of bounds");                       \
    return list->data[index];                                                  \
  }                                                                            \
                                                                               \
  /** assign to an element in a list                                           \
   *                                                                           \
   * \param list List to operate on                                            \
   * \param index Element to assign to                                         \
   * \param item Value to assign                                               \
   */                                                                          \
  static inline LIST_UNUSED void name##_set(name##_t *list, size_t index,      \
                                            type item) {                       \
    assert(list != NULL);                                                      \
    assert(index < list->size && "index out of bounds");                       \
    void (*dtor_)(type) = (void (*)(type))(dtor);                              \
    if (dtor_ != NULL) {                                                       \
      dtor_(list->data[index]);                                                \
    }                                                                          \
    list->data[index] = item;                                                  \
  }                                                                            \
                                                                               \
  /** remove an element from a list                                            \
   *                                                                           \
   * \param list List to operate on                                            \
   * \param item Value of element to remove                                    \
   */                                                                          \
  static inline LIST_UNUSED void name##_remove(name##_t *list, type item) {    \
    assert(list != NULL);                                                      \
                                                                               \
    for (size_t i = 0; i < list->size; ++i) {                                  \
      /* is this the element we are looking for? */                            \
      if (memcmp(&list->data[i], &item, sizeof(type)) == 0) {                  \
                                                                               \
        /* destroy the element we are about to remove */                       \
        void (*dtor_)(type) = (void (*)(type))(dtor);                          \
        if (dtor_ != NULL) {                                                   \
          dtor_(list->data[i]);                                                \
        }                                                                      \
                                                                               \
        /* shrink the list */                                                  \
        size_t remainder = (list->size - i - 1) * sizeof(type);                \
        memmove(&list->data[i], &list->data[i + 1], remainder);                \
        --list->size;                                                          \
        return;                                                                \
      }                                                                        \
    }                                                                          \
  }                                                                            \
                                                                               \
  /** access an element in a list for the purpose of modification              \
   *                                                                           \
   * Because this acquires an internal pointer into the list structure, `get`  \
   * and `set` should be preferred over this function. `get` and `set` are     \
   * easier to reason about. In particular, the pointer returned by this       \
   * function is invalidated by any list operation that may reallocate the     \
   * backing storage (e.g. `shrink_to_fit`).                                   \
   *                                                                           \
   * \param list List to operate on                                            \
   * \param index Element to get a pointer to                                  \
   * \return Pointer to the requested element                                  \
   */                                                                          \
  static inline LIST_UNUSED type *name##_at(name##_t *list, size_t index) {    \
    assert(list != NULL);                                                      \
    assert(index < list->size && "index out of bounds");                       \
    return &list->data[index];                                                 \
  }                                                                            \
                                                                               \
  /** remove all elements from a list */                                       \
  static inline LIST_UNUSED void name##_clear(name##_t *list) {                \
    assert(list != NULL);                                                      \
                                                                               \
    void (*dtor_)(type) = (void (*)(type))(dtor);                              \
    if (dtor_ != NULL) {                                                       \
      for (size_t i = 0; i < list->size; ++i) {                                \
        dtor_(list->data[i]);                                                  \
      }                                                                        \
    }                                                                          \
                                                                               \
    list->size = 0;                                                            \
  }                                                                            \
                                                                               \
  /** shrink or grow the list to the given size                                \
   *                                                                           \
   * \param list List to operate on                                            \
   * \param size New size of the list                                          \
   * \param value Default to assign to any new elements                        \
   */                                                                          \
  static inline LIST_UNUSED void name##_resize(name##_t *list, size_t size,    \
                                               type value) {                   \
    assert(list != NULL);                                                      \
                                                                               \
    if (list->size < size) {                                                   \
      /* we are expanding the list */                                          \
      while (list->size < size) {                                              \
        name##_append(list, value);                                            \
      }                                                                        \
    } else if (list->size > size) {                                            \
      /* we are shrinking the list */                                          \
      while (list->size > size) {                                              \
        void (*dtor_)(type) = (void (*)(type))(dtor);                          \
        if (dtor_ != NULL) {                                                   \
          dtor_(list->data[list->size - 1]);                                   \
        }                                                                      \
        --list->size;                                                          \
      }                                                                        \
    }                                                                          \
  }                                                                            \
                                                                               \
  /** sort the list using the given comparator */                              \
  static inline LIST_UNUSED void name##_sort(                                  \
      name##_t *list, int (*cmp)(const type *a, const type *b)) {              \
    assert(list != NULL);                                                      \
    assert(cmp != NULL);                                                       \
                                                                               \
    int (*compar)(const void *, const void *) =                                \
        (int (*)(const void *, const void *))cmp;                              \
    if (list->size > 0) {                                                      \
      qsort(list->data, list->size, sizeof(type), compar);                     \
    }                                                                          \
  }                                                                            \
                                                                               \
  /** deallocate unused backing storage, shrinking capacity to size */         \
  static inline LIST_UNUSED void name##_shrink_to_fit(name##_t *list) {        \
    assert(list != NULL);                                                      \
                                                                               \
    if (list->capacity > list->size) {                                         \
      list->data =                                                             \
          (type*) gv_recalloc(list->data, list->capacity, list->size, sizeof(type));   \
      list->capacity = list->size;                                             \
    }                                                                          \
  }                                                                            \
                                                                               \
  /** free resources associated with a list */                                 \
  static inline LIST_UNUSED void name##_free(name##_t *list) {                 \
    assert(list != NULL);                                                      \
    name##_clear(list);                                                        \
    free(list->data);                                                          \
    *list = name##_t{0};                                                     \
  }                                                                            \
                                                                               \
  /** alias for append */                                                      \
  static inline LIST_UNUSED void name##_push(name##_t *list, type value) {     \
    name##_append(list, value);                                                \
  }                                                                            \
                                                                               \
  /** remove and return last element */                                        \
  static inline LIST_UNUSED type name##_pop(name##_t *list) {                  \
    assert(list != NULL);                                                      \
    assert(list->size > 0);                                                    \
                                                                               \
    type value = list->data[list->size - 1];                                   \
                                                                               \
    /* do not call `dtor` because we are transferring ownership of the removed \
     * element to the caller                                                   \
     */                                                                        \
    --list->size;                                                              \
                                                                               \
    return value;                                                              \
  }                                                                            \
                                                                               \
  /** create a new list from a bare array and element count                    \
   *                                                                           \
   * This can be useful when receiving data from a caller who does not use     \
   * this API, but the callee wants to. Note that the backing data for the     \
   * array must have been heap-allocated.                                      \
   *                                                                           \
   * \param data Array of existing elements                                    \
   * \param size Number of elements pointed to by `data`                       \
   * \return A managed list containing the provided elements                   \
   */                                                                          \
  static inline LIST_UNUSED name##_t name##_attach(type *data, size_t size) {  \
    assert(data != NULL || size == 0);                                         \
    return name##_t{ data, size, size };           \
  }                                                                            \
                                                                               \
  /** transform a managed list into a bare array                               \
   *                                                                           \
   * This can be useful when needing to pass data to a callee who does not     \
   * use this API. The managed list is emptied and left in a state where it    \
   * can be reused for other purposes.                                         \
   *                                                                           \
   * \param list List to operate on                                            \
   * \return A pointer to an array of the `list->size` elements                \
   */                                                                          \
  static inline LIST_UNUSED type *name##_detach(name##_t *list) {              \
    assert(list != NULL);                                                      \
    type *data = list->data;                                                   \
    *list = name##_t{0};                                                     \
    return data;                                                               \
  }
