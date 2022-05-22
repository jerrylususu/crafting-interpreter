#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "object.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value* args) {
    // clock(void) returns the number of clock ticks elapsed since the program was launched
    return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
    // forward formatting to printf
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    // print stack trace (innermost first)
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        // -1: ip already sitting on the next instruction to be executed, but we want the previous failed instruction.
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

// define a new native function exposed to Lox programs
static void defineNative(const char* name, NativeFn function) {
    // push and pop on the stack: ensure GC knows we're not done with the name and ObjFunction yet.
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM() {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;

    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);

    defineNative("clock", clockNative);
}

void freeVM() {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop() {
    vm.stackTop--;
    // no need to explicitly erase
    return *vm.stackTop;
}

// return a Value from stack, but doesn't pop it
// `distance`: 0 is the top, 1 is one slot down
static Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

// initializes the next CallFrame on the stack
static bool call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (vm.frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;
    // ensure the argument already on the stack line up with parameters
    // -1: account for stack slot 0 set aside by compiler (for when we add methods later)
    // parameters starts at slot 1
    frame->slots = vm.stackTop - argCount - 1;
    return true;
}

// make sure the callee is indeed callable
static bool callValue(Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                // ignoring arguments for initializer for now
                vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
                return true;
            }
            case OBJ_CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                // invoke the underlying C function
                Value result = native(argCount, vm.stackTop - argCount);
                // note: the callee is also popped out along with arguments
                vm.stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break; // Non-callable object type.
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

// closing over a local variable
static ObjUpvalue* captureUpvalue(Value* local) {
    // try to reuse an existing upvalue if there is one
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;
    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    // found one existing, just reuse
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    // not found, create a new upvalue and add that to the open upvalue list
    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

// close every open upvalue it can find pointing to the slot, or any slot above it on stack
static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

// in Lox, only `nil` and `false` are falsey
// every other value behaves like `true`
static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
    // peek and pop later: keep alive for GC
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    // transfer ownership of the char array
    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

static InterpretResult run() {
    // current topmost CallFrame
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

// make that scoping more explicit

// read the byte and advance
// note: ip advanced after reading and before executing
#define READ_BYTE() (*frame->ip++)

#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

// read next byte and use it as index to look up the constant
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define READ_STRING() AS_STRING(READ_CONSTANT())

// handle binary operation
// note: do-while trick: allow multiple statement in a block and having a trailing semicolon
// note: pop out in reverse order (right first, left second)
#define BINARY_OP(valueType, op) \
    do {  \
        if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_NUMBER(pop()); \
        double a = AS_NUMBER(pop()); \
        push(valueType(a op b)); \
    } while (false)

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        // show value stack
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");

        disassembleInstruction(&frame->closure->function->chunk,
                               (int)(frame->ip - frame->closure->function->chunk.code));
#endif

        uint8_t instruction;
        // decoding / dispatching
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_POP: pop(); break;
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]); // so later instructions can find it
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                // don't pop, assignment is an expression, which produces a value.
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                // get the name of the variable
                ObjString* name = READ_STRING();
                // note: don't pop until after the variable is added into `globals`
                // ensure the VM can still find the value if a GC is triggered in the middle of adding
                // which is possible since hash table requires dynamic allocation when resizing
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    // call to `tableSet` stores the variable, even if the variable wasn't defined
                    // (tableSet returns true if the key is new)
                    // need to delete the zombie value from the table
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                // note: don't pop, assignment is an expression, and the assigned value needs to remain on stack
                break;
            }
            case OP_GET_PROPERTY: {
                if (!IS_INSTANCE(peek(0))) {
                    // property def: general term we use to refer to any named entity you can access on an instance
                    runtimeError("Only instances have properties.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjInstance* instance = AS_INSTANCE(peek(0));
                ObjString* name = READ_STRING();

                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(); // Instance.
                    push(value);
                    break;
                }

                runtimeError("Undefined property '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            case OP_SET_PROPERTY: {
                if (!IS_INSTANCE(peek(1))) {
                    // fields def: subset of properties that are backed by the instance’s state.
                    // in Lox, we can only set fields, not non-field properties
                    runtimeError("Only instances have fields");
                    return INTERPRET_RUNTIME_ERROR;
                }

                // before execution, on stack: [instance] [value] (top)
                ObjInstance* instance = AS_INSTANCE(peek(1));
                tableSet(&instance->fields, READ_STRING(), peek(0));
                Value value = pop();
                pop(); // Instance.
                push(value);
                // after execution, on stack: [value] (top)
                break;
            }
            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(NUMBER_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
            case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;
            case OP_NEGATE:
                if (!IS_NUMBER(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                // if false, apply the jump offset
                // note: if we want, this can be done purely arithmetically
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset; // jump backwards
                break;
            }
            case OP_CALL: {
                int argCount = READ_BYTE();
                // peek(argCount): the function to be called
                if (!callValue(peek(argCount), argCount)) {
                    return INTERPRET_RUNTIME_ERROR;
                }
                // update the (local) cached pointer of current frame in `run()`
                // VM will read the `ip` from the new CallFrame in the next cycle
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLOSURE: {
                // all function calls are now wrapped in ObjClosure
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        // when OP_CLOSURE executes, current function is the surrounding one of the closure
                        // and current function's closure is stored in the topmost CallFrame
                        closure->upvalues[i] = frame->closure->upvalues[i];
                    }
                }
                break;
            }
            case OP_CLOSE_UPVALUE:
                // on execution, the local to be closed is on top of the stack
                closeUpvalues(vm.stackTop - 1);
                // after moving the variable to heap, its slot can be discarded
                pop();
                break;
            case OP_RETURN: {
                // return value is at the top of value stack
                Value result = pop();
                // close every remaining open upvalue owned by the returning function
                closeUpvalues(frame->slots);
                // discard current CallFrame
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                // push the return value back to the value stack of previous frame
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }
            case OP_CLASS:
                push(OBJ_VAL(newClass(READ_STRING())));
                break;
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    // stack slot 0 used to store the function being called
    // push & pop: prevent GC from cleaning heap-allocated objects during this operation
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);

    return run();
}