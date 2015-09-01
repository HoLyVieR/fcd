//
//  pass_tie.hpp
//  x86Emulator
//
//  Created by Félix on 2015-07-29.
//  Copyright © 2015 Félix Cloutier. All rights reserved.
//

#ifndef pass_tie_cpp
#define pass_tie_cpp

#include "dumb_allocator.h"
#include "llvm_warnings.h"
#include "pass_targetinfo.h"
#include "tie_types.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/Analysis/CallGraphSCCPass.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/MemorySSA.h>
SILENCE_LLVM_WARNINGS_END()

#include <cassert>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace tie
{
	struct TypeOrValue
	{
		llvm::Value* value;
		const tie::Type* type;
		
		TypeOrValue(llvm::Value* value) : value(value), type(nullptr)
		{
		}
		
		TypeOrValue(const tie::Type* type) : value(nullptr), type(type)
		{
		}
		
		void print(llvm::raw_ostream& os) const;
		void dump() const;
	};
	
	struct Constraint
	{
		enum Type : char
		{
			Specializes = ':', // adds information ("inherits from", larger bit count)
			Generalizes = '!', // takes away information (smaller bit count)
			IsEqual = '=',
			
			Conjunction = '&',
			Disjunction = '|',
		};
		
		Type type;
		
		Constraint(Type type)
		: type(type)
		{
		}
		
		virtual void print(llvm::raw_ostream& os) const = 0;
		void dump();
	};
	
	template<Constraint::Type ConstraintType>
	struct CombinatorConstraint : public Constraint
	{
		static bool classof(const Constraint* that)
		{
			return that->type == ConstraintType;
		}
		
		DumbAllocator& pool;
		PooledDeque<Constraint*> constraints;
		
		CombinatorConstraint(DumbAllocator& pool)
		: Constraint(ConstraintType), pool(pool), constraints(pool)
		{
		}
		
		template<typename Constraint, typename... TArgs>
		Constraint* constrain(TArgs&&... args)
		{
			auto constraint = pool.allocate<Constraint>(args...);
			constraints.push_back(constraint);
			return constraint;
		}
		
		virtual void print(llvm::raw_ostream& os) const override
		{
			os << '(';
			auto iter = constraints.begin();
			if (iter != constraints.end())
			{
				os << '(';
				(*iter)->print(os);
				for (++iter; iter != constraints.end(); ++iter)
				{
					os << ") " << (char)ConstraintType << " (";
					(*iter)->print(os);
				}
				os << ')';
			}
			os << ')';
		}
	};
	
	using ConjunctionConstraint = CombinatorConstraint<Constraint::Conjunction>;
	using DisjunctionConstraint = CombinatorConstraint<Constraint::Disjunction>;
	
	template<Constraint::Type ConstraintType>
	struct BinaryConstraint : public Constraint
	{
		static bool classof(const Constraint* that)
		{
			return that->type == ConstraintType;
		}
		
		llvm::Value* left;
		TypeOrValue right;
		
		BinaryConstraint(llvm::Value* left, TypeOrValue right)
		: Constraint(ConstraintType), left(left), right(right)
		{
		}
		
		virtual void print(llvm::raw_ostream& os) const override
		{
			os << "value<";
			left->printAsOperand(os);
			os << "> " << (char)ConstraintType << ' ';
			right.print(os);
		}
	};
	
	using SpecializesConstraint = BinaryConstraint<Constraint::Specializes>;
	using GeneralizesConstraint = BinaryConstraint<Constraint::Generalizes>;
	using IsEqualConstraint = BinaryConstraint<Constraint::IsEqual>;
	
	class InferenceContext : public llvm::InstVisitor<InferenceContext>
	{
		typedef std::unordered_multimap<llvm::Value*, Constraint*> ValueToConstraintMap;
		const TargetInfo& target;
		llvm::MemorySSA& mssa;
		DumbAllocator pool;
		std::unordered_set<llvm::Value*> visited;
		ValueToConstraintMap constraints;
		
		template<typename Constraint, typename... TArgs>
		Constraint* constrain(llvm::Value* value, TArgs&&... args)
		{
			auto constraint = pool.allocate<Constraint>(value, args...);
			addConstraintToValues(constraint, value, args...);
			return constraint;
		}
		
		template<typename... TArgs>
		void addConstraintToValues(Constraint* c, TypeOrValue tv, TArgs&&... values)
		{
			addConstraintToValues(c, tv);
			addConstraintToValues(c, values...);
		}
		
		void addConstraintToValues(Constraint* c, TypeOrValue tv)
		{
			if (auto value = tv.value)
			{
				constraints.insert({value, c});
			}
		}
		
		void print(llvm::raw_ostream& os, ValueToConstraintMap::const_iterator begin, ValueToConstraintMap::const_iterator end) const;
		
	public:
		InferenceContext(const TargetInfo& target, llvm::MemorySSA& ssa);
		
		void dump() const;
		void dump(llvm::Value* key) const;
		
		static const tie::Type& getAny();
		static const tie::Type& getBoolean();
		const tie::Type& getNum(unsigned width = 0);
		const tie::Type& getSint(unsigned width = 0);
		const tie::Type& getUint(unsigned width = 0);
		const tie::Type& getFunctionPointer();
		const tie::Type& getBasicBlockPointer();
		const tie::Type& getPointer();
		const tie::Type& getPointerTo(const tie::Type& pointee);
		
		void visitICmpInst(llvm::ICmpInst& inst, llvm::Value* constraintKey = nullptr);
		void visitAllocaInst(llvm::AllocaInst& inst, llvm::Value* constraintKey = nullptr);
		void visitLoadInst(llvm::LoadInst& inst, llvm::Value* constraintKey = nullptr);
		void visitStoreInst(llvm::StoreInst& inst, llvm::Value* constraintKey = nullptr);
		void visitGetElementPtrInst(llvm::GetElementPtrInst& inst, llvm::Value* constraintKey = nullptr);
		void visitPHINode(llvm::PHINode& inst, llvm::Value* constraintKey = nullptr);
		void visitSelectInst(llvm::SelectInst& inst, llvm::Value* constraintKey = nullptr);
		void visitCallInst(llvm::CallInst& inst, llvm::Value* constraintKey = nullptr);
		
		void visitBinaryOperator(llvm::BinaryOperator& inst, llvm::Value* constraintKey = nullptr);
		void visitCastInst(llvm::CastInst& inst, llvm::Value* constraintKey = nullptr);
		void visitTerminatorInst(llvm::TerminatorInst& inst, llvm::Value* constraintKey = nullptr);
		void visitInstruction(llvm::Instruction& inst, llvm::Value* constraintKey = nullptr);
		
		void visitConstant(llvm::Constant& constant);
		void visit(llvm::Instruction& inst, llvm::Value* constraintKey);
		void visit(llvm::Instruction& inst);
		using llvm::InstVisitor<InferenceContext>::visit;
	};
}

class TypeInference : public llvm::CallGraphSCCPass
{
public:
	static char ID;
	
	TypeInference();
	
	virtual const char* getPassName() const override;
	virtual void getAnalysisUsage(llvm::AnalysisUsage& au) const override;
	virtual bool runOnSCC(llvm::CallGraphSCC& scc) override;
};

namespace llvm
{
	void initializeTypeInferencePass(PassRegistry& pr);
}

#endif /* pass_tie_cpp */
