#include "monitor/watchpoint.h"
#include "monitor/expr.h"

#define NR_WP 32

static WP wp_pool[NR_WP];
static WP *head, *free_;

void init_wp_pool() {
	int i;
	for(i = 0; i < NR_WP; i ++) {
		wp_pool[i].NO = i;
		wp_pool[i].next = &wp_pool[i + 1];
	}
	wp_pool[NR_WP - 1].next = NULL;

	head = NULL;
	free_ = wp_pool;
}

WP *new_wp(void) {
	WP *wp;

	Assert(free_ != NULL, "No free watchpoint");

	wp = free_;
	free_ = free_->next;

	wp->next = head;
	head = wp;

	return wp;
}

void free_wp(WP *wp) {
	WP **current;

	Assert(wp != NULL, "Can not free a null watchpoint");

	current = &head;
	while(*current != NULL && *current != wp) {
		current = &(*current)->next;
	}

	Assert(*current == wp, "Watchpoint %d is not in use", wp->NO);

	*current = wp->next;
	wp->next = free_;
	free_ = wp;
}

