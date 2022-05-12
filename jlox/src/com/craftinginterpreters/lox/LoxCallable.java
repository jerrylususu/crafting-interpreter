package com.craftinginterpreters.lox;

import java.util.List;

// any Lox object that can be called like a function will implement LoxCallable
// includes user-defined functions, naturally,
// but also class objects since classes are “called” to construct new instances.
interface LoxCallable {
    int arity(); // number of arguments
    Object call(Interpreter interpreter, List<Object> arguments);
}
