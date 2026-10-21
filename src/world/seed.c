#include "maage/seed.h"

/* Base-65536 arithmetic avoids assuming a native 64-bit integer or the width
 * of unsigned long. Intermediate products are at most 655359. */
maage_status maage_world_seed_parse(const char *decimal, maage_world_seed *seed)
{
    unsigned long limbs[4];
    unsigned long carry, value;
    unsigned int digit;
    int negative, index;
    if (seed) seed->high = seed->low = 0;
    if (decimal == NULL || seed == NULL) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PARSE, decimal ? 1 : 0);
    negative = *decimal == '-';
    if (*decimal == '-' || *decimal == '+') ++decimal;
    if (*decimal == '\0') return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PARSE, 0);
    for (index = 0; index < 4; ++index) limbs[index] = 0;
    while (*decimal != '\0') {
        if (*decimal < '0' || *decimal > '9') return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PARSE, 0);
        digit = (unsigned int)(*decimal++ - '0');
        carry = digit;
        for (index = 3; index >= 0; --index) {
            value = limbs[index] * 10UL + carry;
            limbs[index] = value & 65535UL;
            carry = value >> 16;
        }
        if (carry != 0) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PARSE, 0);
    }
    if (limbs[0] > (negative ? 32768UL : 32767UL) ||
        (negative && limbs[0] == 32768UL &&
        (limbs[1] != 0 || limbs[2] != 0 || limbs[3] != 0))) return maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_PARSE, 0);
    if (negative) {
        carry = 1;
        for (index = 3; index >= 0; --index) {
            value = (65535UL ^ limbs[index]) + carry;
            limbs[index] = value & 65535UL;
            carry = value >> 16;
        }
    }
    seed->high = (limbs[0] << 16) | limbs[1];
    seed->low = (limbs[2] << 16) | limbs[3];
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_PARSE, 0);
}

maage_status maage_world_seed_format(maage_world_seed seed, char *text, size_t capacity)
{
    unsigned long limbs[4];
    unsigned long carry, value;
    char reversed[20];
    size_t length, output;
    int negative, index;
    limbs[0] = (seed.high >> 16) & 65535UL;
    limbs[1] = seed.high & 65535UL;
    limbs[2] = (seed.low >> 16) & 65535UL;
    limbs[3] = seed.low & 65535UL;
    negative = (limbs[0] & 32768UL) != 0;
    if (negative) {
        carry = 1;
        for (index = 3; index >= 0; --index) {
            value = (65535UL ^ limbs[index]) + carry;
            limbs[index] = value & 65535UL;
            carry = value >> 16;
        }
    }
    length = 0;
    do {
        carry = 0;
        for (index = 0; index < 4; ++index) {
            value = (carry << 16) | limbs[index];
            limbs[index] = value / 10UL;
            carry = value % 10UL;
        }
        reversed[length++] = (char)('0' + carry);
    } while (limbs[0] || limbs[1] || limbs[2] || limbs[3]);
    if (!text) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_FORMAT, 1);
    if (capacity) text[0] = 0;
    if (capacity < length + (size_t)negative + 1) return maage_status_make(MAAGE_STATUS_BUFFER_OVERFLOW, MAAGE_CONTEXT_WORLD, MAAGE_OPERATION_FORMAT, 0);
    output = 0;
    if (negative) text[output++] = '-';
    while (length > 0) text[output++] = reversed[--length];
    text[output] = '\0';
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_FORMAT, 0);
}
