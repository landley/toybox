/* llist.c - Linked list functions
 *
 * Linked list structures have a next pointer as their first element.
 */

#include "toys.h"

// Callback function to free data pointer of double_list or arg_list

void llist_free_arg(void *node)
{
  struct arg_list *d = node;

  free(d->arg);
  free(d);
}

void llist_free_double(void *node)
{
  struct double_list *d = node;

  free(d->data);
  free(d);
}

// Call a function (such as free()) on each element of a linked list.
void llist_traverse(void *list, void (*using)(void *node))
{
  void *old = list;

  while (list) {
    void *pop = llist_pop(&list);
    using(pop);

    // End doubly linked list too.
    if (old == list) break;
  }
}

// Return the first item from the list, advancing the list (which must be called
// as &list)
void *llist_pop(void *list)
{
  // I'd use a void ** for the argument, and even accept the typecast in all
  // callers as documentation you need the &, except the stupid compiler
  // would then scream about type-punned pointers.  Screw it.
  void **llist = (void **)list;
  void **next = (void **)*llist;
  *llist = *next;

  return (void *)next;
}

void *dlist_pop(void *list)
{
  struct double_list **pdlist = (struct double_list **)list, *dlist = *pdlist;

  if (dlist->next == dlist) *pdlist = 0;
  else {
    dlist->next->prev = dlist->prev;
    dlist->prev->next = *pdlist = dlist->next;
  }

  return dlist;
}

void dlist_add_nomalloc(struct double_list **list, struct double_list *new)
{
  if (*list) {
    new->next = *list;
    new->prev = (*list)->prev;
    (*list)->prev->next = new;
    (*list)->prev = new;
  } else *list = new->next = new->prev = new;
}


// Add an entry to the end of a doubly linked list
struct double_list *dlist_add(struct double_list **list, char *data)
{
  struct double_list *new = xmalloc(sizeof(struct double_list));

  new->data = data;
  dlist_add_nomalloc(list, new);

  return new;
}

// Terminate circular list for traversal in either direction. Returns end *.
void *dlist_terminate(void *list)
{
  struct double_list *end = list;

  if (!list) return 0;

  end = end->prev;
  end->next->prev = 0;
  end->next = 0;

  return end;
}

// Find num in cache
struct num_cache *get_num_cache(struct num_cache *cache, long long num)
{
  while (cache) {
    if (num==cache->num) return cache;
    cache = cache->next;
  }

  return 0;
}

// Uniquely add num+data to cache. Updates *cache, returns pointer to existing
// entry if it was already there.
struct num_cache *add_num_cache(struct num_cache **cache, long long num,
  void *data, int len)
{
  struct num_cache *old = get_num_cache(*cache, num);

  if (old) return old;

  old = xzalloc(sizeof(struct num_cache)+len);
  old->next = *cache;
  old->num = num;
  memcpy(old->data, data, len);
  *cache = old;

  return 0;
}
