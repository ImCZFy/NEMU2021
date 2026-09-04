#include "monitor/monitor.h"
#include "monitor/expr.h"
#include "monitor/watchpoint.h"
#include "nemu.h"

#include <stdlib.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>

void cpu_exec(uint32_t);

/* We use the `readline' library to provide more flexibility to read from stdin. */
char* rl_gets() {
	static char *line_read = NULL;

	if (line_read) {
		free(line_read);
		line_read = NULL;
	}

	line_read = readline("(nemu) ");

	if (line_read && *line_read) {
		add_history(line_read);
	}

	return line_read;
}

static int cmd_c(char *args) {
	cpu_exec(-1);
	return 0;
}

static int cmd_q(char *args) {
	return -1;
}

static int cmd_help(char *args);

static int cmd_si(char *args);

static int cmd_info(char *args);

static int cmd_x(char *args);

static int cmd_p(char *args);

static int cmd_w(char *args);

static int cmd_d(char *args);

static struct {
	char *name;
	char *description;
	int (*handler) (char *);
} cmd_table [] = {
	{ "help", "Display informations about all supported commands", cmd_help },
	{ "c", "Continue the execution of the program", cmd_c },
	{ "q", "Exit NEMU", cmd_q },
	{ "si", "Step N instructions exactly. The default value of N is 1.", cmd_si },
	{ "info", "Print registers or watchpoints. Usage: info r|w", cmd_info },
	{ "x", "Scan the memory. Usage: x N EXPR", cmd_x },
	{ "p", "Evaluate the expression EXPR and print its value. Usage: p EXPR", cmd_p },
	{ "w", "Set a watchpoint. Usage: w EXPR", cmd_w },
	{ "d", "Delete a watchpoint. Usage: d N", cmd_d },
};

#define NR_CMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

static int cmd_help(char *args) {
	/* extract the first argument */
	char *arg = strtok(NULL, " ");
	int i;

	if(arg == NULL) {
		/* no argument given */
		for(i = 0; i < NR_CMD; i ++) {
			printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
		}
	}
	else {
		for(i = 0; i < NR_CMD; i ++) {
			if(strcmp(arg, cmd_table[i].name) == 0) {
				printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
				return 0;
			}
		}
		printf("Unknown command '%s'\n", arg);
	}
	return 0;
}

static int cmd_si(char *args) {
    unsigned int n = 1;
    char extra;
	// Ignore the front space, then read u and c, %u represents an unsigned integer, %c represents extra chars which will be saved in extra.
    if (args != NULL && sscanf(args, " %u %c", &n, &extra) != 1) {
        printf("Usage: si [N]\n");
        return 0;
    }

    cpu_exec(n);
    return 0;
}

static int cmd_info(char *args) {
    char subcmd;
    char extra;
	int i;

    if (args == NULL || sscanf(args, " %c %c", &subcmd, &extra) != 1) {
        printf("Usage: info r|w\n");
        return 0;
    }

	if (subcmd == 'r') {
		for (i = 0; i < 8; i++) {
			printf("%-3s  0x%08x  %u\n", regsl[i], reg_l(i), reg_l(i));
		}

		printf("eip  0x%08x  %u\n", cpu.eip, cpu.eip);
		printf("eflags  0x%08x\n", cpu.eflags.val);

	} else if (subcmd == 'w') {
		print_watchpoints();
	}
	else {
		printf("Unknown subcommand '%c'\n", subcmd);
	}
    return 0;
}

static int cmd_x(char *args) {
    unsigned int n;
	int expression_position;
	uint32_t address;
	uint32_t current;
	uint32_t value;
	bool success;
	unsigned int i;

	expression_position = 0;

	if (args == NULL ||
			sscanf(args, " %u%n", &n, &expression_position) != 1 ||
			n == 0 ||
			args[expression_position] == '\0' ||
			!isspace((unsigned char)args[expression_position])) {
        printf("Usage: x N EXPR\n");
        return 0;
    }

	while(isspace((unsigned char)args[expression_position])) {
		expression_position ++;
	}

	if(args[expression_position] == '\0') {
		printf("Usage: x N EXPR\n");
		return 0;
	}

	address = expr(args + expression_position, &success);

	if(!success) {
		printf("Invalid expression: %s\n", args + expression_position);
		return 0;
	}

    if ((uint64_t)address + (uint64_t)n * 4 > HW_MEM_SIZE) {
        printf("Memory range is out of bounds\n");
        return 0;
    }

    for (i = 0; i < n; i++) {
		current = address + i * 4;
		value = swaddr_read(current, 4);

        printf("0x%08x: 0x%08x\n", current, value);
    }
    return 0;
}

static int cmd_p(char *args) {
	bool success;
	uint32_t result;

	if (args == NULL) {
		printf("Usage: p EXPR\n");
		return 0;
	}

	result = expr(args, &success);

	if (success) {
		printf("Result: %u (0x%08x)\n", result, result);
	} else {
		printf("Invalid expression: %s\n", args);
	}
	return 0;
}

static int cmd_w(char *args) {
	WP *wp;
	bool success;

	if(args == NULL) {
		printf("Usage: w EXPR\n");
		return 0;
	}

	while(isspace((unsigned char)*args)) {
		args ++;
	}

	if(*args == '\0') {
		printf("Usage: w EXPR\n");
		return 0;
	}

	if(strlen(args) >= WP_EXPR_LEN) {
		printf("Watchpoint expression is too long\n");
		return 0;
	}

	wp = add_watchpoint(args, &success);
	if(!success) {
		printf("Invalid expression: %s\n", args);
		return 0;
	}

	printf("Watchpoint %d: %s\n", wp->NO, wp->expression);
	printf("Initial value = %u (0x%08x)\n", wp->value, wp->value);
	return 0;
}

static int cmd_d(char *args) {
	int no;
	char extra;

	if(args == NULL || sscanf(args, " %d %c", &no, &extra) != 1 || no < 0) {
		printf("Usage: d N\n");
		return 0;
	}

	if(!delete_watchpoint(no)) {
		printf("Watchpoint %d does not exist\n", no);
		return 0;
	}

	printf("Watchpoint %d deleted\n", no);
	return 0;
}

void ui_mainloop() {
	while(1) {
		char *str = rl_gets();
		char *str_end = str + strlen(str);

		/* extract the first token as the command */
		char *cmd = strtok(str, " ");
		if(cmd == NULL) { continue; }

		/* treat the remaining string as the arguments,
		 * which may need further parsing
		 */
		char *args = cmd + strlen(cmd) + 1;
		if(args >= str_end) {
			args = NULL;
		}

#ifdef HAS_DEVICE
		extern void sdl_clear_event_queue(void);
		sdl_clear_event_queue();
#endif

		int i;
		for(i = 0; i < NR_CMD; i ++) {
			if(strcmp(cmd, cmd_table[i].name) == 0) {
				if(cmd_table[i].handler(args) < 0) { return; }
				break;
			}
		}

		if(i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
	}
}
