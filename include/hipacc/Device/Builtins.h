//
// Copyright (c) 2012, University of Erlangen-Nuremberg
// Copyright (c) 2012, Siemens AG
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met: 
// 
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer. 
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

//===--- Builtins.h - Builtin function header -------------------*- C++ -*-===//
//
// This file defines enum values for all the target-independent builtin
// functions.
//
//===----------------------------------------------------------------------===//

#ifndef _BUILTINS_H_
#define _BUILTINS_H_

#include <clang/AST/ASTContext.h>
#include <clang/Basic/Builtins.h>
#include <clang/Basic/TargetInfo.h>
#include <llvm/Support/raw_ostream.h>

namespace clang {
namespace hipacc {
enum TargetID {
  C_TARGET      = 0x1,  // builtin for C source.
  CUDA_TARGET   = 0x2,  // builtin for CUDA.
  OPENCL_TARGET = 0x4,  // builtin for OpenCL.
  ALL_TARGETS   = (C_TARGET|CUDA_TARGET|OPENCL_TARGET) //builtin is for all targets.
};

namespace Builtin {
enum ID {
  FirstBuiltin = clang::Builtin::FirstTSBuiltin-1,
  #define HIPACCBUILTIN(NAME, TYPE, CUDAID, OPENCLID) HIPACCBI##NAME,
  #define CUDABUILTIN(NAME, TYPE, CUDANAME) CUDABI##CUDANAME,
  #define OPENCLBUILTIN(NAME, TYPE, OPENCLNAME) OPENCLBI##OPENCLNAME,
  #include "hipacc/Device/Builtins.def"
  LastBuiltin
};

struct Info {
  const char *Name, *Type;
  TargetID builtin_target;
  ID CUDA, OpenCL;
  FunctionDecl *FD;

  bool operator==(const Info &RHS) const {
    return !strcmp(Name, RHS.Name) && !strcmp(Type, RHS.Type);
  }
  bool operator!=(const Info &RHS) const { return !(*this == RHS); }
};

class Context {
  private:
    ASTContext &Ctx;
    bool initialized;
    const Info &getRecord(unsigned int ID) const;

  public:
    Context(ASTContext &Ctx) :
      Ctx(Ctx),
      initialized(false)
    {}

    QualType getBuiltinType(unsigned int Id) const;
    QualType getBuiltinType(const char *TypeStr) const;
    std::string EncodeTypeIntoStr(QualType QT, const ASTContext &Ctx);

    void InitializeBuiltins();
    FunctionDecl *CreateBuiltin(unsigned int bid);
    FunctionDecl *CreateBuiltin(QualType R, const char *Name);

    void getBuiltinNames(TargetID target,
        llvm::SmallVectorImpl<const char *> &Names);

    FunctionDecl *getBuiltinFunction(unsigned int ID) const {
      return getRecord(ID-FirstBuiltin).FD;
    }

    FunctionDecl *getBuiltinFunction(llvm::StringRef Name, QualType QT,
        TargetID target) const;

    const char *getName(unsigned int ID) const {
      return getRecord(ID).Name;
    }

    const char *getTypeString(unsigned int ID) const {
      return getRecord(ID).Type;
    }
};
}
} // end namespace hipacc
} // end namespace clang

#endif  // _BUILTINS_H_

// vim: set ts=2 sw=2 sts=2 et ai:
