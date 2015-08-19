//
// nodes.h
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is part of fcd.
// 
// fcd is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// fcd is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with fcd.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef ast_nodes_cpp
#define ast_nodes_cpp

#include "dumb_allocator.h"
#include "llvm_warnings.h"
#include "not_null.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/IR/CFG.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
SILENCE_LLVM_WARNINGS_END()

#include <algorithm>
#include <string>

#pragma mark - Expressions
class ExpressionVisitor;

struct Expression
{
	enum ExpressionType
	{
		Value, Token, UnaryOperator, NAryOperator, Call, Cast, Numeric, Ternary,
	};
	
	void print(llvm::raw_ostream& os) const;
	void dump() const;
	
	virtual ExpressionType getType() const = 0;
	virtual void visit(ExpressionVisitor& visitor) = 0;
	virtual bool isReferenceEqual(const Expression* that) const = 0;
};

struct UnaryOperatorExpression : public Expression
{
	enum UnaryOperatorType : unsigned
	{
		Min = 0,
		
		// The SSA form ensures that we will never need a distinction between prefix and postfix increment/decrement.
		// That's why there's only one of each. We will, however, prefer the prefix version because postfix ++ and --
		// are the only postfix unary operators.
		Increment = Min, Decrement,
		Dereference,
		LogicalNegate,
		Max
	};
	
	UnaryOperatorType type;
	NOT_NULL(Expression) operand;
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == UnaryOperator;
	}
	
	inline UnaryOperatorExpression(UnaryOperatorType type, Expression* operand)
	: type(type), operand(operand)
	{
	}
	
	virtual inline ExpressionType getType() const override { return UnaryOperator; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
};

struct NAryOperatorExpression : public Expression
{
	enum NAryOperatorType : unsigned
	{
		Min = UnaryOperatorExpression::Max,
		
		Multiply = Min, Divide, Modulus,
		Add, Subtract,
		ShiftLeft, ShiftRight,
		SmallerThan, SmallerOrEqualTo, GreaterThan, GreaterOrEqualTo,
		Equal, NotEqual,
		BitwiseAnd,
		BitwiseXor,
		BitwiseOr,
		ShortCircuitAnd,
		ShortCircuitOr,
		Max
	};
	
	NAryOperatorType type;
	PooledDeque<NOT_NULL(Expression)> operands;
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == NAryOperator;
	}
	
	inline NAryOperatorExpression(DumbAllocator& pool, NAryOperatorType type)
	: type(type), operands(pool)
	{
	}
	
	template<typename... TExpressionType>
	inline NAryOperatorExpression(DumbAllocator& pool, NAryOperatorType type, TExpressionType... expressions)
	: NAryOperatorExpression(pool, type)
	{
		addOperand(expressions...);
	}
	
	template<typename TIter>
	void addOperands(TIter begin, TIter end)
	{
		for (auto iter = begin; iter != end; ++iter)
		{
			addOperand(*iter);
		}
	}
	
	template<typename... TExpressionType>
	void addOperand(Expression* expression, TExpressionType... expressions)
	{
		addOperand(expression);
		addOperand(expressions...);
	}
	
	void addOperand(Expression* expression);
	
	virtual inline ExpressionType getType() const override { return NAryOperator; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
	
private:
	void print(llvm::raw_ostream& os, Expression* expression) const;
};

struct TernaryExpression : public Expression
{
	NOT_NULL(Expression) condition;
	NOT_NULL(Expression) ifTrue;
	NOT_NULL(Expression) ifFalse;
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == Ternary;
	}
	
	inline TernaryExpression(Expression* condition, Expression* ifTrue, Expression* ifFalse)
	: condition(condition), ifTrue(ifTrue), ifFalse(ifFalse)
	{
	}
	
	virtual inline ExpressionType getType() const override { return Ternary; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
};

struct NumericExpression : public Expression
{
	union
	{
		int64_t si64;
		uint64_t ui64;
	};
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == Numeric;
	}
	
	inline NumericExpression(uint64_t ui)
	: ui64(ui)
	{
	}
	
	inline NumericExpression(int64_t si)
	: si64(si)
	{
	}
	
	virtual inline ExpressionType getType() const override { return Numeric; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
};

struct TokenExpression : public Expression
{
	static TokenExpression* trueExpression;
	static TokenExpression* falseExpression;
	static TokenExpression* undefExpression;
	
	NOT_NULL(const char) token;
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == Token;
	}
	
	inline TokenExpression(const char* token)
	: token(token)
	{
	}
	
	inline TokenExpression(DumbAllocator& pool, const std::string& token)
	: TokenExpression(pool.copy(token.c_str(), token.length() + 1))
	{
	}
	
	virtual inline ExpressionType getType() const override { return Token; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
};

struct CallExpression : public Expression
{
	NOT_NULL(Expression) callee;
	PooledDeque<NOT_NULL(Expression)> parameters;
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == Call;
	}
	
	inline explicit CallExpression(DumbAllocator& pool, Expression* callee)
	: callee(callee), parameters(pool)
	{
	}
	
	virtual inline ExpressionType getType() const override { return Call; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
};

struct CastExpression : public Expression
{
	NOT_NULL(TokenExpression) type;
	NOT_NULL(Expression) casted;
	
	static inline bool classof(const Expression* node)
	{
		return node->getType() == Cast;
	}
	
	inline explicit CastExpression(TokenExpression* type, Expression* value)
	: type(type), casted(value)
	{
	}
	
	virtual inline ExpressionType getType() const override { return Cast; }
	
	virtual void visit(ExpressionVisitor& visitor) override;
	virtual bool isReferenceEqual(const Expression* that) const override;
};

#pragma mark - Statements
class StatementVisitor;

struct Statement
{
	enum StatementType
	{
		Sequence, IfElse, Loop, Expr, Keyword, Declaration, Assignment
	};
	
	void printShort(llvm::raw_ostream& os) const;
	void print(llvm::raw_ostream& os) const;
	void dump() const;
	
	virtual StatementType getType() const = 0;
	virtual void visit(StatementVisitor& visitor) = 0;
};

struct ExpressionNode : public Statement
{
	NOT_NULL(Expression) expression;
	
	static inline bool classof(const Statement* node)
	{
		return node->getType() == Expr;
	}
	
	inline ExpressionNode(Expression* expr)
	: expression(expr)
	{
	}
	
	virtual inline StatementType getType() const override { return Expr; }
	virtual void visit(StatementVisitor& visitor) override;
};

struct SequenceNode : public Statement
{
	PooledDeque<NOT_NULL(Statement)> statements;
	
	static inline bool classof(const Statement* node)
	{
		return node->getType() == Sequence;
	}
	
	inline SequenceNode(DumbAllocator& pool)
	: statements(pool)
	{
	}
	
	virtual inline StatementType getType() const override { return Sequence; }
	virtual void visit(StatementVisitor& visitor) override;
};

struct IfElseNode : public Statement
{
	ExpressionNode conditionExpression;
	NOT_NULL(Expression)& condition;
	NOT_NULL(Statement) ifBody;
	Statement* elseBody;
	
	static inline bool classof(const Statement* node)
	{
		return node->getType() == IfElse;
	}
	
	inline IfElseNode(Expression* condition, Statement* ifBody, Statement* elseBody = nullptr)
	: conditionExpression(condition), condition(conditionExpression.expression), ifBody(ifBody), elseBody(elseBody)
	{
	}
	
	virtual inline StatementType getType() const override { return IfElse; }
	virtual void visit(StatementVisitor& visitor) override;
};

struct LoopNode : public Statement
{
	enum ConditionPosition {
		PreTested, // while
		PostTested, // do ... while
	};
	
	ExpressionNode conditionExpression;
	NOT_NULL(Expression)& condition;
	ConditionPosition position;
	NOT_NULL(Statement) loopBody;
	
	static inline bool classof(const Statement* node)
	{
		return node->getType() == Loop;
	}
	
	LoopNode(Statement* body); // creates a `while (true)`
	
	inline LoopNode(Expression* condition, ConditionPosition position, Statement* body)
	: conditionExpression(condition), condition(conditionExpression.expression), position(position), loopBody(body)
	{
	}
	
	inline bool isEndless() const;
	
	virtual inline StatementType getType() const override { return Loop; }
	virtual void visit(StatementVisitor& visitor) override;
};

struct KeywordNode : public Statement
{
	static inline bool classof(const Statement* node)
	{
		return node->getType() == Keyword;
	}
	
	static KeywordNode* breakNode;
	
	NOT_NULL(const char) name;
	Expression* operand;
	
	inline KeywordNode(const char* name, Expression* operand = nullptr)
	: name(name), operand(operand)
	{
	}
	
	virtual inline StatementType getType() const override { return Keyword; }
	virtual void visit(StatementVisitor& visitor) override;
};

struct DeclarationNode : public Statement
{
	NOT_NULL(TokenExpression) type;
	NOT_NULL(TokenExpression) name;
	const char* comment;
	size_t orderHint; // This field helps order declarations when they must be printed.
	
	static inline bool classof(const Statement* node)
	{
		return node->getType() == Declaration;
	}
	
	inline DeclarationNode(TokenExpression* type, TokenExpression* name, const char* comment = nullptr)
	: type(type), name(name), comment(comment), orderHint(0)
	{
	}
	
	virtual inline StatementType getType() const override { return Declaration; }
	virtual void visit(StatementVisitor& visitor) override;
};

struct AssignmentNode : public Statement
{
	NOT_NULL(Expression) left;
	NOT_NULL(Expression) right;
	
	static inline bool classof(const Statement* node)
	{
		return node->getType() == Assignment;
	}
	
	inline AssignmentNode(Expression* left, Expression* right)
	: left(left), right(right)
	{
	}
	
	virtual inline StatementType getType() const override { return Assignment; }
	virtual void visit(StatementVisitor& visitor) override;
};

bool LoopNode::isEndless() const
{
	return condition == TokenExpression::trueExpression;
}

#endif /* ast_nodes_cpp */
