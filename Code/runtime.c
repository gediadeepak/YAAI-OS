#include <stddef.h>

// Freestanding C support routines that the compiler may emit for structure copies.
void *memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *target = destination;
    const unsigned char *bytes = source;
    for (size_t index = 0; index < size; ++index) target[index] = bytes[index];
    return destination;
}

void *memset(void *destination, int value, size_t size)
{
    unsigned char *target = destination;
    for (size_t index = 0; index < size; ++index) target[index] = (unsigned char)value;
    return destination;
}

void *memmove(void *destination, const void *source, size_t size)
{
    unsigned char *target = destination;
    const unsigned char *bytes = source;
    if ((size_t)target <= (size_t)bytes) return memcpy(destination, source, size);
    for (size_t index = size; index > 0; --index) target[index - 1] = bytes[index - 1];
    return destination;
}
