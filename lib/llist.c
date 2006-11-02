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
		void **next = (void **)list;
		void *list_next = *next;
		if (freeit) freeit(list);
		free(list);
		list = list_next;
	}
}
