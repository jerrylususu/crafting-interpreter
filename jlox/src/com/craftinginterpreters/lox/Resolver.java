package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Stack;

// a static analysis pass for variable resolution (chapter 11.1)
// after parsing but before interpreting
// don't rely on runtime state
//
// pay attention on:
// - block: introduce a new scope
// - function decl: introduce a new scope, bind params
// - var decl: add a new var to current scope
// - var & assignment expr: need to resolve vars
class Resolver implements Expr.Visitor<Void>, Stmt.Visitor<Void> {

    private final Interpreter interpreter;

    // lexical scopes behave like a stack
    // scope stack only used for local block scopes, vars decl in global scope not tracked
    // String: var name, Boolean: var is ready (defined), see visitVarStmt
    private final Stack<Map<String, Boolean>> scopes = new Stack<>();

    // help to check whether the usage of "return" is valid
    private FunctionType currentFunction = FunctionType.NONE;



    Resolver(Interpreter interpreter) {
        this.interpreter = interpreter;
    }

    private enum FunctionType {
        NONE,
        FUNCTION,
        INITIALIZER,
        METHOD
    }

    private enum ClassType {
        NONE,
        CLASS,
        SUBCLASS
    }

    // helps to check if "this" is used outside a method
    private ClassType currentClass = ClassType.NONE;

    void resolve(List<Stmt> statements) {
        for (Stmt statement : statements) {
            resolve(statement);
        }
    }

    private void resolveFunction(Stmt.Function function, FunctionType type) {
        // Lox has local functions, so you can nest function declarations arbitrarily deeply
        // piggyback on the JVM and store previous value on stack
        FunctionType enclosingFunction = currentFunction;
        currentFunction = type;

        beginScope();
        // bind var for each param
        for (Token param : function.params) {
            // question: actually only define is needed? (effectively)
            declare(param);
            define(param);
        }
        resolve(function.body);
        endScope();
        currentFunction = enclosingFunction;
    }

    private void beginScope() {
        scopes.push(new HashMap<String, Boolean>());
    }

    private void endScope() {
        scopes.pop();
    }

    private void declare(Token name) {
        // only take care of local scopes, ignore global scope
        if (scopes.isEmpty()) return;

        Map<String, Boolean> scope = scopes.peek();
        // detect multiple var decl in local scope
        // (mistake in local scope, but accepted in global scope)
        if (scope.containsKey(name.lexeme)) {
            Lox.error(name, "Already a variable with this name in this scope.");
        }
        // false: var is not ready yet
        scope.put(name.lexeme, false);
    }

    private void define(Token name) {
        if (scopes.isEmpty()) return;
        // true: var is ready
        scopes.peek().put(name.lexeme, true);
    }

    private void resolveLocal(Expr expr, Token name) {
        // walk from inner scope to outer scope (stack top to bottom)
        for (int i = scopes.size() - 1; i >= 0; i--) {
            if (scopes.get(i).containsKey(name.lexeme)) {
                // include number of environments between the current one and the enclosing one
                interpreter.resolve(expr, scopes.size() - 1 - i);
                return;
            }
        }

        // if not found in any scope, assume this var is global
        // leave it unresolved
    }


    @Override
    public Void visitBlockStmt(Stmt.Block stmt) {
        beginScope();
        resolve(stmt.statements);
        endScope();
        return null;
    }

    @Override
    public Void visitClassStmt(Stmt.Class stmt) {
        ClassType enclosingClass = currentClass;
        currentClass = ClassType.CLASS;

        declare(stmt.name);
        define(stmt.name);

        if (stmt.superclass != null && stmt.name.lexeme.equals(stmt.superclass.name.lexeme)) {
            Lox.error(stmt.superclass.name, "A class can't inherit from itself.");
        }

        // Lox allows class declarations even inside blocks,
        // so it???s possible the superclass name refers to a local variable,
        // therefore has the need to resolve
        if (stmt.superclass != null) {
            currentClass = ClassType.SUBCLASS;
            resolve(stmt.superclass);
        }

        // bind "super" when the class is defined
        // all instance of the class has the one and same "super"
        if (stmt.superclass != null) {
            beginScope();
            scopes.peek().put("super", true);
        }

        beginScope();
        // put "this" into scope so it can be resolved
        scopes.peek().put("this", true);

        for (Stmt.Function method : stmt.methods) {
            // important for resolving "this"
            FunctionType declaration = FunctionType.METHOD;
            // mark whether current method is "init" (to limit its valid return values)
            if (method.name.lexeme.equals("init")) {
                declaration = FunctionType.INITIALIZER;
            }
            resolveFunction(method, declaration);
        }

        endScope();

        if (stmt.superclass != null) endScope();

        currentClass = enclosingClass;
        return null;
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) {
        // define name eagerly: allows recursive function refer to itself
        // question: only "define" is needed, no need for declare here?
        declare(stmt.name);
        define(stmt.name);

        resolveFunction(stmt, FunctionType.FUNCTION);
        return null;
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt) {
        // note: different from interpretation, no control flow here
        // analyze any branch that could be run
        resolve(stmt.condition);
        resolve(stmt.thenBranch);
        if (stmt.elseBranch != null) resolve(stmt.elseBranch);
        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt) {
        if (currentFunction == FunctionType.NONE) {
            Lox.error(stmt.keyword, "Can't return from top-level code.");
        }

        if (stmt.value != null) {
            if (currentFunction == FunctionType.INITIALIZER) {
                // disallow explicit return with value in class initializer
                Lox.error(stmt.keyword, "Can't return a value from an initializer");
            }
            resolve(stmt.value);
        }
        return null;
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) {
        // separate declare and define to prevent self-reference in initializer
        // var a = "outer";
        // {
        //   var a = a;
        // }

        declare(stmt.name);
        if (stmt.initializer != null) {
            resolve(stmt.initializer);
        }
        define(stmt.name);
        return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt) {
        resolve(stmt.condition);
        resolve(stmt.body);
        return null;
    }

    @Override
    public Void visitAssignExpr(Expr.Assign expr) {
        // resolve the assigned value
        resolve(expr.value);
        // resolve the var being assigned to (assign target)
        resolveLocal(expr, expr.name);
        return null;
    }

    @Override
    public Void visitBinaryExpr(Expr.Binary expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitCallExpr(Expr.Call expr) {
        resolve(expr.callee);

        for (Expr argument : expr.arguments) {
            resolve(argument);
        }

        return null;
    }

    @Override
    public Void visitGetExpr(Expr.Get expr) {
        // properties are looked up dynamically and property access happens in the interpreter at runtime
        // only resolve to the left of the dot
        resolve(expr.object);
        return null;
    }

    @Override
    public Void visitGroupingExpr(Expr.Grouping expr) {
        resolve(expr.expression);
        return null;
    }

    @Override
    public Void visitLiteralExpr(Expr.Literal expr) {
        // A literal expression doesn???t mention any variables and
        // doesn???t contain any subexpressions so there is no work to do.
        return null;
    }

    @Override
    public Void visitLogicalExpr(Expr.Logical expr) {
        // no short circuit in static analysis
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitSetExpr(Expr.Set expr) {
        // like Expr.Get, property is dynamically evaluated,
        // no need to solve property (name) here.
        resolve(expr.value);
        resolve(expr.object);
        return null;
    }

    @Override
    public Void visitSuperExpr(Expr.Super expr) {
        // check if the use of "super" is valid
        if (currentClass == ClassType.NONE) {
            Lox.error(expr.keyword, "Can't use 'super' outside a class.");
        } else if (currentClass != ClassType.SUBCLASS) {
            Lox.error(expr.keyword, "Can't use 'super' in a class with no superclass.");
        }

        resolveLocal(expr, expr.keyword);
        return null;
    }

    @Override
    public Void visitThisExpr(Expr.This expr) {
        // check if the use of "this" is valid (in a class method)
        if (currentClass == ClassType.NONE) {
            Lox.error(expr.keyword, "Can't use 'this' outside of a class.");
        }

        // resolve "this" just like any other variable
        // "this" is just a reserved var name
        resolveLocal(expr, expr.keyword);
        return null;
    }

    @Override
    public Void visitUnaryExpr(Expr.Unary expr) {
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitVariableExpr(Expr.Variable expr) {
        if (!scopes.isEmpty() &&
            scopes.peek().get(expr.name.lexeme) == Boolean.FALSE) { // var not ready
            Lox.error(expr.name, "Can't read local variable in its own initializer.");
        }

        resolveLocal(expr, expr.name);
        return null;
    }

    // apply visitor pattern
    private void resolve(Stmt stmt) {
        stmt.accept(this);
    }

    private void resolve(Expr expr) {
        expr.accept(this);
    }



}


