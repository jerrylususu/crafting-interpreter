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
        // When accessing a property, you might get a field, a bit of state stored on the instance,
        // or you could hit a method defined on the instance’s class.

        // note: subtle semantic choice
        // Looking for a field first implies that fields shadow methods
        if (fields.containsKey(name.lexeme)) {
            return fields.get(name.lexeme);
        }

        // if no matching (instance) field, try to find (class) methods
        LoxFunction method = klass.findMethod(name.lexeme);
        if (method != null) return method;

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
