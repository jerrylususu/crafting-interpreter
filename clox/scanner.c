#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;   // beginning of the current lexeme
    const char* current; // point to the current char being looked at (1 past consumed)
    int line;            // line number current lexeme is on
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}