//
// elf_executable.h
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is part of fcd. fcd as a whole is licensed under the terms
// of the GNU GPLv3 license, but specific parts (such as this one) are
// dual-licensed under the terms of a BSD-like license as well. You
// may use, modify and distribute this part of fcd under the terms of
// either license, at your choice. See the LICENSE file in this directory
// for details.
//

#ifndef ElfExecutableParser_h
#define ElfExecutableParser_h

#include "executable.h"

#include <memory>

// Entry point.
std::unique_ptr<Executable> parseElfExecutable(const uint8_t* begin, const uint8_t* end);

#endif /* ElfExecutableParser_h */
