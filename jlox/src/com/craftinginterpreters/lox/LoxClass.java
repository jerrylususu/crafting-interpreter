package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

/**
 * Runtime representation of a class
 */
class LoxClass implements LoxCallable{
    final String name;
    final LoxClass superclass;
    private final Map<String, LoxFunction> methods;

    LoxClass(String name, LoxClass superclass, Map<String, LoxFunction> methods) {
        this.superclass = superclass;
        this.name = name;
        this.methods = methods;
    }

    LoxFunction findMethod(String name) {
        // local method has higher priority (override)
        if (methods.containsKey(name)) {
            return methods.get(name);
        }

        // only find up if not found in local
        if (superclass != null) {
            return superclass.findMethod(name);
        }

        return null;
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
        // call initializer if there is one
        LoxFunction initializer = findMethod("init");
        if (initializer != null) {
            // "init" has access to "this", in order to set up the instance
            initializer.bind(instance).call(interpreter, arguments);
        }
        return instance;
    }

    @Override
    public int arity() {
        LoxFunction initializer = findMethod("init");
        // optional initializer
        if (initializer == null) return 0;
        return initializer.arity();
    }
}
