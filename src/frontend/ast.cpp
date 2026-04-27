#include "ast.h"

// NumberExpr
void NumberExpr::accept(ASTVisitor* visitor) { visitor->visit(this); }

// IdentifierExpr
void IdentifierExpr::accept(ASTVisitor* visitor) { visitor->visit(this); }

// ParenExpr
void ParenExpr::accept(ASTVisitor* visitor) { visitor->visit(this); }

// FunctionCallExpr
void FunctionCallExpr::accept(ASTVisitor* visitor) { visitor->visit(this); }

// UnaryExpr
void UnaryExpr::accept(ASTVisitor* visitor) { visitor->visit(this); }

// BinaryExpr
void BinaryExpr::accept(ASTVisitor* visitor) { visitor->visit(this); }

// Block
void Block::accept(ASTVisitor* visitor) { visitor->visit(this); }

// EmptyStmt
void EmptyStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// ExprStmt
void ExprStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// AssignStmt
void AssignStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// VarDeclStmt
void VarDeclStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// IfStmt
void IfStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// WhileStmt
void WhileStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// BreakStmt
void BreakStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// ContinueStmt
void ContinueStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// ReturnStmt
void ReturnStmt::accept(ASTVisitor* visitor) { visitor->visit(this); }

// FuncDef
void FuncDef::accept(ASTVisitor* visitor) { visitor->visit(this); }

// CompUnit
void CompUnit::accept(ASTVisitor* visitor) { visitor->visit(this); }
