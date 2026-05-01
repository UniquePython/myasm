#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include <strlib-0.2.0/strlib.h>

// Write formatted output to stdout with a newline appended.
#define PRINTLN(fmt, ...)                \
    do                                   \
    {                                    \
        printf(fmt "\n", ##__VA_ARGS__); \
    } while (0)

// Write formatted error to stderr with a newline appended.
#define ERR(fmt, ...)                                                 \
    do                                                                \
    {                                                                 \
        fprintf(stderr, "\033[1;31m" fmt "\n\033[0m", ##__VA_ARGS__); \
    } while (0)

// Write formatted warning to stderr with a newline appended.
#define WARN(fmt, ...)                                                \
    do                                                                \
    {                                                                 \
        fprintf(stderr, "\033[1;33m" fmt "\n\033[0m", ##__VA_ARGS__); \
    } while (0)

// Abort execution and generate a core-dump on usage error.
void usage_error(const char *prog)
{
    ERR("Expected exactly one input file");
    WARN("Usage: %s <input-file.myasm>", prog);
    abort();
}

// Check if a string ends with a given suffix.
bool ends_with(const char *str, const char *suffix)
{
    const size_t str_len = strlen(str);
    const size_t suffix_len = strlen(suffix);

    if (suffix_len >= str_len)
        return false;

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Get the file size (POSIX only). Returns -1 on error.
off_t get_file_size(FILE *fp)
{
    struct stat st;
    if (fstat(fileno(fp), &st) == -1)
        return -1;

    return st.st_size;
}

// Read the contents of a file. Returns NULL on error.
char *read_file(const char *file_name, const char *file_extension)
{
    if (!ends_with(file_name, file_extension))
    {
        ERR("Invalid file extension. Expected \"%s\"", file_extension);
        return NULL;
    }

    FILE *file = fopen(file_name, "rb");
    if (!file)
    {
        ERR("Unable to read file %s", file_name);
        return NULL;
    }

    off_t file_size;
    if ((file_size = get_file_size(file)) == -1)
    {
        ERR("%s", strerror(errno));
        fclose(file);
        return NULL;
    }

    size_t buffer_size = (size_t)(file_size + 1);

    char *buffer = malloc(buffer_size);
    if (!buffer)
    {
        ERR("Not enough memory to read file");
        fclose(file);
        return NULL;
    }

    size_t read = fread(buffer, sizeof(char), (size_t)file_size, file);
    if (read != (size_t)file_size)
    {
        if (feof(file))
            ERR("Unexpected EOF encountered");
        else if (ferror(file))
            ERR("%s", strerror(errno));
        else
            ERR("Unknown error occurred while reading file");
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);

    buffer[buffer_size - 1] = '\0';

    return buffer;
}

typedef enum
{
    TOKEN_KIND_LPAREN,
    TOKEN_KIND_RPAREN,
    TOKEN_KIND_SYMBOL,
    TOKEN_KIND_INT,
    TOKEN_KIND_FLOAT,
    TOKEN_KIND_STRING,
    TOKEN_KIND_EOF,
    TOKEN_KIND_INVALID,
} Token_Kind;

typedef struct
{
    Token_Kind kind;
    SV value;
    size_t line;
} Token;

typedef struct
{
    SV src;
    size_t line;
} Lexer;

Token make_token(Token_Kind kind, SV value, size_t line)
{
    return (Token){.kind = kind, .value = value, .line = line};
}

void advance(Lexer *l, size_t n)
{
    l->src = sv_slice(l->src, n, l->src.len);
}

char peek(const Lexer *l, size_t i)
{
    return l->src.data[i];
}

void skip_noise(Lexer *l)
{
    while (!sv_is_empty(l->src))
    {
        char c = peek(l, 0);

        if (c == '\n')
        {
            l->line++;
            advance(l, 1);
        }
        else if (isspace((unsigned char)c))
        {
            advance(l, 1);
        }
        else if (c == ';') // line comment
        {
            while (!sv_is_empty(l->src) && peek(l, 0) != '\n')
                advance(l, 1);
        }
        else
        {
            break;
        }
    }
}

const char *token_kind_name(Token_Kind kind)
{
    switch (kind)
    {
    case TOKEN_KIND_LPAREN:
        return "LPAREN";
    case TOKEN_KIND_RPAREN:
        return "RPAREN";
    case TOKEN_KIND_SYMBOL:
        return "SYMBOL";
    case TOKEN_KIND_INT:
        return "INT";
    case TOKEN_KIND_FLOAT:
        return "FLOAT";
    case TOKEN_KIND_STRING:
        return "STRING";
    case TOKEN_KIND_EOF:
        return "EOF";
    case TOKEN_KIND_INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
}

Lexer new_lexer(SV src)
{
    return (Lexer){.src = src, .line = 1};
}

Token lexer_next(Lexer *l)
{
    skip_noise(l);

    if (sv_is_empty(l->src))
        return make_token(TOKEN_KIND_EOF, sv_from_parts("", 0), l->line);

    const size_t line = l->line;
    const char c = peek(l, 0);

    // ── Single-character tokens ──────────────────────────────────────────────

    if (c == '(')
    {
        SV val = sv_slice(l->src, 0, 1);
        advance(l, 1);
        return make_token(TOKEN_KIND_LPAREN, val, line);
    }

    if (c == ')')
    {
        SV val = sv_slice(l->src, 0, 1);
        advance(l, 1);
        return make_token(TOKEN_KIND_RPAREN, val, line);
    }

    // ── String literal ───────────────────────────────────────────────────────
    // Opening " was consumed; value is the content without the quotes.

    if (c == '"')
    {
        advance(l, 1); // skip opening "
        size_t i = 0;
        while (i < l->src.len && peek(l, i) != '"')
        {
            if (peek(l, i) == '\n')
                l->line++;
            i++;
        }

        if (i >= l->src.len) // reached EOF without closing "
        {
            SV bad = l->src;
            advance(l, l->src.len);
            return make_token(TOKEN_KIND_INVALID, bad, line);
        }

        SV val = sv_slice(l->src, 0, i);
        advance(l, i + 1); // skip content + closing "
        return make_token(TOKEN_KIND_STRING, val, line);
    }

    // ── Numeric literal ──────────────────────────────────────────────────────
    // Matches: digits, or '-' immediately followed by a digit.

    bool is_negative = (c == '-' && l->src.len > 1 && isdigit((unsigned char)peek(l, 1)));
    if (isdigit((unsigned char)c) || is_negative)
    {
        size_t i = is_negative ? 1 : 0;
        bool is_float = false;

        while (i < l->src.len)
        {
            char ch = peek(l, i);
            if (ch == '.' && !is_float)
            {
                is_float = true;
                i++;
            }
            else if (isdigit((unsigned char)ch))
            {
                i++;
            }
            else
            {
                break;
            }
        }

        SV val = sv_slice(l->src, 0, i);
        advance(l, i);
        return make_token(is_float ? TOKEN_KIND_FLOAT : TOKEN_KIND_INT, val, line);
    }

    // ── Symbol ───────────────────────────────────────────────────────────────
    // Anything that isn't whitespace, parens, or a quote.

    {
        size_t i = 0;
        while (i < l->src.len)
        {
            char ch = peek(l, i);
            if (isspace((unsigned char)ch) || ch == '(' || ch == ')' || ch == '"')
                break;
            i++;
        }

        SV val = sv_slice(l->src, 0, i);
        advance(l, i);
        return make_token(TOKEN_KIND_SYMBOL, val, line);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        usage_error(argv[0]);

    char *program = read_file(argv[1], ".myasm");
    if (!program)
        return EXIT_FAILURE;

    Lexer l = new_lexer(sv_from_cstr(program));
    Token t;

    while ((t = lexer_next(&l)).kind != TOKEN_KIND_EOF)
    {
        if (t.kind == TOKEN_KIND_INVALID)
        {
            ERR("line %zu: unterminated string", t.line);
            free(program);
            return EXIT_FAILURE;
        }

        printf("line %2zu  %-8s  \"" SV_FMT "\"\n", t.line, token_kind_name(t.kind), SV_ARGS(t.value));
    }

    free(program);

    return EXIT_SUCCESS;
}