package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

/**
 * Runtime representation of an instance of a Lox class
 */
class LoxInstance {
    private LoxClass klass;

    // fields: named bits of state, stored directly in an instance
    // properties: named things a get expression may return
    // every field is a property, but not every property is a field
    private final Map<String, Object> fields = new HashMap<>();


    LoxInstance(LoxClass klass) {
        this.klass = klass;
    }

    Object get(Token name) {
        if (fields.containsKey(name.lexeme)) {
            return fields.get(name.lexeme);
        }

        throw new RuntimeError(name, "Undefined Property '" + name.lexeme + "'.");
    }

    void set(Token name, Object value) {
        // allow freely creating new fields
        // no need to check if key is already present (allow overwrite)
        fields.put(name.lexeme, value);
    }

    @Override
    public String toString() {
        return klass.name + " instance";
    }
}
