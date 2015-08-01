//
//  pass_targetinfo.cpp
//  x86Emulator
//
//  Created by Félix on 2015-06-12.
//  Copyright © 2015 Félix Cloutier. All rights reserved.
//

#include "pass_targetinfo.h"

SILENCE_LLVM_WARNINGS_BEGIN()
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
SILENCE_LLVM_WARNINGS_END()

#include <iostream>

using namespace llvm;
using namespace std;

char TargetInfo::ID = 0;

bool TargetInfo::doInitialization(llvm::Module &m)
{
	dl = &m.getDataLayout();
	return ImmutablePass::doInitialization(m);
}

unsigned TargetInfo::getPointerWidth() const
{
	return dl->getPointerSizeInBits();
}

GetElementPtrInst* TargetInfo::getRegister(llvm::Value *registerStruct, const char *name) const
{
	name = largestOverlappingRegister(name);
	
	const TargetRegisterInfo* selected = nullptr;
	for (const auto& info : targetRegisterInfo())
	{
		if (info.name.c_str() == name)
		{
			selected = &info;
			break;
		}
	}
	
	if (selected == nullptr)
	{
		return nullptr;
	}
	
	SmallVector<Value*, 4> indices;
	LLVMContext& ctx = registerStruct->getContext();
	IntegerType* int32 = Type::getInt32Ty(ctx);
	IntegerType* int64 = Type::getInt64Ty(ctx);
	CompositeType* currentType = cast<CompositeType>(registerStruct->getType());
	for (unsigned offset : selected->gepOffsets)
	{
		IntegerType* constantType = isa<StructType>(currentType) ? int32 : int64;
		indices.push_back(ConstantInt::get(constantType, offset));
		currentType = dyn_cast<CompositeType>(currentType->getTypeAtIndex(offset));
	}
	return GetElementPtrInst::CreateInBounds(registerStruct, indices);
}

const char* TargetInfo::registerName(const Value& value) const
{
	if (auto castInst = dyn_cast<CastInst>(&value))
	{
		return registerName(*castInst->getOperand(0));
	}
	if (auto gep = dyn_cast<GetElementPtrInst>(&value))
	{
		return registerName(*gep);
	}
	return nullptr;
}

const char* TargetInfo::registerName(const GetElementPtrInst &gep) const
{
	// Not reading from a register unless the GEP is from the function's first parameter.
	// This needs to check that the pointer operand of the GEP is an argument of the function that declares it.
	// This is different from checking if the pointer operand of the GEP is an argument of the function that declares
	// the GEP, because consistency between functions is not maintained *during* argument recovery.
	if (const Argument* arg = dyn_cast<Argument>(gep.getPointerOperand()))
	{
		if (arg == arg->getParent()->arg_begin())
		{
			APInt offset(64, 0, false);
			if (gep.accumulateConstantOffset(*dl, offset))
			{
				auto resultType = gep.getResultElementType();
				size_t size = dl->getTypeStoreSize(resultType);
				return registerName(offset.getLimitedValue(), size);
			}
		}
	}
	return nullptr;
}

const char* TargetInfo::registerName(size_t offset, size_t size) const
{
	assert(targetRegisterInfo().size() > 0);
	// FIXME - do something better than a linear search
	// (especially since targetRegisterInfo() is sorted)
	for (const auto& info : targetRegisterInfo())
	{
		if (info.offset == offset && info.size == size)
		{
			return info.name.c_str();
		}
		
		if (info.offset > offset)
		{
			break;
		}
	}
	return nullptr;
}

const char* TargetInfo::largestOverlappingRegister(const char *overlapped) const
{
	if (overlapped == nullptr)
	{
		return nullptr;
	}
	
	auto iter = targetRegisterInfo().begin();
	auto end = targetRegisterInfo().end();
	while (iter != end)
	{
		const auto& currentTarget = *iter;
		while (iter->offset < currentTarget.offset + currentTarget.size)
		{
			if (iter->name.c_str() == overlapped)
			{
				return currentTarget.name.c_str();
			}
			iter++;
		}
	}
	return nullptr;
}

const char* TargetInfo::keyName(const char *name) const
{
	if (name == nullptr)
	{
		return nullptr;
	}
	
	for (const auto& info : targetRegisterInfo())
	{
		if (info.name == name)
		{
			return info.name.c_str();
		}
	}
	return nullptr;
}

TargetInfo* createTargetInfoPass()
{
	return new TargetInfo;
}

INITIALIZE_PASS(TargetInfo, "tginf", "Decompiler Target Info", false, true)
