#include <stdlib.h>

#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

// handle all dynamic memory operation
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    // only trigger GC when expanding, since GC itself will call `reallocate` to free or shrink
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage();
#endif
    }

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    // when oldSize=0, realloc() is equivalent to calling malloc()
    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

void markObject(Obj* object) {
    if (object == NULL) return;
    // ensure GC won't stuck in reference cycles
    if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;

    // put gray objects in the worklist
    // note: use stack here since it's the simplest to implement with a dynamic array in C
    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        // use system `realloc`, not our own `reallocate`: prevent recursive GC invoke
        vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);

        if (vm.grayStack == NULL) exit(1);
    }

    vm.grayStack[vm.grayCount++] = object;
}

// only heap objects need to be marked
void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) {
        markValue(array->values[i]);
    }
}

// turn white objects gray, gray objects black
// definition of black: `isMarked == true` and no longer in the gray stack
// trace outgoing references by type
static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*) object;
            markObject((Obj*)closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                markObject((Obj*)closure->upvalues[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*) object;
            markObject((Obj*)function->name);
            markArray(&function->chunk.constants);
            break;
        }
        case OBJ_UPVALUE:
            // note: when upvalue is still open, `closed` is NIL_VAL, so only closed upvalue will be marked
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_NATIVE:
        case OBJ_STRING:
            break;
    }
}

// type-specific code to handle each object type's special needs of freeing
static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
        case OBJ_CLOSURE: {
            // only free ObjClosure, not the ObjFunction, as the closure doesn't own the function
            // other closures and surrounding functions may still have reference to it
            ObjClosure* closure = (ObjClosure*) object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(&function->chunk);
            // note: function name (ObjString) will be handled by garbage collector (once we have one)
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE:
            // ObjNative doesn't own any extra memory
            FREE(ObjNative, object);
            break;
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_UPVALUE:
            // Multiple closure can close over the same variable, so ObjUpvalue doesn't on the variable it references.
            FREE(ObjUpvalue, object);
            break;
    }
}

static void markRoots() {
    // (Lox) Value on stack
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        markValue(*slot);
    }

    // ObjClosure
    for (int i = 0; i < vm.frameCount; i++) {
        markObject((Obj*)vm.frames[i].closure);
    }

    // ObjUpvalue
    for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
    }

    // globals
    markTable(&vm.globals);

    // GC can also begin during compilation. Any value the compiler directly accesses is also root.
    markCompilerRoots();
}

static void traceReferences() {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

    markRoots();
    traceReferences();

#ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
#endif
}


void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }

    free(vm.grayStack);
}