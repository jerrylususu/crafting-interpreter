#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

// hash table entry
typedef struct {
    ObjString* key;  // in clox, only string key is supported
    Value value;
} Entry;

// hash table
typedef struct {
    int count;       // real entries + tombstones
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif