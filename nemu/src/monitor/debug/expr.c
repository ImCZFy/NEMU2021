#include "nemu.h"

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>
#include <errno.h>

enum {
	NOTYPE = 256,
	NUM,
	HEX,
	REG,
	EQ,
	NEQ,
	AND,
	OR,
	NEG,
	DEREF
};

static struct rule {
	char *regex;
	int token_type;
} rules[] = {
	{"[[:space:]]+", NOTYPE},
	{"0[xX][0-9a-fA-F]+", HEX},
	{"[0-9]+", NUM},
	{"\\$[a-z][a-z0-9]*", REG},
	{"==", EQ},
	{"!=", NEQ},
	{"&&", AND},
	{"\\|\\|", OR},
	{"\\+", '+'},
	{"\\-", '-'},
	{"\\*", '*'},
	{"\\/", '/'},
	{"!", '!'},
	{"\\(", '('},
	{"\\)", ')'},
};

#define NR_REGEX (sizeof(rules) / sizeof(rules[0]) )

static regex_t re[NR_REGEX];

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
	int i;
	char error_msg[128];
	int ret;

	for(i = 0; i < NR_REGEX; i ++) {
		ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
		if(ret != 0) {
			regerror(ret, &re[i], error_msg, 128);
			Assert(ret == 0, "regex compilation failed: %s\n%s", error_msg, rules[i].regex);
		}
	}
}

typedef struct token {
	int type;
	char str[32];
} Token;

Token tokens[32];
int nr_token;

static bool make_token(char *e) {
	int position = 0;
	int i;
	regmatch_t pmatch;
	
	nr_token = 0;

	while(e[position] != '\0') {
		/* Try all rules one by one. */
		for(i = 0; i < NR_REGEX; i ++) {
			if(regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
				char *substr_start = e + position;
				int substr_len = pmatch.rm_eo;

				Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s", i, rules[i].regex, position, substr_len, substr_len, substr_start);
				position += substr_len;

				if (rules[i].token_type != NOTYPE && nr_token >= (int)(sizeof(tokens) / sizeof(tokens[0]))) {
					printf("too many tokens\n");
					return false;
				}

				switch(rules[i].token_type) {
					case NOTYPE:
						break;
					case NUM:
					case HEX:
					case REG:
						if ((size_t)substr_len >= sizeof(tokens[nr_token].str)) {
							printf("token too long\n");
							return false;
						}
						tokens[nr_token].type = rules[i].token_type;
						memcpy(tokens[nr_token].str, substr_start, substr_len);
						tokens[nr_token].str[substr_len] = '\0';
						nr_token++;
						break;
					default:
						tokens[nr_token].type = rules[i].token_type;
						tokens[nr_token].str[0] = '\0';
						nr_token++;
						break;
				}
				break;
			}
		}
		if(i == NR_REGEX) {
			printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
			return false;
		}
	}

	return true; 
}

static bool token_ends_operand(int type) {
	return type == NUM || type == HEX || type == REG || type == ')';
}

static void classify_unary_operators(void) {
	int i;

	for(i = 0; i < nr_token; i ++) {
		if(tokens[i].type == '-' &&
				(i == 0 || !token_ends_operand(tokens[i - 1].type))) {
			tokens[i].type = NEG;
		}
		else if(tokens[i].type == '*' &&
				(i == 0 || !token_ends_operand(tokens[i - 1].type))) {
			tokens[i].type = DEREF;
		}
	}
}

static bool check_parentheses(int p, int q, bool *success) {
	int depth = 0;
	int i;
	bool enclosed;

	enclosed = (tokens[p].type == '(' && tokens[q].type == ')');

	for(i = p; i <= q; i ++) {
		if(tokens[i].type == '(') {
			depth ++;
		}
		else if(tokens[i].type == ')') {
			depth --;

			if(depth < 0) {
				*success = false;
				return false;
			}
		}

		if(depth == 0 && i < q) {
			enclosed = false;
		}
	}

	if(depth != 0) {
		*success = false;
		return false;
	}

	return enclosed;
}


static int precedence(int op) {
	switch (op) {
		case OR:
			return 1;

		case AND:
			return 2;

		case EQ:
		case NEQ:
			return 3;

		case '+':
		case '-':
			return 4;

		case '*':
		case '/':
			return 5;

		default:
			return -1;
	}
}

static int find_dominant_op(int p, int q) {
	int depth = 0;
	int dominant_op = -1;
	int lowest_precedence = 100;
	int current_precedence;
	int i;

	for (i = p; i <= q; i++) {
		if (tokens[i].type == '(') {
			depth++;
		} else if (tokens[i].type == ')') {
			depth--;
		} else if (depth == 0) {
			current_precedence = precedence(tokens[i].type);
			if (current_precedence > 0 && current_precedence <= lowest_precedence) {
				lowest_precedence = current_precedence;
				dominant_op = i;
			}
		}
	}
	return dominant_op;
}

static bool read_register(char *name, uint32_t *value) {
	char *register_name;
	int i;

	if(name == NULL || value == NULL || name[0] != '$') {
		return false;
	}

	register_name = name + 1;

	for(i = 0; i < 8; i ++) {
		if(strcmp(register_name, regsl[i]) == 0) {
			*value = reg_l(i);
			return true;
		}
		if(strcmp(register_name, regsw[i]) == 0) {
			*value = reg_w(i);
			return true;
		}
		if(strcmp(register_name, regsb[i]) == 0) {
			*value = reg_b(i);
			return true;
		}
	}

	if(strcmp(register_name, "eip") == 0) {
		*value = cpu.eip;
		return true;
	}

	if(strcmp(register_name, "eflags") == 0) {
		*value = cpu.eflags.val;
		return true;
	}

	return false;
}

static uint32_t eval(int p, int q, bool *success) {
	int op;
	int op_type;
	int base;
	bool enclosed;
	uint32_t val1;
	uint32_t val2;

	if(p > q) {
		*success = false;
		return 0;
	}

	if(p == q) {
		char *endptr;
		unsigned long val;

		if(tokens[p].type == REG) {
			if(!read_register(tokens[p].str, &val1)) {
				*success = false;
				return 0;
			}
			return val1;
		}

		if(tokens[p].type != NUM && tokens[p].type != HEX) {
			*success = false;
			return 0;
		}

		errno = 0;
		endptr = NULL;
		base = (tokens[p].type == HEX ? 16 : 10);

		val = strtoul(tokens[p].str, &endptr, base);

		if(errno == ERANGE ||
				endptr == tokens[p].str ||
				*endptr != '\0' ||
				(uint64_t)val > (uint64_t)UINT32_MAX) {
			*success = false;
			return 0;
		}
		return (uint32_t)val;
	}
	enclosed = check_parentheses(p, q, success);

	if(!*success) {
		return 0;
	}

	if(enclosed) {
		return eval(p + 1, q - 1, success);
	}

	op = find_dominant_op(p, q);

	if(op < 0) {
		if(tokens[p].type == '!' ||
				tokens[p].type == NEG ||
				tokens[p].type == DEREF) {
			val1 = eval(p + 1, q, success);
			if(!*success) {
				return 0;
			}

			if(tokens[p].type == NEG) {
				return 0u - val1;
			}

			if(tokens[p].type == DEREF) {
				if((uint64_t)val1 + 4 > HW_MEM_SIZE) {
					*success = false;
					return 0;
				}

				return swaddr_read((swaddr_t)val1, 4);
			}

			return !val1;
		}

		*success = false;
		return 0;
	}

	op_type = tokens[op].type;

	val1 = eval(p, op - 1, success);

	if(!*success) {
		return 0;
	}

	if(op_type == AND && val1 == 0) {
		return 0;
	}

	if(op_type == OR && val1 != 0) {
		return 1;
	}

	val2 = eval(op + 1, q, success);

	if(!*success) {
		return 0;
	}

	switch(op_type) {
		case '+':
			return val1 + val2;

		case '-':
			return val1 - val2;

		case '*':
			return val1 * val2;

		case '/':
			if(val2 == 0) {
				*success = false;
				return 0;
			}

			return val1 / val2;

		case EQ:
			return val1 == val2;

		case NEQ:
			return val1 != val2;

		case AND:
			return val1 && val2;

		case OR:
			return val1 || val2;

		default:
			*success = false;
			return 0;
	}
}

uint32_t expr(char *e, bool *success) {

	uint32_t result = 0;

	if (success == NULL) {
		return 0;
	}

	*success = false;

	if (e == NULL || !make_token(e) || nr_token == 0) {
		return 0;
	}

	classify_unary_operators();

	*success = true;

	result = eval(0, nr_token - 1, success);

	if (!*success) {
		return 0;
	}

	return result;
}
