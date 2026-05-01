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

Lexer lexer_new(SV src)
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

typedef enum
{
    NODE_KIND_SYMBOL,
    NODE_KIND_INT,
    NODE_KIND_FLOAT,
    NODE_KIND_STRING,
    NODE_KIND_LIST,
} Node_Kind;

typedef struct Node Node;

struct Node
{
    Node_Kind kind;
    size_t line;

    union
    {
        SV symbol;
        long long integer;
        double real;
        SV string;

        struct
        {
            Node **items;
            size_t len;
            size_t cap;
        } list;
    };
};

Node *node_alloc(Node_Kind kind, size_t line)
{
    Node *n = calloc(1, sizeof(Node));
    if (!n)
        return NULL;
    n->kind = kind;
    n->line = line;
    return n;
}

void node_free(Node *n)
{
    if (!n)
        return;

    if (n->kind == NODE_KIND_LIST)
    {
        for (size_t i = 0; i < n->list.len; i++)
            node_free(n->list.items[i]);
        free(n->list.items);
    }

    free(n);
}

void print_indent(int indent)
{
    for (int i = 0; i < indent; i++)
        printf("  ");
}

void node_print(const Node *n, int indent)
{
    if (!n)
    {
        print_indent(indent);
        printf("<null>\n");
        return;
    }

    switch (n->kind)
    {
    case NODE_KIND_SYMBOL:
        print_indent(indent);
        printf("SYMBOL  " SV_FMT "\n", SV_ARGS(n->symbol));
        break;

    case NODE_KIND_INT:
        print_indent(indent);
        printf("INT     %lld\n", n->integer);
        break;

    case NODE_KIND_FLOAT:
        print_indent(indent);
        printf("FLOAT   %g\n", n->real);
        break;

    case NODE_KIND_STRING:
        print_indent(indent);
        printf("STRING  \"" SV_FMT "\"\n", SV_ARGS(n->string));
        break;

    case NODE_KIND_LIST:
        print_indent(indent);
        printf("LIST (%zu)\n", n->list.len);
        for (size_t i = 0; i < n->list.len; i++)
            node_print(n->list.items[i], indent + 1);
        break;
    }
}

typedef struct
{
    Lexer lexer;
    Token peeked;
    bool has_peeked;
} Parser;

Token parser_advance(Parser *p)
{
    if (p->has_peeked)
    {
        p->has_peeked = false;
        return p->peeked;
    }
    return lexer_next(&p->lexer);
}

Token parser_peek(Parser *p)
{
    if (!p->has_peeked)
    {
        p->peeked = lexer_next(&p->lexer);
        p->has_peeked = true;
    }
    return p->peeked;
}

bool list_push(Node *list_node, Node *child)
{
    if (list_node->list.len >= list_node->list.cap)
    {
        size_t new_cap = list_node->list.cap == 0 ? 4 : list_node->list.cap * 2;
        Node **new_items = realloc(list_node->list.items, new_cap * sizeof(Node *));
        if (!new_items)
            return false;
        list_node->list.items = new_items;
        list_node->list.cap = new_cap;
    }
    list_node->list.items[list_node->list.len++] = child;
    return true;
}

Node *parse_expr(Parser *p);

Node *parse_list(Parser *p, size_t open_line)
{
    Node *list = node_alloc(NODE_KIND_LIST, open_line);
    if (!list)
        return NULL;

    while (true)
    {
        Token next = parser_peek(p);

        if (next.kind == TOKEN_KIND_EOF)
        {
            fprintf(stderr, "line %zu: error: unmatched '(' — reached end of file\n",
                    open_line);
            node_free(list);
            return NULL;
        }

        if (next.kind == TOKEN_KIND_RPAREN)
        {
            parser_advance(p); // consume ')'
            break;
        }

        Node *child = parse_expr(p);
        if (!child)
        {
            node_free(list);
            return NULL;
        }

        if (!list_push(list, child))
        {
            fprintf(stderr, "error: out of memory\n");
            node_free(child);
            node_free(list);
            return NULL;
        }
    }

    return list;
}

bool parse_double(SV sv, double *out)
{
    char *end;
    errno = 0;
    double result = strtod(sv.data, &end);

    if (end == sv.data)
        return false; // no digits consumed
    if (end - sv.data != (ptrdiff_t)sv.len)
        return false; // trailing garbage
    if (errno == ERANGE)
        return false; // overflow/underflow

    *out = result;
    return true;
}

Node *parse_expr(Parser *p)
{
    Token t = parser_advance(p);

    switch (t.kind)
    {
    case TOKEN_KIND_LPAREN:
        return parse_list(p, t.line);

    case TOKEN_KIND_RPAREN:
        fprintf(stderr, "line %zu: error: unexpected ')'\n", t.line);
        return NULL;

    case TOKEN_KIND_INVALID:
        fprintf(stderr, "line %zu: error: unterminated string\n", t.line);
        return NULL;

    case TOKEN_KIND_EOF:
        return NULL; // clean EOF — not an error

        // ── Atoms ─────────────────────────────────────────────────────────

    case TOKEN_KIND_SYMBOL:
    {
        Node *n = node_alloc(NODE_KIND_SYMBOL, t.line);
        if (!n)
            return NULL;
        n->symbol = t.value;
        return n;
    }

    case TOKEN_KIND_STRING:
    {
        Node *n = node_alloc(NODE_KIND_STRING, t.line);
        if (!n)
            return NULL;
        n->string = t.value;
        return n;
    }

    case TOKEN_KIND_INT:
    {
        Node *n = node_alloc(NODE_KIND_INT, t.line);
        if (!n)
            return NULL;
        if (!sv_parse_longlong(t.value, &n->integer))
        {
            fprintf(stderr, "line %zu: error: invalid integer '" SV_FMT "'\n",
                    t.line, SV_ARGS(t.value));
            node_free(n);
            return NULL;
        }
        return n;
    }

    case TOKEN_KIND_FLOAT:
    {
        Node *n = node_alloc(NODE_KIND_FLOAT, t.line);
        if (!n)
            return NULL;
        if (!parse_double(t.value, &n->real))
        {
            fprintf(stderr, "line %zu: error: invalid float '" SV_FMT "'\n",
                    t.line, SV_ARGS(t.value));
            node_free(n);
            return NULL;
        }
        return n;
    }
    }

    // unreachable
    return NULL;
}

Parser parser_new(SV src)
{
    return (Parser){
        .lexer = lexer_new(src),
        .has_peeked = false,
    };
}

Node *parser_parse_expr(Parser *p)
{
    if (parser_peek(p).kind == TOKEN_KIND_EOF)
        return NULL;
    return parse_expr(p);
}

Node **parser_parse_all(Parser *p, size_t *out_len)
{
    size_t len = 0;
    size_t cap = 8;
    Node **list = malloc(cap * sizeof(Node *));
    if (!list)
        return NULL;

    Node *expr;
    while ((expr = parser_parse_expr(p)) != NULL)
    {
        if (len >= cap)
        {
            cap *= 2;
            Node **grown = realloc(list, cap * sizeof(Node *));
            if (!grown)
            {
                for (size_t i = 0; i < len; i++)
                    node_free(list[i]);
                free(list);
                return NULL;
            }
            list = grown;
        }
        list[len++] = expr;
    }

    *out_len = len;
    return list;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        usage_error(argv[0]);

    char *program = read_file(argv[1], ".myasm");
    if (!program)
        return EXIT_FAILURE;

    Parser parser = parser_new(sv_from_cstr(program));

    size_t count;
    Node **exprs = parser_parse_all(&parser, &count);

    if (!exprs)
    {
        ERR("Out of memory");
        free(program);
        return EXIT_FAILURE;
    }

    printf("Parsed %zu top-level expression(s):\n\n", count);

    for (size_t i = 0; i < count; i++)
    {
        node_print(exprs[i], 0);
        printf("\n");
    }

    for (size_t i = 0; i < count; i++)
        node_free(exprs[i]);
    free(exprs);

    free(program);

    return EXIT_SUCCESS;
}