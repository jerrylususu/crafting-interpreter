# Commented Code for "Crafting Interpreters"

The book: [Crafting Interpreters](http://craftinginterpreters.com/)

This repo contains mainly personal notes, in the form of code comment.

## Current progress

(Theory chapter, no code)

### Part II: Tree-walk Interpreter (Java)

- Chapter 4: Scanning
- (Chapter 5: Representing Code)
- Chapter 6: Parsing Expressions
  - Recursive descent parsing
  - Precedence Level
- Chapter 7: Evaluating Expressions
- Chapter 8: Statements and state
  - Environment Hashmap
- Chapter 9: Control Flow
- Chapter 10: Functions
  - Chaining Environments
- Chapter 11: Resolving and Binding
  - Static Semantic Analysis Pass
- Chapter 12: Classes
- Chapter 13: Inheritance

### Part III: Bytecode Virtual Machine (C)

- Chapter 14: Chunks of Bytecode
- Chapter 15: A Virtual Machine
  - Value Stack
- Chapter 16: Scanning on Demand
- Chapter 17: Compiling Expressions
  - Single-Pass Compilation
  - Vaughan Pratt’s “top-down operator precedence parsing”
- Chapter 18: Types of Values
- Chapter 19: Strings
  - Struct Inheritance
  - Design Note: String Encoding
- Chapter 20: Hash Table
  - String Interning
- Chapter 21: Global Variables
  - Stack effects
- Chapter 22: Local Variables
  - Value stack and Locals stack is always in sync!
- Chapter 23: Jumping Back and Forth
  - Control Flow
- Chapter 24: Calls and Functions
  - Call Frame
- Chapter 25: Closures
  - Upvalue
- Chapter 26: Garbage Collection
  - Mark-Sweep, Tricolor
- Chapter 27: Classes and Instances
  - Property vs. Field
- Chapter 28: Methods and Initializers
  - Superinstruction as an optimization
- Chapter 29: Superclasses
  - Copy-down Inheritance