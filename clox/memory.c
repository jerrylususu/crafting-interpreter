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

#ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isMarked = true;
}

// only heap objects need to be marked
void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
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

void collectGarbage() {
#ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
#endif

    markRoots();

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
}