#include <stdlib.h>

#include "memory.h"
#include "vm.h"

// handle all dynamic memory operation
void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    // when oldSize=0, realloc() is equivalent to calling malloc()
    void* result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}

// type-specific code to handle each object type's special needs of freeing
static void freeObject(Obj* object) {
    switch (object->type) {
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
    }
}

void freeObjects() {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
}