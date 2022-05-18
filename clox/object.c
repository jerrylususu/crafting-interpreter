#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// avoid the need of cast `void*` back to the desired type
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*) reallocate(NULL, 0, size);
    object->type = type;

    // insert the new object at the head of the linked list
    // therefore no need to maintain its tail
    object->next = vm.objects;
    vm.objects = object;

    return object;
}

// sort like a constructor for ObjString
static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    // intern the new string
    // note: in clox, all strings are interned
    tableSet(&vm.strings, string, NIL_VAL);
    return string;
}

// FNV-1a hash function
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

// claims ownership of `chars` passed in
ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // if already exist a same string, return the reference to it
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        // already obtained ownership, no longer need the duplicate string
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}


// copy the characters into heap, so every ObjString owns its char array and can free it
// assumes it can not take ownership of `chars` passed in
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // if already exist a same string, just return the reference to it, and skip the copying
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    // make a null-terminated c-string for ease of use
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
    }
}