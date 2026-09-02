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
	EQ
};

static struct rule {
	char *regex;
	int token_type;
} rules[] = {
	{"[[:space:]]+", NOTYPE},
	{"[0-9]+", NUM},
	{"==", EQ},
	{"\\+", '+'},
	{"\\-", '-'},
	{"\\*", '*'},
	{"\\/", '/'},
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
						if ((size_t)substr_len >= sizeof(tokens[nr_token].str)) {
							printf("number too long\n");
							return false;
						}
						tokens[nr_token].type = NUM;
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

static bool check_parentheses(int p, int q, bool *success) {
	int depth = 0;
	int i;
	bool enclosed;

	enclosed = (tokens[p].type == '(' && tokens[q].type == ')');

	for (i = p; i <= q; i++) {

		if (tokens[i].type == '(') {
			depth++;
		} else if (tokens[i].type == ')') {
			depth--;

			if (depth < 0) {
			*success = false;
			return false;
		}

		if (depth == 0 && i < q) {
			enclosed = false;
		}
	}
	if (depth != 0) {
		*success = false;
		return false;
	}
	}
	return enclosed;
}


static int precedence(int op) {
	switch (op) {
		case '+':
		case '-':
			return 1;

		case '*':
		case '/':
			return 2;
			
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

static uint32_t eval(int p, int q, bool *success) {
	int op;
	int op_type;
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

		if(tokens[p].type != NUM) {
			*success = false;
			return 0;
		}

		errno = 0;
		endptr = NULL;

		val = strtoul(tokens[p].str, &endptr, 10);

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
		*success = false;
		return 0;
	}

	op_type = tokens[op].type;

	val1 = eval(p, op - 1, success);

	if(!*success) {
		return 0;
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

	*success = true;

	result = eval(0, nr_token - 1, success);

	if (!*success) {
		return 0;
	}

	return result;
}

