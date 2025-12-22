// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024-2025, Shu De Zheng <imchuncai@gmail.com>. All Rights Reserved.

#ifndef __UMEM_CACHE_LIST_H
#define __UMEM_CACHE_LIST_H

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
	assert(prev);
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
 * list_first_entry - Get the first entry of @head
 * @type: the struct type of the entry
 * @member: the name of the list_head within the struct
 *
 * Note: caller should make sure @head is not empty.
 */
#define list_first_entry(head, type, member)				       \
	container_of((head)->next, type, member)

/**
 * list_entry_is_head - Check if @entry is the container of @head
 * @member: the name of the list_head within the struct
 */
#define list_entry_is_head(entry, head, member)				       \
	(&entry->member == (head))

/**
 * list_next_entry - Get the next entry
 * @entry: current entry
 * @member: the name of the list_head within the struct
 */
#define list_next_entry(entry, member)					       \
	container_of((entry)->member.next, typeof(*(entry)), member)

/**
 * list_for_each_entry - Iterate over @head
 * @curr: the (struct *) entry to use as a loop cursor
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry(curr, head, member)				       \
	for (curr = list_first_entry(head, typeof(*curr), member);	       \
	     !list_entry_is_head(curr, head, member);			       \
	     curr = list_next_entry(curr, member))

/**
 * list_for_each_entry_safe - Iterate over @head where @curr can be safely removed
 * @curr: the (struct *) entry to use as a loop cursor
 * @member: the name of the list_head within the struct
 */
#define list_for_each_entry_safe(curr, temp, head, member)		       \
	for (curr = list_first_entry(head, typeof(*curr), member),	       \
	     temp = list_next_entry(curr, member);			       \
	     !list_entry_is_head(curr, head, member); 			       \
	     curr = temp, temp = list_next_entry(temp, member))

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
	return head->prev;
}

struct hlist_head {
	struct hlist_node *first;
};

struct hlist_node {
	struct hlist_node **prev_next;
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
	new->prev_next = &head->first;
	struct hlist_node *first = head->first;
	new->next = first;
	if (first)
		first->prev_next = &new->next;
	head->first = new;
}

/**
 * hlist_del - Delete @node from the hlist
 */
static inline void hlist_del(struct hlist_node *node)
{
	struct hlist_node **prev_next = node->prev_next;
	struct hlist_node *next = node->next;
	*prev_next = next;
	if (next)
		next->prev_next = prev_next;
}

/**
 * hlist_node_fix - Used for replacing a node in the list
 */
static inline void hlist_node_fix(struct hlist_node *node)
{
	*(node->prev_next) = node;
	if (node->next)
		node->next->prev_next = &node->next;
}

/**
 * hlist_for_each - Iterate over @head
 * @curr: the (struct hlist_node *) to use as a loop cursor
 */
#define hlist_for_each(curr, head)					       \
	for (curr = (head)->first; curr ; curr = curr->next)

/**
 * hlist_for_each_safe - Iterate over @head where @curr can be safely removed
 * @curr: the (struct hlist_node *) to use as a loop cursor
 * @temp: the (struct hlist_node *) to use as temporary storage
 */
#define hlist_for_each_safe(curr, temp, head)				       \
	for (curr = (head)->first; curr && ({ temp = curr->next; 1; });	       \
		curr = temp)

#endif
