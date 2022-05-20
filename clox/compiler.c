#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
    Token current;
    Token previous;
    bool hadError; // have compile time error
    bool panicMode; // error will be suppressed if in panic mode, ends when the parser reach sync point
} Parser;

// all of Lox's precedence levels, from lowest to highest
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

// even `canAssign` is only useful for setter and assignment
// C compiler requires all parse functions have the same type
typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth; // scope depth of the block where the local var was declared
} Local;

typedef struct {
    Local locals[UINT8_COUNT]; // array of all locals in scope, in the order of declaration appear in code
    int localCount; // how many locals are in scope (array slots in use)
    int scopeDepth; // number of blocks surrounding current bit of code
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

static Chunk* currentChunk() {
    return compilingChunk;
}

static void errorAt(Token* token, const char* message) {
    // if in panic mode, suppress error
    if (parser.panicMode) return;
    // get into panic mode when an error occurs
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    // more often an error will be reported at the token we just consumed
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    // take old current and store it in previous
    // so we can get the lexeme after matching a token
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        // keep looping until find a non-error token
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

// read and validate next token
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

// only consume the token if type is as expected
// but leave the token alone when unmatched, so it can be matched by other type later
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

// emit a bytecode instruction and write a placeholder operand for jump offset
static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);

    // currentChunk()->count is the *next* slot to be written into
    // return the offset of the operand?
    return currentChunk()->count - 2;
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    // array index starts from 0, code[offset] is the first half of jump operand
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler) {
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
}

static void endCompiler() {
    // in this chapter, our VM only deals with one expression.
    emitReturn();

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

static void beginScope() {
    current->scopeDepth++;
}

static void endScope() {
    current->scopeDepth--;

    // discard any variables declared at the scope we just left
    while (current->localCount > 0 &&
            current->locals[current->localCount - 1].depth > current->scopeDepth) {
        emitByte(OP_POP);
        current->localCount--;
    }
}


static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// takes the given token and adds its lexeme to the chunk's constant table as a string
// returns the index of that constant in the constant table
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// note: value stack and locals stack is in always in sync!
// (statement won't affect the stack, and only variable declaration will leave a value on stack)
static int resolveLocal(Compiler* compiler, Token* name) {
    // walk backward to find the *last* declared variable with the name
    // ensure inner local variable correctly shadow locals with the same name in surrounding scopes
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {

            // variable is still in an "uninitialized" state
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }

            return i;
        }
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];

    // note: lifetime of the string for the variable's name is the same as the source code
    local->name = name;
    local->depth = current->scopeDepth;

    // mark the new variable as "uninitialized"
    local->depth = -1;
}

// let the compiler record the existence of a local variable
// declare: add a local variable to compiler's list of variables in current scope
static void declareVariable() {
    // global var is `late-bound`, compiler doesn't track its declaration
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;

    // start backward, looking for an existing variable with the same name
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];

        // current scope is always at the end of the array (since local vars are appended when declared)
        // if found a variable owned by another scope, we have checked all existing variables in current scope
        // depth == -1: variable is uninitialized
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        // disallow two variable with the same name declared in same scope
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    // locals aren't looked by name, no need to put the variable's name into constant table
    // return a dummy table index if the variable is local
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized() {
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// define: mark the variable as it is available (ready) for use
static void defineVariable(uint8_t global) {
    // for local variables, after executing its initializer, a temp value is generated at the top of stack
    // just use that "temp" value as the real, local variable
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    // short circuit: if left hand is false, no need to eval right hand
    // only pop when left hand of `and` is true: when left hand is false, the value becomes the result of the `and` expr
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    // Each binary operator’s right-hand operand precedence is one level higher than its own,
    // because the binary operators are left-associative.
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        default: return; // Unreachable.
    }
}

// directly push true/false/nil on the stack
// instead of storing on constant table, as they have only 3 values
static void literal(bool canAssign) {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return; // Unreachable.
    }
}

static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void varDeclaration() {
    // add the variable name to constant table
    uint8_t global = parseVariable("Expect variable name.");

    // evaluate initializer expression
    // result is saved on stack
    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        // implicitly initializes to `nil`
        emitByte(OP_NIL);
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    // defines the new variable and stores its initial value
    // initial value is left on the stack
    defineVariable(global);
}

// evaluate the expression and discard the result
// used to invoke side effect
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void ifStatement() {
    // note: left paren before if condition is purely declarative (to balance the paren)
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    // at runtime, the condition value will be left on the top of the stack
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // backpatching: use a placeholder for the jump operand
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    // pop the condition value out of stack, before getting into `then` branch
    emitByte(OP_POP);
    statement();

    // add an unconditional jump after `then` branch to prevent fallthrough into `else` branch
    int elseJump = emitJump(OP_JUMP);

    // backpactching: set the jump operand to the actual jump offset
    patchJump(thenJump);
    // pop the condition value out of stack, before getting into `else` branch
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void printStatement() {
    // `print` has been consumed, just parse and compile the expression after it
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

// skip tokens until we reach something that looks like a statement boundary
static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        // look for a preceding token that can end a statement, like a semicolon
        if (parser.previous.type == TOKEN_SEMICOLON) return;

        // look for a subsequent token that begins a statement
        // usually one of the control flow or declaration keywords
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                ; // Do nothing.
        }

        advance();
    }
}

static void declaration() {
    if (match(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    // if we hit a compile error while parsing the previous statement, enter panic mode
    // after the erroneous statement, start synchronizing
    if (parser.panicMode) synchronize();
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

static void grouping(bool canAssign) {
    // for backend, there is nothing to a grouping expression
    // Its sole function is syntactic, letting you insert a lower-precedence expression
    // where a higher precedence is expected.
    // no runtime semantic on its own and doesn't emit any bytecode
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
    // `strtod`'s 2nd param EndPtr points to the location after the number
    // not needed here, as we will scan and parse manually
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void or_(bool canAssign) {
    // if left-hand is truthy, skip over the right operand
    // note: should use `OP_JUMP_IF_TRUE`, but just use what we have now (Section 23.2.1)
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void string(bool canAssign) {
    // +1 and -2: trim the leading and trailing quotation marks
    // note: Lox don't support string escape sequence ('\n'), which could be processed here

    // note: as this is an interpreter, parsing, code-gen and vm share the same heap
    // therefore a pointer into heap is enough
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                    parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    // first try to resolve the variable in local scope, if failed try resolve in global scope
    // use `int` instead of `uint_8` so -1 can indicate not found.
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    // look for an equal sign after identifier, to determine whether this is a set or a get
    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    // Compile the operand. (with correct precedence level limit)
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return; // Unreachable;
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,    PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

// start at the current token, and parses any expression at the given precedence level or higher
// refer to Section 17.6.1 for details
static void parsePrecedence(Precedence precedence) {
    advance();
    // first token is always going to belong to some kind of prefix expression, by definition
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    // only allow assignment when parsing an assignment expression
    // or top-level expression like in an expression statement
    // fixes `a * b = c + d`, see Section 21.4
    // if the variable is nested in higher precedence expression, `canAssign` will be false, and `=` will be ignored
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    // prefix expression parse done, now look for an infix parser
    // if found, the prefix expression is might be its left operand
    // but only when `precedence` is low enough to permit that infix operator
    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    // `=` doesn't get consumed, indicating in further parsing the precedence isn't low enough (See section 21.4)
    // if `canAssign` is true, and assignment is allowed, `=` should get consumed
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

// wrap lookup in a function
// exists solely to handle a declaration cycle in the C code
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

bool compile(const char* source, Chunk* chunk) {
    initScanner(source);

    Compiler compiler;
    initCompiler(&compiler);

    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    // keep compiling declarations until hit the end
    while (!match(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_EOF, "Expect end of expression.");
    endCompiler();
    return !parser.hadError;
}
