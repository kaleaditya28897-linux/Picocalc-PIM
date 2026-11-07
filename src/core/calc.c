#include "pim/tools.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALC_DEPTH 24

typedef struct {
    const char *cursor;
    double previous;
    bool degrees;
    int depth;
    char error[40];
} calc_t;

static double expression(calc_t *calc);

static void fail(calc_t *calc, const char *message) {
    if (calc->error[0] == '\0') snprintf(calc->error, sizeof(calc->error), "%s", message);
}
static void skip(calc_t *calc) { while (*calc->cursor == ' ') calc->cursor++; }
static bool accept(calc_t *calc, char c) {
    skip(calc);
    if (*calc->cursor != c) return false;
    calc->cursor++;
    return true;
}

static double function_call(calc_t *calc, const char *name, double x) {
    double to_radians = calc->degrees ? M_PI / 180.0 : 1.0;
    if (strcmp(name, "sin") == 0) return sin(x * to_radians);
    if (strcmp(name, "cos") == 0) return cos(x * to_radians);
    if (strcmp(name, "tan") == 0) return tan(x * to_radians);
    if (strcmp(name, "asin") == 0) { if (x < -1 || x > 1) fail(calc, "Domain error"); return asin(x) / to_radians; }
    if (strcmp(name, "acos") == 0) { if (x < -1 || x > 1) fail(calc, "Domain error"); return acos(x) / to_radians; }
    if (strcmp(name, "atan") == 0) return atan(x) / to_radians;
    if (strcmp(name, "sqrt") == 0) { if (x < 0) fail(calc, "Domain error"); return sqrt(x); }
    if (strcmp(name, "cbrt") == 0) return cbrt(x);
    if (strcmp(name, "ln") == 0) { if (x <= 0) fail(calc, "Domain error"); return log(x); }
    if (strcmp(name, "log") == 0) { if (x <= 0) fail(calc, "Domain error"); return log10(x); }
    if (strcmp(name, "exp") == 0) return exp(x);
    if (strcmp(name, "abs") == 0) return fabs(x);
    if (strcmp(name, "floor") == 0) return floor(x);
    if (strcmp(name, "ceil") == 0) return ceil(x);
    if (strcmp(name, "round") == 0) return round(x);
    fail(calc, "Unknown function");
    return 0;
}

static double primary(calc_t *calc) {
    double value = 0;
    skip(calc);
    if (calc->depth >= CALC_DEPTH) { fail(calc, "Too deeply nested"); return 0; }
    calc->depth++;
    if (*calc->cursor == '(') {
        calc->cursor++;
        value = expression(calc);
        if (!accept(calc, ')')) fail(calc, "Missing )");
    } else if (isdigit((unsigned char)*calc->cursor) || *calc->cursor == '.') {
        char *end;
        value = strtod(calc->cursor, &end);
        if (end == calc->cursor) fail(calc, "Bad number");
        calc->cursor = end;
    } else if (isalpha((unsigned char)*calc->cursor)) {
        char name[10];
        size_t length = 0;
        while (isalpha((unsigned char)*calc->cursor)) {
            if (length < sizeof(name) - 1) name[length++] = (char)tolower((unsigned char)*calc->cursor);
            calc->cursor++;
        }
        name[length] = '\0';
        if (strcmp(name, "pi") == 0) value = M_PI;
        else if (strcmp(name, "e") == 0) value = M_E;
        else if (strcmp(name, "ans") == 0) value = calc->previous;
        else if (accept(calc, '(')) {
            double argument = expression(calc);
            if (!accept(calc, ')')) fail(calc, "Missing )");
            value = function_call(calc, name, argument);
        } else fail(calc, "Unknown name");
    } else fail(calc, *calc->cursor == '\0' ? "Unexpected end" : "Unexpected character");
    calc->depth--;
    return value;
}

static double unary(calc_t *calc);
static double power(calc_t *calc) {
    double base = primary(calc);
    if (accept(calc, '^')) {
        double exponent = unary(calc);
        double result = pow(base, exponent);
        if (isnan(result)) fail(calc, "Domain error");
        return result;
    }
    return base;
}
static double unary(calc_t *calc) {
    if (accept(calc, '-')) return -unary(calc);
    if (accept(calc, '+')) return unary(calc);
    return power(calc);
}
static double term(calc_t *calc) {
    double value = unary(calc);
    for (;;) {
        if (accept(calc, '*')) value *= unary(calc);
        else if (accept(calc, '/')) {
            double divisor = unary(calc);
            if (divisor == 0) { fail(calc, "Divide by zero"); return 0; }
            value /= divisor;
        } else if (accept(calc, '%')) {
            double divisor = unary(calc);
            if (divisor == 0) { fail(calc, "Divide by zero"); return 0; }
            value = fmod(value, divisor);
        } else return value;
    }
}
static double expression(calc_t *calc) {
    double value = term(calc);
    for (;;) {
        if (accept(calc, '+')) value += term(calc);
        else if (accept(calc, '-')) value -= term(calc);
        else return value;
    }
}

bool pim_calc_eval(const char *text, double previous, bool degrees, double *result, char *error, size_t error_size) {
    calc_t calc = {text, previous, degrees, 0, ""};
    double value = expression(&calc);
    skip(&calc);
    if (calc.error[0] == '\0' && *calc.cursor != '\0') fail(&calc, "Unexpected character");
    if (calc.error[0] == '\0' && (isnan(value) || isinf(value))) fail(&calc, "Out of range");
    if (calc.error[0] != '\0') {
        if (error != NULL) snprintf(error, error_size, "%s", calc.error);
        return false;
    }
    *result = value;
    return true;
}

void pim_calc_format(double value, char *out, size_t size) {
    if (fabs(value) < 1e-12) value = 0;
    snprintf(out, size, "%.10g", value);
}
