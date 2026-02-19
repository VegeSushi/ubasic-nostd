/*
 * Copyright (c) 2006, Adam Dunkels
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include "tokenizer.h"
#include <string.h>

#define DEBUG 0
#if DEBUG
#define DEBUG_PRINTF(...) 
#else
#define DEBUG_PRINTF(...)
#endif

static char const *ptr, *nextptr;

#define MAX_NUMLEN 6

struct keyword_token {
  char *keyword;
  int token;
};

static int current_token = TOKENIZER_ERROR;

static const struct keyword_token keywords[] = {
  {"let", TOKENIZER_LET},
  {"print", TOKENIZER_PRINT},
  {"if", TOKENIZER_IF},
  {"then", TOKENIZER_THEN},
  {"else", TOKENIZER_ELSE},
  {"for", TOKENIZER_FOR},
  {"to", TOKENIZER_TO},
  {"next", TOKENIZER_NEXT},
  {"goto", TOKENIZER_GOTO},
  {"gosub", TOKENIZER_GOSUB},
  {"return", TOKENIZER_RETURN},
  {"call", TOKENIZER_CALL},
  {"rem", TOKENIZER_REM},
  {"peek", TOKENIZER_PEEK},
  {"poke", TOKENIZER_POKE},
  {"end", TOKENIZER_END},
  {(void*)0, TOKENIZER_ERROR}
};

/* --- Internal Bare Metal Helpers --- */

static int is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static int custom_atoi(const char *s) {
    int res = 0;
    while (is_digit(*s)) {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

static char* custom_strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return (void*)0;
    }
    return (char *)s;
}

/*---------------------------------------------------------------------------*/
static int singlechar(void) {
  if(*ptr == '\n') return TOKENIZER_CR;
  if(*ptr == ',')  return TOKENIZER_COMMA;
  if(*ptr == ';')  return TOKENIZER_SEMICOLON;
  if(*ptr == '+')  return TOKENIZER_PLUS;
  if(*ptr == '-')  return TOKENIZER_MINUS;
  if(*ptr == '&')  return TOKENIZER_AND;
  if(*ptr == '|')  return TOKENIZER_OR;
  if(*ptr == '*')  return TOKENIZER_ASTR;
  if(*ptr == '/')  return TOKENIZER_SLASH;
  if(*ptr == '%')  return TOKENIZER_MOD;
  if(*ptr == '(')  return TOKENIZER_LEFTPAREN;
  if(*ptr == '#')  return TOKENIZER_HASH;
  if(*ptr == ')')  return TOKENIZER_RIGHTPAREN;
  if(*ptr == '<')  return TOKENIZER_LT;
  if(*ptr == '>')  return TOKENIZER_GT;
  if(*ptr == '=')  return TOKENIZER_EQ;
  return 0;
}

/*---------------------------------------------------------------------------*/
static int get_next_token(void) {
  struct keyword_token const *kt;
  int i;

  if(*ptr == 0) return TOKENIZER_ENDOFINPUT;

  if(is_digit(*ptr)) {
    for(i = 0; i < MAX_NUMLEN; ++i) {
      if(!is_digit(ptr[i])) {
        if(i > 0) {
          nextptr = ptr + i;
          return TOKENIZER_NUMBER;
        } else return TOKENIZER_ERROR;
      }
    }
    return TOKENIZER_ERROR;
  } else if(singlechar()) {
    nextptr = ptr + 1;
    return singlechar();
  } else if(*ptr == '"') {
    nextptr = ptr;
    do {
      ++nextptr;
    } while(*nextptr != '"' && *nextptr != 0);
    if (*nextptr == '"') ++nextptr;
    return TOKENIZER_STRING;
  } else {
    for(kt = keywords; kt->keyword != (void*)0; ++kt) {
      if(strncmp(ptr, kt->keyword, strlen(kt->keyword)) == 0) {
        nextptr = ptr + strlen(kt->keyword);
        return kt->token;
      }
    }
  }

  if(*ptr >= 'a' && *ptr <= 'z') {
    nextptr = ptr + 1;
    return TOKENIZER_VARIABLE;
  }

  return TOKENIZER_ERROR;
}

/*---------------------------------------------------------------------------*/
void tokenizer_goto(const char *program) {
  ptr = program;
  current_token = get_next_token();
}

/*---------------------------------------------------------------------------*/
void tokenizer_init(const char *program) {
  tokenizer_goto(program);
}

/*---------------------------------------------------------------------------*/
int tokenizer_token(void) {
  return current_token;
}

/*---------------------------------------------------------------------------*/
void tokenizer_next(void) {
  if(tokenizer_finished()) return;

  ptr = nextptr;
  while(*ptr == ' ' || *ptr == '\t' || *ptr == '\r') {
    ++ptr;
  }
  current_token = get_next_token();

  if(current_token == TOKENIZER_REM) {
      while(!(*nextptr == '\n' || *nextptr == 0)) {
        ++nextptr;
      }
      if(*nextptr == '\n') ++nextptr;
      tokenizer_next();
  }
}

/*---------------------------------------------------------------------------*/
VARIABLE_TYPE tokenizer_num(void) {
  return (VARIABLE_TYPE)custom_atoi(ptr);
}

/*---------------------------------------------------------------------------*/
void tokenizer_string(char *dest, int len) {
  char *string_end;
  int string_len;

  if(tokenizer_token() != TOKENIZER_STRING) return;

  string_end = custom_strchr(ptr + 1, '"');
  if(string_end == (void*)0) return;

  string_len = string_end - ptr - 1;
  if(len < string_len) string_len = len;

  memcpy(dest, ptr + 1, string_len);
  dest[string_len] = 0;
}

/*---------------------------------------------------------------------------*/
void tokenizer_error_print(void) {
  // Can be mapped to Circle Logger if needed
}

/*---------------------------------------------------------------------------*/
int tokenizer_finished(void) {
  return *ptr == 0 || current_token == TOKENIZER_ENDOFINPUT;
}

/*---------------------------------------------------------------------------*/
int tokenizer_variable_num(void) {
  return *ptr - 'a';
}

/*---------------------------------------------------------------------------*/
char const * tokenizer_pos(void) {
    return ptr;
}
