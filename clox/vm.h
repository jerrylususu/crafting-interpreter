#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
// note: still might overflow if a function use too much local var, yet it's simple
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

// represents a single ongoing function call
typedef struct {
    ObjClosure* closure;
    uint8_t* ip; // caller stores its own ip before invoking callee, as the return address
    Value* slots; // pointing to the first slot the function can use in VM's value stack
} CallFrame;

typedef struct {
    CallFrame frames[FRAMES_MAX];
    int frameCount; // height of call stack (number of ongoing calls)

    Value stack[STACK_MAX]; // value stack
    Value* stackTop; // where the next value to be pushed will go (not the top)
    Table globals; // global variables
    Table strings; // a hash table (set) for all interned strings
    ObjUpvalue* openUpvalues; // a linked list of open upvalues (to ensure only 1 upvalue for each local)
    Obj* objects; // a linked list of heap-allocated objects
} VM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif