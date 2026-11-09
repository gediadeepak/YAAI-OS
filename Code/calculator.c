#include "calculator.h"
#include <limits.h>
#define SCALE INT64_C(1000000)
static unsigned size(const char *s) {
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}
static void error(Calculator *c, const char *s) {
    unsigned i = 0;
    for (; s[i] && i < 31; ++i)
        c->text[i] = s[i];
    c->text[i] = 0;
    c->error = 1;
    c->operation = 0;
    c->fresh = 1;
}
void calculator_reset(Calculator *c) { *c = (Calculator){.text = "0", .fresh = 1}; }
static int parse(const char *s, int64_t *out) {
    int negative = *s == '-';
    if (negative)
        ++s;
    int64_t whole = 0, fraction = 0;
    unsigned places = 0;
    int dot = 0, seen = 0;
    for (; *s; ++s) {
        if (*s == '.' && !dot) {
            dot = 1;
            continue;
        }
        if (*s < '0' || *s > '9')
            return 0;
        seen = 1;
        if (!dot) {
            if (__builtin_mul_overflow(whole, 10, &whole) || __builtin_add_overflow(whole, *s - '0', &whole))
                return 0;
        } else if (places < 6) {
            fraction = fraction * 10 + *s - '0';
            ++places;
        } else
            return 0;
    }
    while (places++ < 6)
        fraction *= 10;
    if (!seen || __builtin_mul_overflow(whole, SCALE, &whole) ||
        __builtin_add_overflow(whole, fraction, &whole))
        return 0;
    *out = negative ? -whole : whole;
    return 1;
}
static void format(Calculator *c, int64_t value) {
    char digits[24];
    unsigned n = 0, at = 0;
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
    if (value < 0)
        c->text[at++] = '-';
    uint64_t whole = magnitude / SCALE;
    do {
        digits[n++] = (char)('0' + whole % 10);
        whole /= 10;
    } while (whole);
    while (n)
        c->text[at++] = digits[--n];
    unsigned fraction = (unsigned)(magnitude % SCALE);
    if (fraction) {
        c->text[at++] = '.';
        unsigned divisor = 100000;
        for (unsigned i = 0; i < 6; ++i) {
            c->text[at++] = (char)('0' + fraction / divisor);
            fraction %= divisor;
            divisor /= 10;
        }
        while (c->text[at - 1] == '0')
            --at;
    }
    c->text[at] = 0;
}
static uint64_t magnitude(int64_t v) { return v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v; }
static uint64_t gcd(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}
static int apply(Calculator *c, int64_t b) {
    int64_t a = c->accumulator, result = 0;
    int bad = 0;
    if (c->operation == '+')
        bad = __builtin_add_overflow(a, b, &result);
    else if (c->operation == '-')
        bad = __builtin_sub_overflow(a, b, &result);
    else if (c->operation == '*') {
        int64_t denominator = SCALE;
        uint64_t g = gcd(magnitude(a), (uint64_t)denominator);
        a /= (int64_t)g;
        denominator /= (int64_t)g;
        g = gcd(magnitude(b), (uint64_t)denominator);
        b /= (int64_t)g;
        denominator /= (int64_t)g;
        bad = __builtin_mul_overflow(a, b, &result);
        if (!bad)
            result /= denominator;
    } else if (c->operation == '/') {
        if (!b) {
            error(c, "Cannot divide by zero");
            return 0;
        }
        uint64_t g = gcd(magnitude(a), magnitude(b));
        a /= (int64_t)g;
        b /= (int64_t)g;
        bad = __builtin_mul_overflow(a, SCALE, &result);
        if (!bad) {
            if (result == INT64_MIN && b == -1)
                bad = 1;
            else
                result /= b;
        }
    } else
        result = b;
    if (bad) {
        error(c, "Number too large");
        return 0;
    }
    c->accumulator = result;
    format(c, result);
    return 1;
}
void calculator_key(Calculator *c, char key) {
    if (key == 'c' || key == 'C') {
        calculator_reset(c);
        return;
    }
    if ((key >= '0' && key <= '9') || key == '.') {
        if (c->fresh || c->error) {
            c->text[0] = '0';
            c->text[1] = 0;
            c->fresh = c->error = 0;
        }
        unsigned n = size(c->text);
        if (key == '.') {
            for (unsigned i = 0; i < n; ++i)
                if (c->text[i] == '.')
                    return;
        }
        if (n >= 24)
            return;
        if (n == 1 && c->text[0] == '0' && key != '.')
            n = 0;
        c->text[n] = key;
        c->text[n + 1] = 0;
        return;
    }
    if (c->error)
        return;
    if (key == '\b') {
        if (c->fresh) {
            c->text[0] = '0';
            c->text[1] = 0;
            c->fresh = 0;
            return;
        }
        unsigned n = size(c->text);
        if (n > 1)
            c->text[n - 1] = 0;
        else
            c->text[0] = '0';
        if (c->text[0] == '-' && !c->text[1]) {
            c->text[0] = '0';
            c->text[1] = 0;
        }
        return;
    }
    if (key == 'n' || key == 'N') {
        unsigned n = size(c->text);
        if (c->text[0] == '-') {
            for (unsigned i = 0; i < n; ++i)
                c->text[i] = c->text[i + 1];
        } else if (!(n == 1 && c->text[0] == '0') && n < 30) {
            for (unsigned i = n + 1; i; --i)
                c->text[i] = c->text[i - 1];
            c->text[0] = '-';
        }
        c->fresh = 0;
        return;
    }
    if (key == '+' || key == '-' || key == '*' || key == '/' || key == '=' || key == '\n') {
        int64_t value;
        if (!parse(c->text, &value)) {
            error(c, "Number too large");
            return;
        }
        int equals = key == '=' || key == '\n';
        if (!c->fresh || equals) {
            if (!apply(c, value))
                return;
        }
        c->operation = equals ? 0 : key;
        c->fresh = 1;
    }
}
