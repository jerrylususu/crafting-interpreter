package com.craftinginterpreters.lox;

class Token {
    final TokenType type;
    final String lexeme;
    final Object literal; // literal values for runtime, primitives are auto boxed here
    final int line; 
  
    Token(TokenType type, String lexeme, Object literal, int line) {
      this.type = type;
      this.lexeme = lexeme;
      this.literal = literal;
      this.line = line;
    }
  
    public String toString() {
      return type + " " + lexeme + " " + literal;
    }
  }