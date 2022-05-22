#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "table.h"
#include "value.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_CLASS(value)        (isObjType(value, OBJ_CLASS))
#define IS_CLOSURE(value)      (isObjType(value, OBJ_CLOSURE))
#define IS_FUNCTION(value)     (isObjType(value, OBJ_FUNCTION))
#define IS_INSTANCE(value)     (isObjType(value, OBJ_INSTANCE))
#define IS_NATIVE(value)       (isObjType(value, OBJ_NATIVE))
#define IS_STRING(value)       (isObjType(value, OBJ_STRING))

#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)

typedef enum {
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_UPVALUE
} ObjType;

// base class for heap-allocated objects
struct Obj {
    ObjType type;
    bool isMarked; // marked as reachable (for GC)
    struct Obj* next;
};

typedef struct {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk; // each function's bytecode lives in its own chunk
    ObjString* name;
} ObjFunction;

// pointer to a C function
// access the arguments using `args` pointer, and return the result as a Lox Value
typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
    Obj obj;
    NativeFn function; // pointer to the C function implementing native behavior
} ObjNative;

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash; // clox strings are immutable, so it's safe to cache hash eagerly
};

// runtime representation for upvalues
typedef struct ObjUpvalue {
    Obj obj;
    Value* location; // pointer to the value (open: on stack, closed: in heap)
    Value closed; // where the closed-over values live in heap
    struct ObjUpvalue* next; // intrusive list of ObjUpvalues, use to ensure only 1 Upvalue for each local
} ObjUpvalue;

// runtime representation of a function with captured variables
typedef struct {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues; // pointer to a dynamically allocated array of pointers to upvalues
    int upvalueCount; // `function` may be freed earlier by GC, so we store the upvalueCount redundantly.
} ObjClosure;

typedef struct {
    Obj obj;
    ObjString* name;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass* klass; // pointer to the class that it is an instance of
    Table fields; // each instance has its own fields, and user can add fields at runtime
} ObjInstance;

ObjClass* newClass(ObjString* name);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjInstance* newInstance(ObjClass* klass);
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);
void printObject(Value value);

// use a function to prevent evaluate parameter multiple times
// as the expression evaluated might have side effect
static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif