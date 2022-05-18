#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}


// find the entry the key belongs to
// use linear probing for collision handling
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash % capacity;
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];

        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Empty entry.
                // reuse tombstone if possible
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone.
                // save the location of first tombstone
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) { // equality relies on String Interning
            // We found the key.
            return entry;
        }

        // *linear* probing
        index = (index + 1) % capacity;
    }
}

// if the key is found, return true and modify `value`
// otherwise return false
bool tableGet(Table* table, ObjString* key, Value* value) {
    // ensure don't access the bucket array when it's NULL
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

// adjust the capacity of a hash table
// in essence: create a new hash table and re-insert all existing entries
static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    // re-insert existing entries
    // when array size changes, entries may end up in different buckets
    // also discard tombstones in this step
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        // skipping empty slots and tombstone slots
        if (entry->key == NULL) continue;

        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    // release the old array
    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

// add the given key/value pair to the given hash table
// return true if a new entry is added
bool tableSet(Table* table, ObjString* key, Value value) {
    // resize if needed
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;
    // increment count only if the new entry goes into an entirely empty bucket (not reusing tombstone)
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

// if entry is found, return true and place a tombstone (in its slot)
// otherwise return false
bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    // Find the entry.
    Entry* entry = findEntry(table->entries, table->capacity, key);
    // key not found
    if (entry->key == NULL) return false;

    // Place a tombstone in the entry.
    // key=NULL, value=true
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

// copy all entries of one hash table into another
// will be used for method inheritance
void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}


// basically `findEntry`, but checks actual strings (instead of pointer)
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) { // if the slot is empty or tombstone
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->value)) return NULL;
            // otherwise found a tombstone, need to keep searching
        } else if (entry->key->length == length &&
                    entry->key->hash == hash &&
                memcmp(entry->key->chars, chars, length) == 0) {
            // note: the only place in VM where we actually test strings for textual equality
            // We found it.
            return entry->key;
        }

        index = (index + 1) % table->capacity;
    }
}