#ifndef clox_value_h
#define clox_value_h

#include <string.h>

#include "common.h"

// forward declare to fix cyclic dependencies
typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING
// NaN Boxing optimization (Section 30.3)
typedef uint64_t Value;

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000) // quiet NaN mask (with the Intel FP Indef flag)

#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

#define IS_BOOL(value)      (((value) | 1) == TRUE_VAL) // FALSE_VAL and TRUE_VAL both will be converted to TRUE_VAL
#define IS_NIL(value)       ((value) == NIL_VAL)
#define IS_NUMBER(value)    (((value) & QNAN) != QNAN)
#define IS_OBJ(value)       (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT)) // check both SIGN_BIT and QNAN are set

#define AS_BOOL(value)      ((value) == TRUE_VAL)
#define AS_NUMBER(value)    valueToNum(value)
#define AS_OBJ(value)       ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL         ((Value)(uint64_t)(QNAN | TAG_NIL))
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj)    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

static inline double valueToNum(Value value) {
    double num;
    mempcpy(&num, &value, sizeof(Value));
    return num;
}

static inline Value numToValue(double num) {
    // a supported idiom for type punning, and should be recognized by the C/C++ compiler
    Value value;
    mempcpy(&value, &num, sizeof(double));
    return value;
}

#else

// VM types, not user types
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        double number;
        Obj* obj;      // pointer to the heap-allocated types
    } as;
} Value;

// check Lox value type
// need to guard before conversion from Lox value to C value
#define IS_BOOL(value)   ((value).type == VAL_BOOL)
#define IS_NIL(value)    ((value).type == VAL_NIL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value)    ((value).type == VAL_OBJ)

// Lox value -> C value
// no AS_NIL: only one `nil`, VAL_NIL doesn't carry extra data
#define AS_OBJ(value)     ((value).as.obj)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

// C value -> Lox Value
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})

#endif

typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif