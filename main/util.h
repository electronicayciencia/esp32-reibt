#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>
#include <stddef.h>

// Convert Bluetooth Device Address (BDA) to string.
char *bda2str(uint8_t *bda, char *str, size_t size);

#endif // UTIL_H_