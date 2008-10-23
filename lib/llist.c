/* vi: set sw=4 ts=4 :
 * llist.c - Linked list functions
 *
 * Linked list structures have a next pointer as their first element.
 */

#include "toys.h"

// Free all the elements of a linked list
// if freeit!=NULL call freeit() on each element before freeing it.

void llist_free(void *list, void (*freeit)(void *data))
{
	while (list) {
		void *pop = llist_pop(&list);
		if (freeit) freeit(pop);
		else free(pop);

		// End doubly linked list too.
		if (list==pop) break;
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

// Add an entry to the end off a doubly linked list
struct double_list *dlist_add(struct double_list **list, char *data)
{
	struct double_list *line = xmalloc(sizeof(struct double_list));

	line->data = data;
	if (*list) {
		line->next = *list;
		line->prev = (*list)->prev;
		(*list)->prev->next = line;
		(*list)->prev = line;
	} else *list = line->next = line->prev = line;

	return line;
}
