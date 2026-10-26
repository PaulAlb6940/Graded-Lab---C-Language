// PAUL ALBERTI 260ADB178
// (optional) https://github.com/<your-username>/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: Claude.ai (Anthropic AI assistant), conversation link: <PASTE SHARED CONVERSATION LINK>; details in comments where used.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

// Identity used to build output names: <base>_<NAME>_<LASTNAME>_<ID>.txt
// and the default output folder <base>_<NAME>_<ID>.
#define STUDENT_NAME "paul"
#define STUDENT_LAST "alberti"
#define STUDENT_ID   "260ADB178"
#define FILE_SUFFIX  "_" STUDENT_NAME "_" STUDENT_LAST "_" STUDENT_ID ".txt"
#define DIR_SUFFIX   "_" STUDENT_NAME "_" STUDENT_ID

// ---------------------------------------------------------------------------
// Types and shared parser state
// ---------------------------------------------------------------------------

typedef enum {
    T_INT,      // integer literal
    T_FLOAT,    // float literal (digits '.' digits)
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW,
    T_LPAREN, T_RPAREN,
    T_EOL,      // '\n' OR end of file: both end an expression, and both are
                // where "missing operand" / "missing )" errors are reported
    T_BAD       // any character that is not part of the language; the tokenizer
                // never reports errors itself, the parser decides (one place)
} TokType;

// A number is an int or a double; the flag decides the arithmetic (see apply()).
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
// Not const: the tokenizer briefly writes a '\0' after a float so strtod()
// can read it without copying the digits anywhere (see next_token()).
static char *buf;         // whole file content, read once (buf[len] == '\0')
static size_t len;        // number of bytes in buf
static size_t off;        // 0-based offset of the next unread character
static Token tok;         // current token (one token of lookahead)

static int has_error;     // set once the first error is found
static size_t err_pos;    // position of that first error (1-based)

// Results are collected here and written only at the end: if any error
// occurs the file must contain ONLY the error, never partial results.
static char *out;
static size_t out_len, out_cap;

// Only the FIRST error matters, and the file is read front to back, so the
// first call is always the earliest position. Later calls are ignored.
static void fail(size_t pos) {
    if (!has_error) {
        has_error = 1;
        err_pos = pos;
    }
}

// Append to the output buffer. Capacity doubles, so appending N results costs
// O(N) overall instead of copying the whole buffer on every line.
// AI (Claude): prompt "<PASTE YOUR REAL PROMPT HERE>"
// MY NOTES: <explain in your own words why doubling is amortized O(1)>
static void out_append(const char *s, size_t n) {
    if (out_len + n > out_cap) {
        size_t cap = out_cap ? out_cap : 4096;
        while (cap < out_len + n) cap *= 2;
        char *p = realloc(out, cap);
        if (!p) {
            fprintf(stderr, "calc: out of memory\n");
            exit(1);
        }
        out = p;
        out_cap = cap;
    }
    memcpy(out + out_len, s, n);
    out_len += n;
}

static void append_value(Value v) {
    char tmp[64];   // "%.15g" and "%lld" never need more than ~25 chars
    int n = v.is_float ? snprintf(tmp, sizeof tmp, "%.15g\n", v.d)
                       : snprintf(tmp, sizeof tmp, "%lld\n", v.i);
    out_append(tmp, (size_t)n);
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

// Position tracking: `off` only ever moves forward over the whole file and is
// never reset at line starts, so off + 1 is already the file-wide 1-based
// position (newlines and comment lines included). No separate counter needed.
// MY NOTES: <describe in your own words how positions are tracked>
// AI (Claude): prompt "donne moi une piste pour la structure de next_token(),
// j'essaie de la compléter moi-même". Squelette repris 

static void next_token(void) {
    // 1. Sauter les espaces et tabulations (mais PAS '\n' : c'est un token).
    while (off < len && (buf[off] == ' ' || buf[off] == '\t')) {
        off++;
    }

    // 2. Position fixée APRÈS les blancs et AVANT de consommer le token :
    //    off est 0-based, les positions de l'énoncé sont 1-based.
    tok.pos = off + 1;

    // 3. Fin du buffer : T_EOL. Quand off == len, off + 1 == len + 1,
    //    exactement la position demandée pour un fichier sans '\n' final.
    if (off >= len) {
        tok.type = T_EOL;
        return;
    }

    char c = buf[off];

    // 4. Chiffres.
    if (isdigit((unsigned char)c)) {
        size_t start = off;
        long long n = 0;
        while (isdigit((unsigned char)buf[off])) {  // buf[len] == '\0' arrête la boucle
            n = n * 10 + (buf[off] - '0');
            off++;
        }
        // Flottant seulement si '.' est suivi d'un chiffre. Sinon le '.' n'est
        // pas consommé : il devient le token suivant (T_BAD, à sa position).
        // Pareil pour le 'e' de 1.5e3 : il reste non lu.
        if (buf[off] == '.' && isdigit((unsigned char)buf[off + 1])) {
            off++;
            while (isdigit((unsigned char)buf[off])) off++;
            // strtod veut une chaîne terminée par '\0' : on en pose un
            // temporairement au lieu de copier les chiffres, puis on restaure.
            char saved = buf[off];
            buf[off] = '\0';
            tok.val.d = strtod(buf + start, NULL);
            buf[off] = saved;
            tok.val.is_float = 1;
            tok.type = T_FLOAT;
        } else {
            tok.val.is_float = 0;
            tok.val.i = n;
            tok.type = T_INT;
        }
        return;
    }

    // 5. Opérateurs et parenthèses.
    off++;               // on consomme le caractère
    switch (c) {
        case '+': tok.type = T_PLUS;   break;
        case '-': tok.type = T_MINUS;  break;
        case '/': tok.type = T_SLASH;  break;
        case '(': tok.type = T_LPAREN; break;
        case ')': tok.type = T_RPAREN; break;
        case '\n': tok.type = T_EOL;   break;
        case '*':
            // "**" doit être UN token, sinon 2 ** 3 serait lu comme deux '*'.
            // buf[off] est sûr même en fin de buffer grâce au '\0' final.
            if (buf[off] == '*') {
                off++;
                tok.type = T_POW;
            } else {
                tok.type = T_STAR;
            }
            break;
        default:  tok.type = T_BAD;  break;   // y compris '#' ici
    }
}

// Algorythme écris par mes soins puis vérifier par claude ensuite.

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

static Value zero_value(void) {
    Value v;
    v.is_float = 0;
    v.i = 0;
    v.d = 0.0;
    return v;
}

static double as_double(Value v) {
    return v.is_float ? v.d : (double)v.i;
}

// Integer power by repeated squaring: O(log e) even for huge exponents such as
// 1 ** 1000000000000. The base is not squared after the last bit is used,
// so we never overflow just to compute a value we would throw away.
static long long int_pow(long long base, long long e) {
    long long r = 1;
    while (e > 0) {
        if (e & 1) r *= base;
        e >>= 1;
        if (e > 0) base *= base;
    }
    return r;   // e == 0 gives 1, including 0 ** 0, as the rules say
}

// Apply a binary operator. Integer vs float: the result is a float as soon as
// ONE operand is a float (then everything is computed in double); two ints
// stay integer. This is decided here, per operation, so 7 / 2 is 3 but
// 7 / 2.0 is 3.5.
//   oppos : position of the operator (reported for non-finite float results)
//   rpos  : start of the right operand (reported for division by zero and
//           negative integer exponent, which are faults of that operand)
// AI (Claude): prompt "<PASTE YOUR REAL PROMPT HERE>"
// MY NOTES: <explain why each error uses oppos or rpos>
static Value apply(TokType op, size_t oppos, size_t rpos, Value a, Value b) {
    Value r = zero_value();

    if (a.is_float || b.is_float) {
        double x = as_double(a), y = as_double(b), z = 0.0;
        switch (op) {
        case T_PLUS:  z = x + y; break;
        case T_MINUS: z = x - y; break;
        case T_STAR:  z = x * y; break;
        case T_SLASH:
            if (y == 0.0) { fail(rpos); return r; }
            z = x / y;
            break;
        case T_POW:   z = pow(x, y); break;
        default: break;
        }
        // (0-8) ** 0.5 gives NaN and 10.0 ** 400 gives inf: neither can be
        // printed as a result, and the operator is what produced them.
        if (!isfinite(z)) { fail(oppos); return r; }
        r.is_float = 1;
        r.d = z;
        return r;
    }

    switch (op) {
    case T_PLUS:  r.i = a.i + b.i; break;
    case T_MINUS: r.i = a.i - b.i; break;
    case T_STAR:  r.i = a.i * b.i; break;
    case T_SLASH:
        if (b.i == 0) { fail(rpos); return r; }
        r.i = a.i / b.i;    // C truncates toward zero, exactly what the rules ask
        break;
    case T_POW:
        if (b.i < 0) { fail(rpos); return r; }   // an int result cannot be a fraction
        r.i = int_pow(a.i, b.i);
        break;
    default: break;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Recursive descent parser: one function per precedence level.
//   expr    := term  { ('+' | '-') term }
//   term    := power { ('*' | '/') power }
//   power   := primary [ '**' power ]
//   primary := NUMBER | '(' expr ')'
// Lower precedence = higher in the call chain: expr calls term, which calls
// power. So in 3 + 2 * 5 the product is finished inside term() before expr()
// sees the '+', which gives * and / priority over + and -.
// Every function stops quickly once has_error is set.
// AI (Claude): prompt "<PASTE YOUR REAL PROMPT HERE>"
// MY NOTES: <explain the grammar in your own words>
// ---------------------------------------------------------------------------

static Value expr(void);

static Value primary(void) {
    Value v;
    switch (tok.type) {
    case T_INT:
    case T_FLOAT:
        v = tok.val;
        next_token();
        return v;
    case T_LPAREN:
        next_token();
        v = expr();
        if (has_error) return v;
        // Missing ')' : the token we are looking at (usually the line end)
        // is the place where the ')' should have been.
        if (tok.type != T_RPAREN) {
            fail(tok.pos);
            return zero_value();
        }
        next_token();
        return v;
    default:
        // Anything else where an operand is required: a second operator
        // ("7 + * 3"), a unary minus ("-5"), "()", a missing operand at
        // the line end, or an invalid character. The offending token is
        // the current one, so its position is the answer in all cases.
        fail(tok.pos);
        return zero_value();
    }
}

// power calls itself for the exponent, so 2 ** 3 ** 2 groups as 2 ** (3 ** 2).
// A loop (like in expr/term) would group to the left; the recursion on the
// right-hand side is what makes ** right-associative.
static Value power(void) {
    Value base = primary();
    if (!has_error && tok.type == T_POW) {
        size_t oppos = tok.pos;
        next_token();
        size_t rpos = tok.pos;      // start of the exponent, noted before parsing it
        Value e = power();
        if (has_error) return base;
        return apply(T_POW, oppos, rpos, base, e);
    }
    return base;
}

// Loops give left associativity: 8 - 3 - 2 is (8 - 3) - 2.
static Value term(void) {
    Value l = power();
    while (!has_error && (tok.type == T_STAR || tok.type == T_SLASH)) {
        TokType op = tok.type;
        size_t oppos = tok.pos;
        next_token();
        size_t rpos = tok.pos;
        Value r = power();
        if (has_error) return l;
        l = apply(op, oppos, rpos, l, r);
    }
    return l;
}

static Value expr(void) {
    Value l = term();
    while (!has_error && (tok.type == T_PLUS || tok.type == T_MINUS)) {
        TokType op = tok.type;
        size_t oppos = tok.pos;
        next_token();
        size_t rpos = tok.pos;
        Value r = term();
        if (has_error) return l;
        l = apply(op, oppos, rpos, l, r);
    }
    return l;
}

// Evaluate the whole buffer line by line. Stops at the first error.
static void evaluate_all(void) {
    has_error = 0;
    err_pos = 0;
    out_len = 0;
    off = 0;

    while (!has_error) {
        // Look at the first non-blank character to classify the line.
        while (off < len && (buf[off] == ' ' || buf[off] == '\t')) off++;
        if (off >= len) break;
        if (buf[off] == '\n') {         // blank line: no output
            off++;
            continue;
        }
        if (buf[off] == '#') {          // comment: only valid as first non-blank char
            while (off < len && buf[off] != '\n') off++;
            if (off < len) off++;
            continue;
        }

        next_token();
        Value v = expr();
        // After a full expression only the line end is allowed. A leftover
        // ')' ("(1 + 2))") or any other token is reported where it starts.
        if (!has_error && tok.type != T_EOL) fail(tok.pos);
        if (!has_error) append_value(v);
    }
}

// ---------------------------------------------------------------------------
// Files, folders and command line
// ---------------------------------------------------------------------------

// Read a whole file into one malloc'd buffer, '\0'-terminated.
// The buffer doubles when full (no repeated copying of the whole content for
// each chunk), so multi-megabyte files are read in a handful of reallocs.
// AI (Claude): prompt "<PASTE YOUR REAL PROMPT HERE>"
// MY NOTES: <say which part of the loop is yours>
static char *read_file(const char *path, size_t *n) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "calc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (S_ISDIR(st.st_mode)) {
        fprintf(stderr, "calc: '%s' is a directory, not a file\n", path);
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "calc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = 1 << 16, used = 0;
    char *b = malloc(cap);
    if (!b) {
        fclose(f);
        fprintf(stderr, "calc: out of memory\n");
        return NULL;
    }
    for (;;) {
        if (used + 1 >= cap) {          // keep one byte free for the final '\0'
            cap *= 2;
            char *p = realloc(b, cap);
            if (!p) {
                free(b);
                fclose(f);
                fprintf(stderr, "calc: out of memory\n");
                return NULL;
            }
            b = p;
        }
        size_t r = fread(b + used, 1, cap - used - 1, f);
        if (r == 0) break;
        used += r;
    }
    if (ferror(f)) {
        fprintf(stderr, "calc: error reading '%s'\n", path);
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    b[used] = '\0';
    *n = used;
    return b;
}

// "dir/task1.txt" -> "task1", "labs/" -> "labs". Malloc'd.
static char *base_name(const char *path) {
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    size_t n = end - start;
    if (n > 4 && strcmp(path + end - 4, ".txt") == 0) n -= 4;
    char *s = malloc(n + 1);
    if (!s) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    memcpy(s, path + start, n);
    s[n] = '\0';
    return s;
}

// base + suffix, malloc'd.
static char *concat(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *s = malloc(la + lb + 1);
    if (!s) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    memcpy(s, a, la);
    memcpy(s + la, b, lb + 1);
    return s;
}

// dir + "/" + name, malloc'd.
static char *join_path(const char *dir, const char *name) {
    size_t la = strlen(dir), lb = strlen(name);
    char *s = malloc(la + lb + 2);
    if (!s) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    memcpy(s, dir, la);
    s[la] = '/';
    memcpy(s + la + 1, name, lb + 1);
    return s;
}

// Create the output folder if needed (its parent exists); reuse it otherwise.
static int make_outdir(const char *d) {
    struct stat st;
    if (stat(d, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "calc: '%s' exists and is not a folder\n", d);
        return 1;
    }
    if (mkdir(d, 0777) != 0) {
        fprintf(stderr, "calc: cannot create folder '%s': %s\n", d, strerror(errno));
        return 1;
    }
    return 0;
}

// Evaluate one input file and write its output file. Returns 0 on success.
// A result of ERROR:<pos> is NOT a failure here (exit code stays 0).
static int process_file(const char *in_path, const char *base, const char *outdir) {
    size_t n = 0;
    char *data = read_file(in_path, &n);
    if (!data) return 1;

    buf = data;
    len = n;
    evaluate_all();

    // Created only after the input was read, so a missing input leaves no
    // empty folder behind.
    if (make_outdir(outdir) != 0) {
        free(data);
        return 1;
    }

    char *name = concat(base, FILE_SUFFIX);
    char *path = join_path(outdir, name);
    FILE *f = fopen(path, "wb");
    int rc = 0;
    if (!f) {
        fprintf(stderr, "calc: cannot write '%s': %s\n", path, strerror(errno));
        rc = 1;
    } else {
        if (has_error) {
            char msg[64];
            int m = snprintf(msg, sizeof msg, "ERROR:%zu\n", err_pos);
            if (fwrite(msg, 1, (size_t)m, f) != (size_t)m) rc = 1;
        } else if (out_len > 0 && fwrite(out, 1, out_len, f) != out_len) {
            rc = 1;
        }
        if (fclose(f) != 0) rc = 1;
        if (rc) fprintf(stderr, "calc: error writing '%s'\n", path);
    }
    free(path);
    free(name);
    free(data);
    buf = NULL;
    return rc;
}

static int usage(void) {
    fprintf(stderr,
            "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *dir = NULL, *outdir = NULL, *input = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-d") == 0 || strcmp(a, "--dir") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "calc: option '%s' needs a value\n", a);
                return usage();
            }
            dir = argv[i];
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "--output-dir") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "calc: option '%s' needs a value\n", a);
                return usage();
            }
            outdir = argv[i];
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "calc: unknown option '%s'\n", a);
            return usage();
        } else {
            if (input) {
                fprintf(stderr, "calc: more than one input file\n");
                return usage();
            }
            input = a;
        }
    }

    // Exactly one of: an input file, or -d DIR.
    if ((dir == NULL) == (input == NULL)) {
        fprintf(stderr, "calc: give either one input file or -d DIR\n");
        return usage();
    }

    int rc = 0;

    if (input) {
        char *base = base_name(input);
        char *default_out = NULL;
        if (!outdir) {
            default_out = concat(base, DIR_SUFFIX);
            outdir = default_out;
        }
        rc = process_file(input, base, outdir);
        free(default_out);
        free(base);
    } else {
        DIR *d = opendir(dir);
        if (!d) {
            fprintf(stderr, "calc: cannot open folder '%s': %s\n", dir, strerror(errno));
            free(out);
            return 1;
        }
        char *dbase = base_name(dir);
        char *default_out = NULL;
        if (!outdir) {
            default_out = concat(dbase, DIR_SUFFIX);
            outdir = default_out;
        }
        rc = make_outdir(outdir);
        struct dirent *e;
        while (rc == 0 && (e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            // Only "*.txt" files directly inside DIR; subfolders (even named
            // x.txt) and other files are ignored.
            if (n <= 4 || strcmp(e->d_name + n - 4, ".txt") != 0) continue;
            char *p = join_path(dir, e->d_name);
            struct stat st;
            if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
                char *b = base_name(e->d_name);
                if (process_file(p, b, outdir) != 0) rc = 1;
                free(b);
            }
            free(p);
        }
        closedir(d);
        free(default_out);
        free(dbase);
    }

    free(out);
    return rc;
}
