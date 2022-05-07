package com.craftinginterpreters.lox;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

class Interpreter implements Expr.Visitor<Object>, Stmt.Visitor<Void> {

    // a fixed reference to outermost global env
    // (in current interpreter instance)
    final Environment globals = new Environment();
    // tracks current env, which changes as we enter and exit local scopes
    // initialized as global
    private Environment environment = globals;

    // variable resolution info
    // each expression node is its own Java object, no need for nested tree map
    private final Map<Expr, Integer> locals = new HashMap<>();


    Interpreter() {
        // define a native function that returns the clock
        // utilize the underlying platform (Java & JVM)
        globals.define("clock", new LoxCallable() {
            @Override
            public int arity() { return 0; }

            @Override
            public Object call(Interpreter interpreter, List<Object> arguments) {
                return (double) System.currentTimeMillis() / 1000.0;
            }

            @Override
            public String toString() { return "<native fn>"; }
        });
    }


    void interpret(List<Stmt> statements) {
        try {
            for (Stmt statement: statements) {
                execute(statement);
            }
        } catch (RuntimeError error){
            Lox.runtimeError(error);
        }
    }


    @Override
    public Object visitLiteralExpr(Expr.Literal expr){
        return expr.value;
    }

    // short circuit for logical expressions
    // return a value with appropriate truthiness (not just true/false)
    // note: compare with visitBinaryExpr (always eval both operand)
    @Override
    public Object visitLogicalExpr(Expr.Logical expr) {
        // eval for left side is always needed
        Object left = evaluate(expr.left);

        if (expr.operator.type == TokenType.OR) {
            // OR, if left is true, no need to check right
            if (isTruthy(left)) return left;
        } else {
            // AND, if left is false, no need to check right
            if (!isTruthy(left)) return left;
        }

        // not determined when only eval left
        return evaluate(expr.right);
    }

    @Override
    public Object visitSetExpr(Expr.Set expr) {
        // note: subtle semantic choice of execution order
        // 1. eval object
        // 2. if object is an instance, report error
        // 3. eval value
        Object object = evaluate(expr.object);

        if (!(object instanceof LoxInstance)) {
            throw new RuntimeError(expr.name, "Only instances have fields.");
        }

        Object value = evaluate(expr.value);
        ((LoxInstance)object).set(expr.name, value);
        // value is returned to allow chaining, just like assignment
        return value;
    }

    @Override
    public Object visitUnaryExpr(Expr.Unary expr){
        Object right = evaluate(expr.right);

        switch (expr.operator.type) {
            case BANG:
                return !isTruthy(right);
            case MINUS:
                checkNumberOperand(expr.operator, right);
                // all numbers are double by now
                return -(double)right;
        }

        // Unreachable
        return null;
    }

    @Override
    public Object visitVariableExpr(Expr.Variable expr) {
        return lookUpVariable(expr.name, expr);
    }

    private Object lookUpVariable(Token name, Expr expr) {
        Integer distance = locals.get(expr);
        if (distance != null) {
            return environment.getAt(distance, name.lexeme);
        } else {
            // global vars are left unresolved, no record in the var info map
            // throws runtime error if var is not defined
            return globals.get(name);
        }
    }

    private void checkNumberOperand(Token operator, Object operand) {
        if (operand instanceof Double) return;
        throw new RuntimeError(operator, "Operand must be a number.");
    }

    private void checkNumberOperands(Token operator, Object left, Object right) {
        if (left instanceof Double && right instanceof Double) return;

        throw new RuntimeError(operator, "Operands must be numbers.");
    }

    // only nil and false are falsey, all other are truthy
    private boolean isTruthy(Object object){
        if (object == null) return false;
        // although boolean are stored in primitive type, not boxed (Boolean)
        // it will get auto boxed when stored into expr.literal (type Object)
        // therefore this instanceof works
        if (object instanceof Boolean) return (boolean) object;
        return true;
    }

    // Lox's equality is pretty similar to Java's
    // just need to take care of NullPointerException
    private boolean isEqual(Object a, Object b){
        if(a == null && b == null) return true;
        if(a == null) return false;

        // note: Double.equals don't follow IEEE 754 (NaN != NaN)
        return a.equals(b);
    }

    // stringify a Lox object
    // Any -> Object, nil -> null
    // Boolean -> Boolean, number -> Double, string -> String
    private String stringify(Object object) {
        if (object == null) return "nil";

        if (object instanceof Double) {
            String text = object.toString();
            if (text.endsWith(".0")) {
                text = text.substring(0, text.length() - 2);
            }
            return text;
        }

        // should be String or Boolean?
        return object.toString();
    }

    @Override
    public Object visitGroupingExpr(Expr.Grouping expr){
        // recursively evaluate
        return evaluate(expr.expression);
    }

    private Object evaluate(Expr expr){
        return expr.accept(this);
    }

    private void execute(Stmt stmt) {
        stmt.accept(this);
    }

    void resolve(Expr expr, int depth) {
        // depth: number of environments between the current one and the enclosing one
        locals.put(expr, depth);
    }

    void executeBlock(List<Stmt> statements, Environment environment) {

        // save current scope and recover when finished
        Environment previous = this.environment;
        try {
            this.environment = environment;

            for (Stmt statement : statements) {
                execute(statement);
            }
        } finally {
            // recover previous scope
            // even when runtime error happened
            this.environment = previous;
        }
    }

    @Override
    public Void visitBlockStmt(Stmt.Block stmt) {
        executeBlock(stmt.statements, new Environment(environment));
        return null;
    }

    @Override
    public Void visitClassStmt(Stmt.Class stmt) {
        // first define, then assign
        // 2-stage var binding allows reference to the class inside its own methods
        environment.define(stmt.name.lexeme, null);
        LoxClass klass = new LoxClass(stmt.name.lexeme);
        environment.assign(stmt.name, klass);
        return null;
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) {
        // expression are evaluated, and the result is discarded
        // this is done to trigger the side effect in the evaluation process
        evaluate(stmt.expression);

        // explicit return null to match Void return type
        return null;
    }

    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) {
        // capture the env when the function is declared, not called
        // the lexical scope surrounding the function declaration
        LoxFunction function = new LoxFunction(stmt, environment);
        environment.define(stmt.name.lexeme, function);
        return null;
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt) {
        // not executed parts are not evaluated
        // affects side effect
        if(isTruthy(evaluate(stmt.condition))) {
            execute(stmt.thenBranch);
        } else if (stmt.elseBranch != null) {
            execute(stmt.elseBranch);
        }
        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt) {
        Object value = evaluate(stmt.expression);
        System.out.println(stringify(value));
        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt) {
        // eval return value if present
        Object value = null;
        if (stmt.value != null) value = evaluate(stmt.value);

        throw new Return(value);
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) {
        // if no initializer, default is null
        Object value = null;
        if (stmt.initializer != null) {
            value = evaluate(stmt.initializer);
        }

        environment.define(stmt.name.lexeme, value);
        return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt) {
        while(isTruthy(evaluate(stmt.condition))) {
            execute(stmt.body);
        }
        return null;
    }

    @Override
    public Object visitAssignExpr(Expr.Assign expr) {
        Object value = evaluate(expr.value);

        Integer distance = locals.get(expr);
        if (distance != null) {
            environment.assignAt(distance, expr.name, value);
        } else {
            globals.assign(expr.name, value);
        }

        // assign is an expression, returns eval result of right-hand
        return value;
    }

    @Override
    public Object visitBinaryExpr(Expr.Binary expr){
        // subtle semantic choice: left side is evaluated before right side
        Object left = evaluate(expr.left);
        Object right = evaluate(expr.right);

        switch (expr.operator.type) {
            case BANG_EQUAL:
                return !isEqual(left, right);
            case EQUAL_EQUAL:
                return isEqual(left, right);
            case GREATER:
                // subtle semantic choice: eval both operands before checking type of either
                checkNumberOperands(expr.operator, left, right);
                return (double)left > (double) right;
            case GREATER_EQUAL:
                checkNumberOperands(expr.operator, left, right);
                return (double)left >= (double) right;
            case LESS:
                checkNumberOperands(expr.operator, left, right);
                return (double)left < (double) right;
            case LESS_EQUAL:
                checkNumberOperands(expr.operator, left, right);
                return (double)left <= (double) right;
            case MINUS:
                checkNumberOperands(expr.operator, left, right);
                return (double)left - (double) right;
            case PLUS:
                // note: no concat between string and numbers yet

                // primitive auto boxed when storing to token.literal (Type Object)
                if (left instanceof Double && right instanceof Double) {
                    return (double)left + (double) right;
                }

                if (left instanceof String && right instanceof String){
                    return (String)left + (String) right;
                }

                break;
            case SLASH: // divide
                // note: no div by 0 check yet
                checkNumberOperands(expr.operator, left, right);
                return (double)left / (double) right;
            case STAR: // multiply
                checkNumberOperands(expr.operator, left, right);
                return (double)left * (double) right;
        }

        // Unreachable.
        return null;
    }

    @Override
    public Object visitCallExpr(Expr.Call expr) {
        // eval callee
        // Typically, this expression is just an identifier that looks up
        // the function by its name, but it could be anything.
        Object callee = evaluate(expr.callee);

        List<Object> arguments = new ArrayList<>();

        // note: subtle semantic choice: function param eval order
        for (Expr argument : expr.arguments) {
            arguments.add(evaluate(argument));
        }

        // check callee is actually callable
        if (!(callee instanceof LoxCallable)) {
            throw new RuntimeError(expr.paren, "Can only call functions and classes.");
        }

        LoxCallable function = (LoxCallable) callee;

        // check arity
        if (arguments.size() != function.arity()) {
            throw new RuntimeError(expr.paren,
                    "Expected " + function.arity() + " arguments but got " + arguments.size() + ".");
        }
        return function.call(this, arguments);
    }

    @Override
    public Object visitGetExpr(Expr.Get expr) {
        Object object = evaluate(expr.object);
        if (object instanceof LoxInstance) {
            return ((LoxInstance) object).get(expr.name);
        }

        throw new RuntimeError(expr.name, "Only instances has properties.");
    }
}
