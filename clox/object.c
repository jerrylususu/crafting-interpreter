#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
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
    return allocateString(chars, length, hash);
}


// copy the characters into heap, so every ObjString owns its char array and can free it
// assumes it can not take ownership of `chars` passed in
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
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