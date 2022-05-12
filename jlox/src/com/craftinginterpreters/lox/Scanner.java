package com.craftinginterpreters.lox;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.craftinginterpreters.lox.TokenType.*; 

class Scanner {
  private final String source; // a big string
  private final List<Token> tokens = new ArrayList<>();
  private int start = 0; // offset into string, first char offset of the lexeme
  private int current = 0; // current char offset of lexeme
  private int line = 1; // line number of current character

  private static final Map<String, TokenType> keywords;

  // run only once when the class invoked
  static {
    keywords = new HashMap<>();
    keywords.put("and",    AND);
    keywords.put("class",  CLASS);
    keywords.put("else",   ELSE);
    keywords.put("false",  FALSE);
    keywords.put("for",    FOR);
    keywords.put("fun",    FUN);
    keywords.put("if",     IF);
    keywords.put("nil",    NIL);
    keywords.put("or",     OR);
    keywords.put("print",  PRINT);
    keywords.put("return", RETURN);
    keywords.put("super",  SUPER);
    keywords.put("this",   THIS);
    keywords.put("true",   TRUE);
    keywords.put("var",    VAR);
    keywords.put("while",  WHILE);
  }

  Scanner(String source) {
    this.source = source;
  }

  List<Token> scanTokens() {
    while (!isAtEnd()) {
      // We are at the beginning of the next lexeme.
      start = current;
      scanToken();
    }

    tokens.add(new Token(EOF, "", null, line));
    return tokens;
  }

  private void scanToken() {
    char c = advance(); // consume one char
    switch (c) { 
      // single char tokens
      case '(': addToken(LEFT_PAREN); break;
      case ')': addToken(RIGHT_PAREN); break;
      case '{': addToken(LEFT_BRACE); break;
      case '}': addToken(RIGHT_BRACE); break;
      case ',': addToken(COMMA); break;
      case '.': addToken(DOT); break;
      case '-': addToken(MINUS); break;
      case '+': addToken(PLUS); break;
      case ';': addToken(SEMICOLON); break;
      case '*': addToken(STAR); break;

      // need special treat as it may concat with other char to form other lexeme
      // maximal match principle
      case '!': 
        addToken(match('=') ? BANG_EQUAL : BANG);
        break;
      case '=':
        addToken(match('=') ? EQUAL_EQUAL : EQUAL);
        break;
      case '<':
        addToken(match('=') ? LESS_EQUAL : LESS);
        break;
      case '>':
        addToken(match('=') ? GREATER_EQUAL : GREATER);
        break;

      // may be division, or comment
      case '/':
        if (match('/')) {
          // A comment goes until the end of the line.
          // keep consuming until reach the end of line
          // use peek, not match: so newline can be handled later (add line number)
          while (peek() != '\n' && !isAtEnd()) advance();

          // no addToken here
        } else {
          addToken(SLASH);
        }
        break;
      
      // handle whitespace
      case ' ':
      case '\r':
      case '\t':
          // Ignore whitespace.
          // note: the whitespace has been consumed, next lexeme starts at next char
          break;

      case '\n':
          line++;
          break;

      // handle string
      // idea for long patterns: detect the start, then get into special function
      case '"': string(); break;

      default:

        // handle other long patterns
        if (isDigit(c)) { // number
          number();
        } else if (isAlpha(c)) { // identifier & reserved words
          identifier();
        } else {
          
          // note: the unexpected char is still consumed
          // to prevent stuck in inf loop

          // note: also continues scanning, instead of
          // stopping when just one error is found
          Lox.error(line, "Unexpected character.");
        }

        
        break;
    }
  }

  private void identifier() {
    while (isAlphaNumeric(peek())) advance();

    String text = source.substring(start, current);
    TokenType type = keywords.get(text); // has match, keyword, add with corresponding token type
    if (type == null) type = IDENTIFIER; // no match, plain identifier
    addToken(type); 
  }


  private void number() {
    while (isDigit(peek())) advance();

    // Look for a fractional part.
    // contains a '.' and at least one number digit
    if (peek() == '.' && isDigit(peekNext())) {
      // Consume the "."
      advance();

      while (isDigit(peek())) advance();
    }

    addToken(NUMBER,
        Double.parseDouble(source.substring(start, current)));
  }

  private void string() {
    // keep consume until meet the closing quote
    while (peek() != '"' && !isAtEnd()) {
      // actually allow multi line string
      if (peek() == '\n') line++;
      advance();
    }

    if (isAtEnd()) {
      Lox.error(line, "Unterminated string.");
      return;
    }

    // The closing ".
    advance();

    // Trim the surrounding quotes.
    String value = source.substring(start + 1, current - 1);
    // add token with string literal value
    addToken(STRING, value);
  }

  // conditional advance: only consume if the char is as expected
  // can also be seen as lookahead
  private boolean match(char expected) {
    if (isAtEnd()) return false;
    // by this time, current is the pos of next unconsumed char
    if (source.charAt(current) != expected) return false;

    current++; // consume next char, only when as expected
    return true;
  }

  // lookahead, don't actually consume
  // 1 char lookahead
  private char peek() {
    if (isAtEnd()) return '\0'; // null terminated?
    return source.charAt(current);
  }

  // 2 char lookahead
  private char peekNext() {
    if (current + 1 >= source.length()) return '\0';
    return source.charAt(current + 1);
  }

  private boolean isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
            c == '_';
  }

  private boolean isAlphaNumeric(char c) {
    return isAlpha(c) || isDigit(c);
  }

  private boolean isDigit(char c) {
    return c >= '0' && c <= '9';
  } 

  private boolean isAtEnd() {
    return current >= source.length();
  }

  // consume next token and return it
  // consume: current++, edits value of current
  private char advance() {
    // first read the char at pos current
    // then incr current
    // after invoke, current is the pos of next unconsumed char
    return source.charAt(current++);
  }

  private void addToken(TokenType type) {
    addToken(type, null);
  }

  private void addToken(TokenType type, Object literal) {
    String text = source.substring(start, current);
    tokens.add(new Token(type, text, literal, line));
  }
}