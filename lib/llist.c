/* vi: set sw=4 ts=4 :
 * llist.c - Linked list functions
 *
 * Linked list structures have a next pointer as their first element.
 */

#include "toys.h"

// Call a function (such as free()) on each element of a linked list.
void llist_traverse(void *list, void (*using)(void *data))
{
	while (list) {
		void *pop = llist_pop(&list);
		using(pop);

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
