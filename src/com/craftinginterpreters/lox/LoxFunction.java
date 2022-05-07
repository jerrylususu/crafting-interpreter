package com.craftinginterpreters.lox;

import java.util.List;

// separates runtime phase (Interpreting) from front end (Parsing)
// basically a wrapper with runtime info around Stmt.Function
class LoxFunction implements LoxCallable {
    private final Stmt.Function declaration;
    private final Environment closure;
    private final boolean isInitializer;

    LoxFunction(Stmt.Function declaration, Environment closure, boolean isInitializer) {
        this.isInitializer = isInitializer;
        this.closure = closure;
        this.declaration = declaration;
    }

    LoxFunction bind(LoxInstance instance) {
        // create a new environment nestled inside the method’s original closure
        Environment environment = new Environment(closure);
        // declare “this” as a variable in that environment and bind it to the given instance,
        // the instance that the method is being accessed from
        environment.define("this", instance);
        return new LoxFunction(declaration, environment, isInitializer);
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
        // create a new environment at each call, not at the function declaration
        // (function-local env)
        // use closure as parent, so surrounding vars are captured
        Environment environment = new Environment(closure);
        // bind values to parameters
        for (int i = 0; i < declaration.params.size(); i++) {
            environment.define(declaration.params.get(i).lexeme, arguments.get(i));
        }

        try {
            interpreter.executeBlock(declaration.body, environment);
        } catch (Return returnValue) {
            // allow early return without return value in "init"
            // always return "this"
            if (isInitializer) return closure.getAt(0, "this");

            return returnValue.value;
        }

        // special case: if "init" is directly called (on an instance), return "this"
        if (isInitializer) return closure.getAt(0, "this");

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
