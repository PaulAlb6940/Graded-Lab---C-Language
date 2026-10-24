// PAUL ALBERTI 260ADB178
// (optional) https://github.com/<your-username>/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: <every Claude.ai, website and book you used; details in comments where you used them>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

typedef enum {
    T_INT,      // integer literal
    T_FLOAT,    // float literal (digits '.' digits)
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW,
    T_LPAREN, T_RPAREN,
    T_EOL       // '\n' OR end of file: both end an expression, and both are
                // where "missing operand" / "missing )" errors are reported
} TokType;

typedef struct {
    int is_float;
    long long i;
    double d;
} Value;

typedef struct {
    TokType type;
    size_t pos;     // 1-based position of the token's first character in the
                    // file; stored in the token because errors are reported
                    // at the start of the token the parser was looking at
    Value val;      // only meaningful for T_INT / T_FLOAT
} Token;

// Contenu du fichier d'entrée, lu une seule fois en mémoire.
// Tous les offsets (donc toutes les positions d'erreur) se calculent dans ce buffer.
static const char *buf;   // whole file content, read once
static size_t len;        // number of bytes in buf
static size_t off;        // 0-based offset of the next unread character
static Token tok;         // current token (one token of lookahead)

static int has_error;     // set once the first error is found
static size_t err_pos;    // position of that first error (1-based)

// Only the FIRST error matters, and the file is read front to back, so the
// first call is always the earliest position. Later calls are ignored.
static void fail(size_t pos) {
    if (!has_error) {
        has_error = 1;
        err_pos = pos;
    }
}
