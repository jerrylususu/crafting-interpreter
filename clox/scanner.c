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

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

static char peek() {
    return *scanner.current;
}

static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

// conditionally advance
static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char* message) {
    // only called with C string literal, no need to worry ownership of "message"
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhiteSpace() {
    // advance the scanner past any leading whitespace (and also comments)
    // when this returns, next char is meaningful (or we're at the end)
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    // comment goes until the end of the line
                    // leave the newline unconsumed so the line number can increment
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token number() {
    while (isDigit(peek())) advance();

    // look for a fractional part
    if (peek() == '.' && isDigit(peekNext())) {
        // consume '.'
        advance();

        while (isDigit(peek())) advance();
    }

    // note: not converting to double yet
    return makeToken(TOKEN_NUMBER);
}

static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++; // lox supports multi-line string
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance(); // the closing quote
    // note: convert literal value to runtime value is deferred to compiling phase
    return makeToken(TOKEN_STRING);
}

Token scanToken() {
    skipWhiteSpace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    // Numbers
    // put the check before switch to avoid making switch cases for 0 ~ 9
    if (isDigit(c)) return number();


    switch (c) {
        // Single-character tokens.
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);

        // One or two character tokens.
        case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL: TOKEN_BANG);
        case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL: TOKEN_EQUAL);
        case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL: TOKEN_LESS);
        case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL: TOKEN_GREATER);

        // String
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}