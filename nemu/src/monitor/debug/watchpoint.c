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
		wp_pool[i].expression[0] = '\0';
		wp_pool[i].value = 0;
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

	wp->expression[0] = '\0';
	wp->value = 0;
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
	wp->expression[0] = '\0';
	wp->value = 0;
	wp->next = free_;
	free_ = wp;
}

WP *add_watchpoint(char *expression, bool *success) {
	WP *wp;
	uint32_t initial_value;
	size_t expression_length;

	if(success == NULL) {
		return NULL;
	}

	*success = false;
	if(expression == NULL) {
		return NULL;
	}

	expression_length = strlen(expression);
	if(expression_length == 0 || expression_length >= WP_EXPR_LEN) {
		return NULL;
	}

	initial_value = expr(expression, success);
	if(!*success) {
		return NULL;
	}

	wp = new_wp();
	memcpy(wp->expression, expression, expression_length + 1);
	wp->value = initial_value;

	return wp;
}

bool delete_watchpoint(int no) {
	WP *current;

	current = head;
	while(current != NULL) {
		if(current->NO == no) {
			free_wp(current);
			return true;
		}
		current = current->next;
	}

	return false;
}

void print_watchpoints(void) {
	WP *current;

	if(head == NULL) {
		printf("No watchpoints.\n");
		return;
	}

	printf("Num  Value       Expression\n");
	current = head;
	while(current != NULL) {
		printf("%-4d 0x%08x  %s\n",
				current->NO, current->value, current->expression);
		current = current->next;
	}
}

bool check_watchpoints(swaddr_t eip) {
	WP *current;
	bool success;
	bool triggered;
	uint32_t old_value;
	uint32_t new_value;

	triggered = false;
	current = head;

	while(current != NULL) {
		new_value = expr(current->expression, &success);

		if(!success) {
			printf("Watchpoint %d has an invalid expression: %s\n",
					current->NO, current->expression);
			triggered = true;
			current = current->next;
			continue;
		}

		if(new_value != current->value) {
			old_value = current->value;
			current->value = new_value;

			printf("Hint watchpoint %d at address 0x%08x\n",
					current->NO, eip);
			printf("Old value = %u (0x%08x)\n", old_value, old_value);
			printf("New value = %u (0x%08x)\n", new_value, new_value);
			triggered = true;
		}

		current = current->next;
	}

	return triggered;
}
