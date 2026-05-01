#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

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

int main(int argc, char *argv[])
{
    if (argc != 2)
        usage_error(argv[0]);

    char *program = read_file(argv[1], ".myasm");
    if (!program)
        return EXIT_FAILURE;

    // TODO: Actual logic

    free(program);

    return EXIT_SUCCESS;
}