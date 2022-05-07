package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

/**
 * Runtime representation of a class
 */
class LoxClass implements LoxCallable{
    final String name;

    LoxClass(String name) {
        this.name = name;
    }

    @Override
    public String toString() {
        return name;
    }

    /**
     * When calling on class itself, creates a new instance of the class and returns it
     * @param interpreter
     * @param arguments
     * @return
     */
    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
        LoxInstance instance = new LoxInstance(this);
        return instance;
    }

    @Override
    public int arity() {
        // no user-defined ctor for now
        return 0;
    }
}
