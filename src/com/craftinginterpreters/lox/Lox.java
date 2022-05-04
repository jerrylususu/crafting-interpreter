package com.craftinginterpreters.lox;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class Lox {

  private static final Interpreter interpreter = new Interpreter();

  static boolean hadError = false; // error in scanning or parsing (syntax error)
  static boolean hadRuntimeError = false; // error in interpreting (run time error)

  public static void main(String[] args) throws IOException {
    if (args.length > 1) {
      System.out.println("Usage: jlox [script]");
      System.exit(64); // normal exit?
    } else if (args.length == 1) {
      runFile(args[0]);
    } else {
      runPrompt();
    }
  }

  private static void runFile(String path) throws IOException {
    byte[] bytes = Files.readAllBytes(Paths.get(path));
    run(new String(bytes, Charset.defaultCharset()));

    // Indicate an error in the exit code.
    if (hadError) System.exit(65); // exit for syntax error
    if (hadRuntimeError) System.exit(70); // exit for run time error
  }

  private static void runPrompt() throws IOException {
    InputStreamReader input = new InputStreamReader(System.in);
    BufferedReader reader = new BufferedReader(input);

    for (;;) { 
      System.out.print("> ");
      String line = reader.readLine();
      if (line == null) break;
      run(line); // run one line per REPL loop
      hadError = false; // reset error status, no need to kill session
      // no need to check hadRuntimeError (just loop back for next new line)
    }
  }

  private static void run(String source) {
    Scanner scanner = new Scanner(source); // scanner for Lox source code, not java.util.Scanner
    List<Token> tokens = scanner.scanTokens(); // scanning
    Parser parser = new Parser(tokens);
    List<Stmt> statements = parser.parse(); // parsing

    // Stop if there was a syntax error.
    if (hadError) return;

//    System.out.println(new AstPrinter().print(expression));
    interpreter.interpret(statements); // interpreting
  }

  static void error(int line, String message) {
    report(line, "", message);
  }

  static void runtimeError(RuntimeError error) {
    System.out.println(error.getMessage() + "\n[line" + error.token.line + "]");
    hadRuntimeError = true;
  }

  private static void report(int line, String where,
                             String message) {
    System.err.println(
        "[line " + line + "] Error" + where + ": " + message);
    hadError = true;
  }

  static void error(Token token, String message) {
    if (token.type == TokenType.EOF) {
      report(token.line, " at end", message);
    } else {
      report(token.line, " at '" + token.lexeme + "'", message);
    }
  }
}