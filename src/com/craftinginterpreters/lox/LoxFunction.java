package com.craftinginterpreters.lox;

import java.util.List;

// separates runtime phase (Interpreting) from front end (Parsing)
// basically a wrapper with runtime info around Stmt.Function
class LoxFunction implements LoxCallable {
    private final Stmt.Function declaration;

    LoxFunction(Stmt.Function declaration) {
        this.declaration = declaration;
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
        // create a new environment at each call, not at the function declaration
        // (function-local env)
        Environment environment = new Environment(interpreter.globals);
        // bind values to parameters
        for (int i = 0; i < declaration.params.size(); i++) {
            environment.define(declaration.params.get(i).lexeme, arguments.get(i));
        }

        try {
            interpreter.executeBlock(declaration.body, environment);
        } catch (Return returnValue) {
            return returnValue.value;
        }
        // if no return statement, implicitly return nil
        return null;
    }

    @Override
    public int arity() {
        return declaration.params.size();
    }

    @Override
    public String toString() {
        return "<fn " + declaration.name.lexeme + ">";
    }

}
