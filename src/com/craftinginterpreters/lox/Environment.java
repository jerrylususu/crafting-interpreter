package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

public class Environment {

    // support scoping
    // search parent when variable name not found in current scope
    final Environment enclosing;

    // string as key, not Token
    // Token represents a unit of code existed in a specific location in source file
    // not considering scope now
    private final Map<String, Object> values = new HashMap<>();

    Environment() {
        // global scope
        enclosing = null;
    }

    Environment(Environment enclosing){
        this.enclosing = enclosing;
    }

    Object get(Token name) {
        if(values.containsKey(name.lexeme)) {
            return values.get(name.lexeme);
        }

        // search in parent scope if not found locally
        if (enclosing != null) return enclosing.get(name);

        // if var name not found
        // throw a run time error, not syntax error
        // as long as variable is assigned before use, we allow to refer to it before its declaration
        // helps to define recursion programs.

        // We’d let you define an uninitialized variable, but if you accessed it
        // before assigning to it, a runtime error would occur.
        throw new RuntimeError(name, "Undefined variable'" + name.lexeme + "'.");
    }

    // used by 'a = 1' (no 'var')
    void assign(Token name, Object value) {
        if (values.containsKey(name.lexeme)) {
            values.put(name.lexeme, value);
            return;
        }

        // assign in parent scope if not found locally
        if (enclosing != null) {
            enclosing.assign(name, value);
            return;
        }

        // a runtime error if the key doesn’t already exist in the environment’s variable map
        // no implicit variable declaration
        throw new RuntimeError(name, "Undefined variable'" + name.lexeme +"'.");
    }

    // used by 'var a = 1'
    // allow global level var re-define (define a var more than once using 'var')
    // new variable always defined in current scope
    void define(String name, Object value) {
        values.put(name, value);
    }
}
