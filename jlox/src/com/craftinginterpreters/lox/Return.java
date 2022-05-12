package com.craftinginterpreters.lox;

class Return extends RuntimeException{
    final Object value;

    Return(Object value) {
        // JVM machinery that we don’t need
        // disables stacktrace
        super(null, null, false, false);
        this.value = value;
    }
}
