#ifndef __WATCHPOINT_H__
#define __WATCHPOINT_H__

#include "common.h"

#define WP_EXPR_LEN 256

typedef struct watchpoint {
	int NO;
	struct watchpoint *next;
	char expression[WP_EXPR_LEN];
	uint32_t value;
} WP;

WP *new_wp(void);
void free_wp(WP *wp);
WP *add_watchpoint(char *expression, bool *success);
bool delete_watchpoint(int no);
void print_watchpoints(void);
bool check_watchpoints(swaddr_t eip);

#endif
