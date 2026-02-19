/*
 * Copyright (c) 2006, Adam Dunkels
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "ubasic.h"
#include "tokenizer.h"
#include <string.h>

/* Redirections for Circle / Bare Metal */
extern void circle_basic_print(const char *s);
extern void circle_basic_print_num(int n);

static void (*poke_ptr)(VARIABLE_TYPE, VARIABLE_TYPE) = NULL;

void ubasic_set_poke_function(void (*f)(VARIABLE_TYPE, VARIABLE_TYPE)) {
  poke_ptr = f;
}

#define DEBUG 0
#if DEBUG
#define DEBUG_PRINTF(...) 
#else
#define DEBUG_PRINTF(...)
#endif

#define HALT() while(1)

static char const *program_ptr;
#define MAX_STRINGLEN 40
static char string[MAX_STRINGLEN];

#define MAX_GOSUB_STACK_DEPTH 10
static int gosub_stack[MAX_GOSUB_STACK_DEPTH];
static int gosub_stack_ptr;

struct for_state {
  int line_after_for;
  int for_variable;
  int to;
};
#define MAX_FOR_STACK_DEPTH 4
static struct for_state for_stack[MAX_FOR_STACK_DEPTH];
static int for_stack_ptr;

/* Fixed-size index table instead of malloc'd linked list */
struct line_index {
  int line_number;
  char const *program_text_position;
};
#define MAX_LINE_INDEXES 256
static struct line_index line_index_table[MAX_LINE_INDEXES];
static int line_index_current_ptr = 0;

#define MAX_VARNUM 26
static VARIABLE_TYPE variables[MAX_VARNUM];

static int ended;

static VARIABLE_TYPE expr(void);
static void line_statement(void);
static void statement(void);

peek_func peek_function = (void*)0;
poke_func poke_function = (void*)0;

/*---------------------------------------------------------------------------*/
void ubasic_init(const char *program) {
  program_ptr = program;
  for_stack_ptr = gosub_stack_ptr = 0;
  line_index_current_ptr = 0; // Reset static index
  tokenizer_init(program);
  ended = 0;
}
/*---------------------------------------------------------------------------*/
void ubasic_init_peek_poke(const char *program, peek_func peek, poke_func poke) {
  program_ptr = program;
  for_stack_ptr = gosub_stack_ptr = 0;
  line_index_current_ptr = 0;
  peek_function = peek;
  poke_function = poke;
  tokenizer_init(program);
  ended = 0;
}
/*---------------------------------------------------------------------------*/
static void accept(int token) {
  if(token != tokenizer_token()) {
    circle_basic_print("Unexpected token error\n");
    HALT();
  }
  tokenizer_next();
}
/*---------------------------------------------------------------------------*/
static int varfactor(void) {
  int r = ubasic_get_variable(tokenizer_variable_num());
  accept(TOKENIZER_VARIABLE);
  return r;
}
/*---------------------------------------------------------------------------*/
static int factor(void) {
  int r;
  switch(tokenizer_token()) {
  case TOKENIZER_NUMBER:
    r = tokenizer_num();
    accept(TOKENIZER_NUMBER);
    break;
  case TOKENIZER_LEFTPAREN:
    accept(TOKENIZER_LEFTPAREN);
    r = expr();
    accept(TOKENIZER_RIGHTPAREN);
    break;
  default:
    r = varfactor();
    break;
  }
  return r;
}
/*---------------------------------------------------------------------------*/
static int term(void) {
  int f1, f2, op;
  f1 = factor();
  op = tokenizer_token();
  while(op == TOKENIZER_ASTR || op == TOKENIZER_SLASH || op == TOKENIZER_MOD) {
    tokenizer_next();
    f2 = factor();
    if (op == TOKENIZER_ASTR) f1 = f1 * f2;
    else if (op == TOKENIZER_SLASH) f1 = f1 / f2;
    else if (op == TOKENIZER_MOD) f1 = f1 % f2;
    op = tokenizer_token();
  }
  return f1;
}
/*---------------------------------------------------------------------------*/
static VARIABLE_TYPE expr(void) {
  int t1, t2, op;
  t1 = term();
  op = tokenizer_token();
  while(op == TOKENIZER_PLUS || op == TOKENIZER_MINUS || op == TOKENIZER_AND || op == TOKENIZER_OR) {
    tokenizer_next();
    t2 = term();
    if (op == TOKENIZER_PLUS) t1 = t1 + t2;
    else if (op == TOKENIZER_MINUS) t1 = t1 - t2;
    else if (op == TOKENIZER_AND) t1 = t1 & t2;
    else if (op == TOKENIZER_OR) t1 = t1 | t2;
    op = tokenizer_token();
  }
  return t1;
}
/*---------------------------------------------------------------------------*/
static int relation(void) {
  int r1, r2, op;
  r1 = expr();
  op = tokenizer_token();
  while(op == TOKENIZER_LT || op == TOKENIZER_GT || op == TOKENIZER_EQ) {
    tokenizer_next();
    r2 = expr();
    if (op == TOKENIZER_LT) r1 = r1 < r2;
    else if (op == TOKENIZER_GT) r1 = r1 > r2;
    else if (op == TOKENIZER_EQ) r1 = r1 == r2;
    op = tokenizer_token();
  }
  return r1;
}
/*---------------------------------------------------------------------------*/
static void index_free(void) {
    line_index_current_ptr = 0;
}
/*---------------------------------------------------------------------------*/
static char const* index_find(int linenum) {
  for(int i = 0; i < line_index_current_ptr; i++) {
    if(line_index_table[i].line_number == linenum) {
      return line_index_table[i].program_text_position;
    }
  }
  return (void*)0;
}
/*---------------------------------------------------------------------------*/
static void index_add(int linenum, char const* sourcepos) {
  if(index_find(linenum)) return;
  if(line_index_current_ptr < MAX_LINE_INDEXES) {
    line_index_table[line_index_current_ptr].line_number = linenum;
    line_index_table[line_index_current_ptr].program_text_position = sourcepos;
    line_index_current_ptr++;
  }
}
/*---------------------------------------------------------------------------*/
static void jump_linenum_slow(int linenum) {
  tokenizer_init(program_ptr);
  while(tokenizer_num() != linenum) {
    do {
      do {
        tokenizer_next();
      } while(tokenizer_token() != TOKENIZER_CR && tokenizer_token() != TOKENIZER_ENDOFINPUT);
      if(tokenizer_token() == TOKENIZER_CR) tokenizer_next();
    } while(tokenizer_token() != TOKENIZER_NUMBER);
  }
}
/*---------------------------------------------------------------------------*/
static void jump_linenum(int linenum) {
  char const* pos = index_find(linenum);
  if(pos != (void*)0) tokenizer_goto(pos);
  else jump_linenum_slow(linenum);
}
/*---------------------------------------------------------------------------*/
static void goto_statement(void) {
  accept(TOKENIZER_GOTO);
  jump_linenum(tokenizer_num());
}
/*---------------------------------------------------------------------------*/
static void print_statement(void) {
  accept(TOKENIZER_PRINT);
  do {
    if(tokenizer_token() == TOKENIZER_STRING) {
      tokenizer_string(string, sizeof(string));
      circle_basic_print(string);
      tokenizer_next();
    } else if(tokenizer_token() == TOKENIZER_COMMA) {
      circle_basic_print(" ");
      tokenizer_next();
    } else if(tokenizer_token() == TOKENIZER_SEMICOLON) {
      tokenizer_next();
    } else if(tokenizer_token() == TOKENIZER_VARIABLE || tokenizer_token() == TOKENIZER_NUMBER) {
      circle_basic_print_num(expr());
    } else break;
  } while(tokenizer_token() != TOKENIZER_CR && tokenizer_token() != TOKENIZER_ENDOFINPUT);
  circle_basic_print("\n");
  tokenizer_next();
}
/*---------------------------------------------------------------------------*/
static void if_statement(void) {
  int r;
  accept(TOKENIZER_IF);
  r = relation();
  accept(TOKENIZER_THEN);
  if(r) statement();
  else {
    do {
      tokenizer_next();
    } while(tokenizer_token() != TOKENIZER_ELSE && tokenizer_token() != TOKENIZER_CR && tokenizer_token() != TOKENIZER_ENDOFINPUT);
    if(tokenizer_token() == TOKENIZER_ELSE) {
      tokenizer_next();
      statement();
    } else if(tokenizer_token() == TOKENIZER_CR) tokenizer_next();
  }
}
/*---------------------------------------------------------------------------*/
static void let_statement(void) {
  int var = tokenizer_variable_num();
  accept(TOKENIZER_VARIABLE);
  accept(TOKENIZER_EQ);
  ubasic_set_variable(var, expr());
  accept(TOKENIZER_CR);
}
/*---------------------------------------------------------------------------*/
static void gosub_statement(void) {
  int linenum;
  accept(TOKENIZER_GOSUB);
  linenum = tokenizer_num();
  accept(TOKENIZER_NUMBER);
  accept(TOKENIZER_CR);
  if(gosub_stack_ptr < MAX_GOSUB_STACK_DEPTH) {
    gosub_stack[gosub_stack_ptr] = tokenizer_num();
    gosub_stack_ptr++;
    jump_linenum(linenum);
  }
}
/*---------------------------------------------------------------------------*/
static void return_statement(void) {
  accept(TOKENIZER_RETURN);
  if(gosub_stack_ptr > 0) {
    gosub_stack_ptr--;
    jump_linenum(gosub_stack[gosub_stack_ptr]);
  }
}
/*---------------------------------------------------------------------------*/
static void next_statement(void) {
  int var;
  accept(TOKENIZER_NEXT);
  var = tokenizer_variable_num();
  accept(TOKENIZER_VARIABLE);
  if(for_stack_ptr > 0 && var == for_stack[for_stack_ptr - 1].for_variable) {
    ubasic_set_variable(var, ubasic_get_variable(var) + 1);
    if(ubasic_get_variable(var) <= for_stack[for_stack_ptr - 1].to) {
      jump_linenum(for_stack[for_stack_ptr - 1].line_after_for);
    } else {
      for_stack_ptr--;
      accept(TOKENIZER_CR);
    }
  } else accept(TOKENIZER_CR);
}
/*---------------------------------------------------------------------------*/
static void for_statement(void) {
  int for_variable, to;
  accept(TOKENIZER_FOR);
  for_variable = tokenizer_variable_num();
  accept(TOKENIZER_VARIABLE);
  accept(TOKENIZER_EQ);
  ubasic_set_variable(for_variable, expr());
  accept(TOKENIZER_TO);
  to = expr();
  accept(TOKENIZER_CR);
  if(for_stack_ptr < MAX_FOR_STACK_DEPTH) {
    for_stack[for_stack_ptr].line_after_for = tokenizer_num();
    for_stack[for_stack_ptr].for_variable = for_variable;
    for_stack[for_stack_ptr].to = to;
    for_stack_ptr++;
  }
}
/*---------------------------------------------------------------------------*/
static void peek_statement(void) {
  VARIABLE_TYPE peek_addr;
  int var;
  accept(TOKENIZER_PEEK);
  peek_addr = expr();
  accept(TOKENIZER_COMMA);
  var = tokenizer_variable_num();
  accept(TOKENIZER_VARIABLE);
  accept(TOKENIZER_CR);
  if(peek_function) ubasic_set_variable(var, peek_function(peek_addr));
}
/*---------------------------------------------------------------------------*/
static void poke_statement(void) {
  accept(TOKENIZER_POKE);
  VARIABLE_TYPE addr = expr();
  accept(TOKENIZER_COMMA);
  VARIABLE_TYPE val = expr();
  accept(TOKENIZER_CR);

  if (poke_ptr != NULL) {
    poke_ptr(addr, val);
  }
}
/*---------------------------------------------------------------------------*/
static void end_statement(void) {
  accept(TOKENIZER_END);
  ended = 1;
}
/*---------------------------------------------------------------------------*/
static void statement(void) {
  int token = tokenizer_token();
  switch(token) {
  case TOKENIZER_PRINT:    print_statement(); break;
  case TOKENIZER_IF:       if_statement(); break;
  case TOKENIZER_GOTO:     goto_statement(); break;
  case TOKENIZER_GOSUB:    gosub_statement(); break;
  case TOKENIZER_RETURN:   return_statement(); break;
  case TOKENIZER_FOR:      for_statement(); break;
  case TOKENIZER_PEEK:     peek_statement(); break;
  case TOKENIZER_POKE:     poke_statement(); break;
  case TOKENIZER_NEXT:     next_statement(); break;
  case TOKENIZER_END:      end_statement(); break;
  case TOKENIZER_LET:      accept(TOKENIZER_LET); /* Fall through */
  case TOKENIZER_VARIABLE: let_statement(); break;
  default:
    circle_basic_print("Unknown statement\n");
    HALT();
  }
}
/*---------------------------------------------------------------------------*/
static void line_statement(void) {
  index_add(tokenizer_num(), tokenizer_pos());
  accept(TOKENIZER_NUMBER);
  statement();
}
/*---------------------------------------------------------------------------*/
void ubasic_run(void) {
  if(!tokenizer_finished() && !ended) line_statement();
}
/*---------------------------------------------------------------------------*/
int ubasic_finished(void) {
  return ended || tokenizer_finished();
}
/*---------------------------------------------------------------------------*/
void ubasic_set_variable(int varnum, VARIABLE_TYPE value) {
  if(varnum >= 0 && varnum < MAX_VARNUM) variables[varnum] = value;
}
/*---------------------------------------------------------------------------*/
VARIABLE_TYPE ubasic_get_variable(int varnum) {
  if(varnum >= 0 && varnum < MAX_VARNUM) return variables[varnum];
  return 0;
}
