// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_LIST_H
#define __UMEM_CACHE_LIST_H

#include <assert.h>
#include "container_of.h"

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

/**
 * list_head_init - Initialize list @head
 */
static inline void list_head_init(struct list_head *head)
{
	head->prev = head;
	head->next = head;
}

/**
 * list_empty - Check if list @head is empty
 */
static inline bool list_empty(const struct list_head *head)
{
	return head->next == head;
}

/*
 * __list_add - Add node @new between @prev and @next
 */
static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	assert(new != prev && new != next);
	new->prev = prev;
	new->next = next;
	prev->next = new;
	next->prev = new;
}

/**
 * list_add - Add @new after @head
 */
static inline void list_add(struct list_head *head, struct list_head *new)
{
	__list_add(new, head, head->next);
}

/*
 * __list_del - Delete the node between @prev and @next
 */
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	assert(prev != NULL);
	prev->next = next;
	next->prev = prev;
}

/**
 * list_del - Delete @node from the list
 */
static inline void list_del(struct list_head *node)
{
	__list_del(node->prev, node->next);
}

/**
 * list_fix - Used for replacing a node in the list
 */
static inline void list_fix(struct list_head *head)
{
	head->prev->next = head;
	head->next->prev = head;
}

/**
 * list_for_each - Iterate over a list
 * @curr: the (struct list_head *) to use as a loop cursor
 */
#define list_for_each(curr, head)					       \
	for (curr = (head)->next; curr != (head); curr = curr->next)

/**
 * list_for_each_safe - Iterate over a list where @curr can be safely removed
 * @curr: the (struct list_head *) to use as a loop cursor
 * @temp: the (struct list_head *) to use as temporary storage
 */
#define list_for_each_safe(curr, temp, head)				       \
	for (curr = (head)->next, temp = curr->next;			       \
		curr != (head);						       \
		curr = temp, temp = curr->next)

/**
 * list_lru_add - Add @new to @head
 */
static inline void list_lru_add(struct list_head *head, struct list_head *new)
{
	list_add(head, new);
}

/**
 * list_lru_del - Delete @node from the list lru
 */
static inline void list_lru_del(struct list_head *node)
{
	list_del(node);
}

/**
 * list_lru_touch - Touch @node
 */
static inline void list_lru_touch(struct list_head *head,
				  struct list_head *node)
{
	list_lru_del(node);
	list_lru_add(head, node);
}

/**
 * list_lru_for_each - Iterate over a list lru start from the least active node
 * @curr: the (struct list_head *) to use as a loop cursor
 */
#define list_lru_for_each(curr, head)					       \
	for (curr = (head)->prev; curr != (head); curr = curr->prev)

/**
 * list_lru_next_active - Get the next more active node
 */
static inline struct list_head *list_lru_next_active(struct list_head *node)
{
	return node->prev;
}

/**
 * list_lru_peek - Get the least active node from @head
 * 
 * Note: caller should make sure @head is not empty
 */
static inline struct list_head *list_lru_peek(struct list_head *head)
{
	assert(!list_empty(head));
	return list_lru_next_active(head);
}

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node **pprev;
	struct hlist_node *next;
};

/**
 * hlist_head_init - Initialize hlist @head
 */
static inline void hlist_head_init(struct hlist_head *head)
{
	head->first = NULL;
}

/**
 * hlist_empty - Check if @head is empty
 */
static inline bool hlist_empty(const struct hlist_head *head)
{
	return head->first == NULL;
}

/**
 * hlist_add - Add @new to @head
 */
static inline void hlist_add(struct hlist_head *head, struct hlist_node *new)
{
	new->pprev = &head->first;
	struct hlist_node *first = head->first;
	new->next = first;
	if (first)
		first->pprev = &new->next;
	head->first = new;
}

/**
 * hlist_add_before - Add @new before @node
 */
static inline void hlist_add_before(struct hlist_node *node,
				    struct hlist_node *new)
{
	new->pprev = node->pprev;
	new->next = node;
	node->pprev = &new->next;
	*(new->pprev) = new;
}

/**
 * hlist_del - Delete @node from the hlist
 */
static inline void hlist_del(struct hlist_node *node)
{
	struct hlist_node **pprev = node->pprev;
	struct hlist_node *next = node->next;
	*pprev = next;
	if (next)
		next->pprev = pprev;
}

/**
 * hlist_node_fix - Used for replacing a node in the list
 */
static inline void hlist_node_fix(struct hlist_node *node)
{
	*(node->pprev) = node;
	if (node->next)
		node->next->pprev = &node->next;
}

/**
 * hlist_first_node - Get the first node of a hlist
 * @type: the type of the node
 * @member: the (struct hlist_node) member within @type
 *
 * Note: caller should make sure the hlist is not empty.
 */
#define hlist_first_node(head, type, member) ({				       \
	assert(!hlist_empty(head));					       \
	container_of((head)->first, type, member);})

/**
 * hlist_for_each - Iterate over a hlist
 * @curr: the (struct hlist_node *) to use as a loop cursor
 */
#define hlist_for_each(curr, head)					       \
	for (curr = (head)->first; curr ; curr = curr->next)

/**
 * hlist_for_each_safe - Iterate over a hlist where @curr can be safely removed
 * @curr: the (struct hlist_node *) to use as a loop cursor
 * @temp: the (struct hlist_node *) to use as temporary storage
 */
#define hlist_for_each_safe(curr, temp, head)				       \
	for (curr = (head)->first; curr && ({ temp = curr->next; 1; });	       \
		curr = temp)

#endif
