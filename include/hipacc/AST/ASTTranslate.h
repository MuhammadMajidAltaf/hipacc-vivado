//
// Copyright (c) 2012, University of Erlangen-Nuremberg
// Copyright (c) 2012, Siemens AG
// Copyright (c) 2010, ARM Limited
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

//===--- ASTTranslate.h - C to CL Translation of the AST ------------------===//
//
// This file implements translation of statements and expressions.
//
//===----------------------------------------------------------------------===//

#ifndef _ASTTRANSLATE_H_
#define _ASTTRANSLATE_H_

#include <clang/AST/Attr.h>
#include <clang/AST/Type.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Sema/Ownership.h>
#include <llvm/ADT/SmallVector.h>

#include "hipacc/Analysis/KernelStatistics.h"
#include "hipacc/AST/ASTNode.h"
#include "hipacc/Config/CompilerOptions.h"
#include "hipacc/Device/Builtins.h"
#include "hipacc/DSL/ClassRepresentation.h"
#include "hipacc/Vectorization/SIMDTypes.h"


//===----------------------------------------------------------------------===//
// Statement/expression transformations
//===----------------------------------------------------------------------===//

namespace clang {
namespace hipacc {
typedef union border_variant {
  struct {
    unsigned int left   : 1;
    unsigned int right  : 1;
    unsigned int top    : 1;
    unsigned int bottom : 1;
  } borders;
  unsigned int borderVal;
  border_variant() : borderVal(0) {}
} border_variant;

class ASTTranslate : public StmtVisitor<ASTTranslate, Stmt *> {
  private:
    enum TranslationMode {
      CloneAST,
      TranslateAST
    };
    ASTContext &Ctx;
    DiagnosticsEngine &Diags;
    FunctionDecl *kernelDecl;
    HipaccKernel *Kernel;
    HipaccKernelClass *KernelClass;
    hipacc::Builtin::Context &builtins;
    CompilerOptions &compilerOptions;
    TranslationMode astMode;
    SIMDTypes simdTypes;
    border_variant bh_variant;
    bool emitEstimation;

    // "global variables"
    unsigned int literalCount;
    SmallVector<FunctionDecl *, 16> cloneFuns;
    SmallVector<Stmt *, 16> preStmts, postStmts;
    SmallVector<CompoundStmt *, 16> preCStmt, postCStmt;
    CompoundStmt *curCStmt;
    HipaccMask *convMask;
    HipaccMask *vivadoWindow;
    DeclRefExpr *convTmp;
    ConvolutionMode convMode;
    int convIdxX, convIdxY;
    enum ConvolveMethod {
      Convolve,
      Reduce,
      Iterate
    };

    SmallVector<HipaccMask *, 4> redDomains;
    SmallVector<DeclRefExpr *, 4> redTmps;
    SmallVector<ConvolutionMode, 4> redModes;
    SmallVector<int, 4> redIdxX, redIdxY;

    DeclRefExpr *bh_start_left, *bh_start_right, *bh_start_top,
                *bh_start_bottom, *bh_fall_back;
    DeclRefExpr *outputImage;
    DeclRefExpr *retValRef;
    Expr *writeImageRHS;
    NamespaceDecl *hipaccNS, *hipaccMathNS;
    TypedefDecl *samplerTy;
    DeclRefExpr *kernelSamplerRef;

    class BlockingVars {
      public:
        Expr *global_id_x, *global_id_y;
        Expr *local_id_x, *local_id_y;
        Expr *local_size_x, *local_size_y;
        Expr *block_id_x, *block_id_y;
        //Expr *block_size_x, *block_size_y;
        //Expr *grid_size_x, *grid_size_y;

        BlockingVars() :
          global_id_x(nullptr), global_id_y(nullptr), local_id_x(nullptr),
          local_id_y(nullptr), local_size_x(nullptr), local_size_y(nullptr),
          block_id_x(nullptr), block_id_y(nullptr) {}
    };
    BlockingVars tileVars;
    // updated index for PPT (iteration space unrolling)
    Expr *lidYRef, *gidYRef;


    template<class T> T *Clone(T *S) {
      if (S==nullptr) return nullptr;

      return static_cast<T *>(Visit(S));
    }
    template<class T> T *CloneDecl(T *D) {
      if (D==nullptr) return nullptr;

      switch (D->getKind()) {
        default:
          assert(0 && "Only VarDecls, ParmVArDecls, and FunctionDecls supported!");
        case Decl::ParmVar:
          if (astMode==CloneAST) return D;
          return dyn_cast<T>(CloneParmVarDecl(dyn_cast<ParmVarDecl>(D)));
        case Decl::Var:
          if (astMode==CloneAST) return D;
          return dyn_cast<T>(CloneVarDecl(dyn_cast<VarDecl>(D)));
        case Decl::Function:
          return dyn_cast<T>(cloneFunction(dyn_cast<FunctionDecl>(D)));
      }
    }

    VarDecl *CloneVarDecl(VarDecl *VD);
    VarDecl *CloneParmVarDecl(ParmVarDecl *PVD);
    VarDecl *CloneDeclTex(ParmVarDecl *D, std::string prefix);
    void setExprProps(Expr *orig, Expr *clone);
    void setExprPropsClone(Expr *orig, Expr *clone);
    void setCastPath(CastExpr *orig, CXXCastPath &castPath);
    void initCPU(SmallVector<Stmt *, 16> &kernelBody, Stmt *S);
    void initCUDA(SmallVector<Stmt *, 16> &kernelBody);
    void initOpenCL(SmallVector<Stmt *, 16> &kernelBody);
    void initRenderscript(SmallVector<Stmt *, 16> &kernelBody);
    void updateTileVars();
    Expr *addCastToInt(Expr *E);
    Expr *stripLiteralOperand(Expr *operand1, Expr *operand2, int val);
    Expr *stripLiteralOperand(Expr *operand1, Expr *operand2, double val);
    FunctionDecl *cloneFunction(FunctionDecl *FD);
    template <typename T>
    T *lookup(std::string name, QualType QT, NamespaceDecl *NS=nullptr);
    // wrappers to mark variables as being used
    DeclRefExpr *getWidthDecl(HipaccAccessor *Acc) {
      Kernel->setUsed(Acc->getWidthDecl()->getNameInfo().getAsString());
      return Acc->getWidthDecl();
    }
    DeclRefExpr *getHeightDecl(HipaccAccessor *Acc) {
      Kernel->setUsed(Acc->getHeightDecl()->getNameInfo().getAsString());
      return Acc->getHeightDecl();
    }
    DeclRefExpr *getStrideDecl(HipaccAccessor *Acc) {
      Kernel->setUsed(Acc->getStrideDecl()->getNameInfo().getAsString());
      return Acc->getStrideDecl();
    }
    DeclRefExpr *getOffsetXDecl(HipaccAccessor *Acc) {
      Kernel->setUsed(Acc->getOffsetXDecl()->getNameInfo().getAsString());
      return Acc->getOffsetXDecl();
    }
    DeclRefExpr *getOffsetYDecl(HipaccAccessor *Acc) {
      Kernel->setUsed(Acc->getOffsetYDecl()->getNameInfo().getAsString());
      return Acc->getOffsetYDecl();
    }
    DeclRefExpr *getBHStartLeft() {
      Kernel->setUsed(bh_start_left->getNameInfo().getAsString());
      return bh_start_left;
    }
    DeclRefExpr *getBHStartRight() {
      Kernel->setUsed(bh_start_right->getNameInfo().getAsString());
      return bh_start_right;
    }
    DeclRefExpr *getBHStartTop() {
      Kernel->setUsed(bh_start_top->getNameInfo().getAsString());
      return bh_start_top;
    }
    DeclRefExpr *getBHStartBottom() {
      Kernel->setUsed(bh_start_bottom->getNameInfo().getAsString());
      return bh_start_bottom;
    }
    DeclRefExpr *getBHFallBack() {
      Kernel->setUsed(bh_fall_back->getNameInfo().getAsString());
      return bh_fall_back;
    }

    // KernelDeclMap - this keeps track of the cloned Decls which are used in
    // expressions, e.g. DeclRefExpr
    typedef llvm::DenseMap<VarDecl *, VarDecl *> DeclMapTy;
    typedef llvm::DenseMap<ParmVarDecl *, VarDecl *> PVDeclMapTy;
    typedef llvm::DenseMap<ParmVarDecl *, HipaccAccessor *> AccMapTy;
    typedef llvm::DenseMap<FunctionDecl *, FunctionDecl *> FunMapTy;
    DeclMapTy KernelDeclMap;
    DeclMapTy LambdaDeclMap;
    PVDeclMapTy KernelDeclMapTex;
    PVDeclMapTy KernelDeclMapShared;
    PVDeclMapTy KernelDeclMapVector;
    AccMapTy KernelDeclMapAcc;
    FunMapTy KernelFunctionMap;

    // BorderHandling.cpp
    Expr *addBorderHandling(DeclRefExpr *LHS, Expr *local_offset_x, Expr
        *local_offset_y, HipaccAccessor *Acc);
    Expr *addBorderHandling(DeclRefExpr *LHS, Expr *local_offset_x, Expr
        *local_offset_y, HipaccAccessor *Acc, SmallVector<Stmt *, 16> &bhStmts,
        SmallVector<CompoundStmt *, 16> &bhCStmt);
    Stmt *addClampUpper(HipaccAccessor *Acc, Expr *idx, Expr *upper, bool);
    Stmt *addClampLower(HipaccAccessor *Acc, Expr *idx, Expr *lower, bool);
    Stmt *addRepeatUpper(HipaccAccessor *Acc, Expr *idx, Expr *upper, bool);
    Stmt *addRepeatLower(HipaccAccessor *Acc, Expr *idx, Expr *lower, bool);
    Stmt *addMirrorUpper(HipaccAccessor *Acc, Expr *idx, Expr *upper, bool);
    Stmt *addMirrorLower(HipaccAccessor *Acc, Expr *idx, Expr *lower, bool);
    Expr *addConstantUpper(HipaccAccessor *Acc, Expr *idx, Expr *upper, Expr
        *cond);
    Expr *addConstantLower(HipaccAccessor *Acc, Expr *idx, Expr *lower, Expr
        *cond);

    // Convolution.cpp
    Stmt *getConvolutionStmt(ConvolutionMode mode, DeclRefExpr *tmp_var, Expr
        *ret_val);
    Expr *getInitExpr(ConvolutionMode mode, QualType QT);
    Stmt *addDomainCheck(HipaccMask *Domain, DeclRefExpr *domain_var, Stmt
        *stmt);
    Expr *convertConvolution(CXXMemberCallExpr *E);

    // Interpolation.cpp
    Expr *addNNInterpolationX(HipaccAccessor *Acc, Expr *idx_x);
    Expr *addNNInterpolationY(HipaccAccessor *Acc, Expr *idx_y);
    FunctionDecl *getInterpolationFunction(HipaccAccessor *Acc);
    FunctionDecl *getTextureFunction(HipaccAccessor *Acc, MemoryAccess memAcc);
    FunctionDecl *getImageFunction(HipaccAccessor *Acc, MemoryAccess memAcc);
    FunctionDecl *getAllocationFunction(const BuiltinType *BT, bool isVecType,
                                        MemoryAccess memAcc);
    FunctionDecl *getConvertFunction(QualType QT, bool isVecType);
    FunctionDecl *getVivadoReturnConvertFunction(std::string name);
    FunctionDecl *getWindowFunction(MemoryAccess memAcc);
    Expr *addInterpolationCall(DeclRefExpr *LHS, HipaccAccessor *Acc, Expr
        *idx_x, Expr *idx_y);

    // MemoryAccess.cpp
    Expr *addLocalOffset(Expr *idx, Expr *local_offset);
    Expr *addGlobalOffsetX(Expr *idx_x, HipaccAccessor *Acc);
    Expr *addGlobalOffsetY(Expr *idx_y, HipaccAccessor *Acc);
    Expr *removeISOffsetX(Expr *idx_x, HipaccAccessor *Acc);
    Expr *removeISOffsetY(Expr *idx_y, HipaccAccessor *Acc);
    Expr *accessMem(DeclRefExpr *LHS, HipaccAccessor *Acc, MemoryAccess memAcc,
        Expr *offset_x=nullptr, Expr *offset_y=nullptr);
    Expr *accessMem2DAt(DeclRefExpr *LHS, Expr *idx_x, Expr *idx_y);
    Expr *accessMemArrAt(DeclRefExpr *LHS, Expr *stride, Expr *idx_x, Expr
        *idx_y);
    Expr *accessMemAllocAt(DeclRefExpr *LHS, MemoryAccess memAcc,
                           Expr *idx_x, Expr *idx_y);
    Expr *accessMemAllocPtr(DeclRefExpr *LHS);
    Expr *accessMemTexAt(DeclRefExpr *LHS, HipaccAccessor *Acc, MemoryAccess
        memAcc, Expr *idx_x, Expr *idx_y);
    Expr *accessMemImgAt(DeclRefExpr *LHS, HipaccAccessor *Acc, MemoryAccess
        memAcc, Expr *idx_x, Expr *idx_y);
    Expr *accessMemShared(DeclRefExpr *LHS, Expr *offset_x=nullptr, Expr
        *offset_y=nullptr);
    Expr *accessMemSharedAt(DeclRefExpr *LHS, Expr *idx_x, Expr *idx_y);
    Expr *accessMemStream(DeclRefExpr *LHS);
    Expr *accessMemWindowAt(DeclRefExpr *LHS, MemoryAccess memAcc,
                            Expr *idx_x, Expr *idx_y);
    void stageLineToSharedMemory(ParmVarDecl *PVD, SmallVector<Stmt *, 16>
        &stageBody, Expr *local_offset_x, Expr *local_offset_y, Expr
        *global_offset_x, Expr *global_offset_y);
    void stageIterationToSharedMemory(SmallVector<Stmt *, 16> &stageBody, int
        p);
    void stageIterationToSharedMemoryExploration(SmallVector<Stmt *, 16>
        &stageBody);

    // default error message for unsupported expressions and statements.
    #define HIPACC_NOT_SUPPORTED(MSG) \
    assert(0 && "Hipacc: Stumbled upon unsupported expression or statement: " #MSG)
    #define HIPACC_BASE_CLASS(MSG) \
    assert(0 && "Hipacc: Stumbled upon base class, implementation of any derived class missing? Base class was: " #MSG)

  public:
    ASTTranslate(ASTContext& Ctx, FunctionDecl *kernelDecl, HipaccKernel
        *kernel, HipaccKernelClass *kernelClass, hipacc::Builtin::Context
        &builtins, CompilerOptions &options, bool emitEstimation=false) :
      Ctx(Ctx),
      Diags(Ctx.getDiagnostics()),
      kernelDecl(kernelDecl),
      Kernel(kernel),
      KernelClass(kernelClass),
      builtins(builtins),
      compilerOptions(options),
      astMode(TranslateAST),
      simdTypes(SIMDTypes(Ctx, builtins, options)),
      bh_variant(),
      emitEstimation(emitEstimation),
      literalCount(0),
      curCStmt(nullptr),
      convMask(nullptr),
      vivadoWindow(nullptr),
      convTmp(nullptr),
      convIdxX(0),
      convIdxY(0),
      bh_start_left(nullptr),
      bh_start_right(nullptr),
      bh_start_top(nullptr),
      bh_start_bottom(nullptr),
      bh_fall_back(nullptr),
      outputImage(nullptr),
      retValRef(nullptr),
      writeImageRHS(nullptr),
      tileVars(),
      lidYRef(nullptr),
      gidYRef(nullptr) {
        // get 'hipacc' namespace context for lookups
        for (DeclContext::lookup_result Lookup =
            Ctx.getTranslationUnitDecl()->lookup(&Ctx.Idents.get("hipacc"));
            !Lookup.empty(); Lookup=Lookup.slice(1)) {
          hipaccNS = cast_or_null<NamespaceDecl>(Lookup.front());
          if (hipaccNS) break;
        }
        assert(hipaccNS && "could not lookup 'hipacc' namespace");

        // get 'hipacc::' namespace context for lookups
        for (DeclContext::lookup_result Lookup =
            hipaccNS->lookup(&Ctx.Idents.get("math"));
            !Lookup.empty(); Lookup=Lookup.slice(1)) {
          hipaccMathNS = cast_or_null<NamespaceDecl>(Lookup.front());
          if (hipaccMathNS) break;
        }
        assert(hipaccMathNS && "could not lookup 'hipacc::math' namespace");

        // typedef unsigned int sampler_t;
        TypeSourceInfo *TInfosampler =
          Ctx.getTrivialTypeSourceInfo(Ctx.UnsignedIntTy);
        samplerTy = TypedefDecl::Create(Ctx, Ctx.getTranslationUnitDecl(),
            SourceLocation(), SourceLocation(), &Ctx.Idents.get("sampler_t"),
            TInfosampler);

        // sampler_t <clKernel>Sampler
        kernelSamplerRef = ASTNode::createDeclRefExpr(Ctx,
            ASTNode::createVarDecl(Ctx, Ctx.getTranslationUnitDecl(),
              kernelDecl->getNameAsString() + "Sampler",
              Ctx.getTypeDeclType(samplerTy), nullptr));

        builtins.InitializeBuiltins();
        Kernel->resetUsed();

        // debug
        //dump_available_statement_visitors();
        // debug
      }

    Stmt *Hipacc(Stmt *S);

  public:
    // dump all available statement visitors
    static void dump_available_statement_visitors() {
      llvm::errs() <<
        #define STMT(Type, Base) #Base << " *Visit"<< #Type << "(" << #Type << " *" << #Base << ");\n" <<
        #include "clang/AST/StmtNodes.inc"
        "\n\n";
    }
    // Interpolation.cpp
    // create interpolation function name
    static std::string getInterpolationName(ASTContext &Ctx,
        hipacc::Builtin::Context &builtins, CompilerOptions &compilerOptions,
        HipaccKernel *Kernel, HipaccAccessor *Acc, border_variant bh_variant);

    // the following list ist ordered according to
    // include/clang/Basic/StmtNodes.td

    // implementation of Visitors is split into two files:
    // ASTClone.cpp for cloning single AST nodes
    // ASTTranslate.cpp for translation related to CUDA/OpenCL

    #define VISIT_MODE(B, K) \
    B *Visit##K##Clone(K *k); \
    B *Visit##K##Translate(K *k); \
    B *Visit##K(K *k) { \
      if (astMode==CloneAST) return Visit##K##Clone(k); \
      return Visit##K##Translate(k); \
    }

    // Statements
    Stmt *VisitStmt(Stmt *S);
    Stmt *VisitNullStmt(NullStmt *S);
    //Stmt *VisitCompoundStmt(CompoundStmt *S);
    VISIT_MODE(Stmt, CompoundStmt)
    Stmt *VisitLabelStmt(LabelStmt *S);
    Stmt *VisitAttributedStmt(AttributedStmt *Stmt);
    Stmt *VisitIfStmt(IfStmt *S);
    Stmt *VisitSwitchStmt(SwitchStmt *S);
    Stmt *VisitWhileStmt(WhileStmt *S);
    Stmt *VisitDoStmt(DoStmt *S);
    Stmt *VisitForStmt(ForStmt *S);
    Stmt *VisitGotoStmt(GotoStmt *S);
    Stmt *VisitIndirectGotoStmt(IndirectGotoStmt *S);
    Stmt *VisitContinueStmt(ContinueStmt *S);
    Stmt *VisitBreakStmt(BreakStmt *S);
    //Stmt *VisitReturnStmt(ReturnStmt *S);
    VISIT_MODE(Stmt, ReturnStmt)
    Stmt *VisitDeclStmt(DeclStmt *S);
    Stmt *VisitSwitchCase(SwitchCase *S);
    Stmt *VisitCaseStmt(CaseStmt *S);
    Stmt *VisitDefaultStmt(DefaultStmt *S);
    Stmt *VisitCapturedStmt(CapturedStmt *S);

    // Asm Statements
    Stmt *VisitAsmStmt(AsmStmt *S) {  // abstract base class
      HIPACC_BASE_CLASS(AsmStmt);
      return nullptr;
    }
    Stmt *VisitGCCAsmStmt(GCCAsmStmt *S);
    Stmt *VisitMSAsmStmt(MSAsmStmt *S) {
      HIPACC_NOT_SUPPORTED(MSAsmStmt);
      return nullptr;
    }

    // Obj-C Statements
    Stmt *VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCAtTryStmt);
      return nullptr;
    }
    Stmt *VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCAtCatchStmt);
      return nullptr;
    }
    Stmt *VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCAtFinallyStmt);
      return nullptr;
    }
    Stmt *VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCAtThrowStmt);
      return nullptr;
    }
    Stmt *VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCAtSynchronizedStmt);
      return nullptr;
    }
    Stmt *VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCForCollectionStmt);
      return nullptr;
    }
    Stmt *VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S) {
      HIPACC_NOT_SUPPORTED(ObjCAutoreleasePoolStmt);
      return nullptr;
    }

    // C++ Statements
    Stmt *VisitCXXCatchStmt(CXXCatchStmt *S);
    Stmt *VisitCXXTryStmt(CXXTryStmt *S);
    Stmt *VisitCXXForRangeStmt(CXXForRangeStmt *S);

    // Expressions
    Expr *VisitExpr(Expr *E);
    Expr *VisitPredefinedExpr(PredefinedExpr *E);
    Expr *VisitDeclRefExpr(DeclRefExpr *E);
    Expr *VisitIntegerLiteral(IntegerLiteral *E);
    Expr *VisitFloatingLiteral(FloatingLiteral *E);
    Expr *VisitImaginaryLiteral(ImaginaryLiteral *E);
    Expr *VisitStringLiteral(StringLiteral *E);
    Expr *VisitCharacterLiteral(CharacterLiteral *E);
    Expr *VisitParenExpr(ParenExpr *E);
    Expr *VisitUnaryOperator(UnaryOperator *E);
    Expr *VisitOffsetOfExpr(OffsetOfExpr *E);
    Expr *VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E);
    Expr *VisitArraySubscriptExpr(ArraySubscriptExpr *E);
    // Expr *VisitCallExpr(CallExpr *E);
    VISIT_MODE(Expr, CallExpr)
    // Expr *VisitMemberExpr(MemberExpr *E);
    VISIT_MODE(Expr, MemberExpr)
    Expr *VisitCastExpr(CastExpr *E);
    // Expr *VisitBinaryOperator(BinaryOperator *E);
    VISIT_MODE(Expr, BinaryOperator)
    Expr *VisitCompoundAssignOperator(CompoundAssignOperator *E);
    Expr *VisitAbstractConditionalOperator(AbstractConditionalOperator *E);
    Expr *VisitConditionalOperator(ConditionalOperator *E);
    Expr *VisitBinaryConditionalOperator(BinaryConditionalOperator *E);
    // Expr *VisitImplicitCastExpr(ImplicitCastExpr *E);
    VISIT_MODE(Expr, ImplicitCastExpr)
    Expr *VisitExplicitCastExpr(ExplicitCastExpr *E);
    // Expr *VisitCStyleCastExpr(CStyleCastExpr *E);
    VISIT_MODE(Expr, CStyleCastExpr)
    Expr *VisitCompoundLiteralExpr(CompoundLiteralExpr *E);
    Expr *VisitExtVectorElementExpr(ExtVectorElementExpr *E);
    Expr *VisitInitListExpr(InitListExpr *E);
    Expr *VisitDesignatedInitExpr(DesignatedInitExpr *E);
    Expr *VisitImplicitValueInitExpr(ImplicitValueInitExpr *E);
    Expr *VisitParenListExpr(ParenListExpr *E);
    Expr *VisitVAArgExpr(VAArgExpr *E);
    Expr *VisitGenericSelectionExpr(GenericSelectionExpr *E);
    Expr *VisitPseudoObjectExpr(PseudoObjectExpr *E);

    // Atomic Expressions
    Expr *VisitAtomicExpr(AtomicExpr *E);

    // GNU Extensions
    Expr *VisitAddrLabelExpr(AddrLabelExpr *E);
    Expr *VisitStmtExpr(StmtExpr *E);
    Expr *VisitChooseExpr(ChooseExpr *E);
    Expr *VisitGNUNullExpr(GNUNullExpr *E);

    // C++ Expressions
    // Expr *VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E);
    VISIT_MODE(Expr, CXXOperatorCallExpr)
    // Expr *VisitCXXMemberCallExpr(CXXMemberCallExpr *E);
    VISIT_MODE(Expr, CXXMemberCallExpr)
    Expr *VisitCXXNamedCastExpr(CXXNamedCastExpr *E);
    Expr *VisitCXXStaticCastExpr(CXXStaticCastExpr *E);
    Expr *VisitCXXDynamicCastExpr(CXXDynamicCastExpr *E);
    Expr *VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E);
    Expr *VisitCXXConstCastExpr(CXXConstCastExpr *E);
    Expr *VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E);
    Expr *VisitCXXTypeidExpr(CXXTypeidExpr *E);
    Expr *VisitUserDefinedLiteral(UserDefinedLiteral *E);
    Expr *VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E);
    Expr *VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E);
    Expr *VisitCXXThisExpr(CXXThisExpr *E);
    Expr *VisitCXXThrowExpr(CXXThrowExpr *E);
    Expr *VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E);
    Expr *VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E);
    Expr *VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E);
    Expr *VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E);
    Expr *VisitCXXNewExpr(CXXNewExpr *E);
    Expr *VisitCXXDeleteExpr(CXXDeleteExpr *E);
    Expr *VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E);
    Expr *VisitTypeTraitExpr(TypeTraitExpr *E);
    Expr *VisitUnaryTypeTraitExpr(UnaryTypeTraitExpr *E);
    Expr *VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E);
    Expr *VisitExpressionTraitExpr(ExpressionTraitExpr *E);
    Expr *VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E);
    Expr *VisitCXXConstructExpr(CXXConstructExpr *E);
    Expr *VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E);
    Expr *VisitExprWithCleanups(ExprWithCleanups *E);
    Expr *VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E);
    Expr *VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E);
    Expr *VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E);
    Expr *VisitOverloadExpr(OverloadExpr *E);
    Expr *VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E);
    Expr *VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E);
    Expr *VisitCXXNoexceptExpr(CXXNoexceptExpr *E);
    Expr *VisitPackExpansionExpr(PackExpansionExpr *E);
    Expr *VisitSizeOfPackExpr(SizeOfPackExpr *E);
    Expr *VisitSubstNonTypeTemplateParmExpr(SubstNonTypeTemplateParmExpr *E);
    Expr *VisitSubstNonTypeTemplateParmPackExpr(
        SubstNonTypeTemplateParmPackExpr *E);
    Expr *VisitFunctionParmPackExpr(FunctionParmPackExpr *E);
    Expr *VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E);
    Expr *VisitLambdaExpr(LambdaExpr *E);

    // Obj-C Expressions
    Expr *VisitObjCStringLiteral(ObjCStringLiteral *E) {
      HIPACC_NOT_SUPPORTED(ObjCStringLiteral);
      return nullptr;
    }
    Expr *VisitObjCBoxedExpr(ObjCBoxedExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCBoxedExpr);
      return nullptr;
    }
    Expr *VisitObjCArrayLiteral(ObjCArrayLiteral *E) {
      HIPACC_NOT_SUPPORTED(ObjCArrayLiteral);
      return nullptr;
    }
    Expr *VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
      HIPACC_NOT_SUPPORTED(ObjCDictionaryLiteral);
      return nullptr;
    }
    Expr *VisitObjCEncodeExpr(ObjCEncodeExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCEncodeExpr);
      return nullptr;
    }
    Expr *VisitObjCMessageExpr(ObjCMessageExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCMessageExpr);
      return nullptr;
    }
    Expr *VisitObjCSelectorExpr(ObjCSelectorExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCSelectorExpr);
      return nullptr;
    }
    Expr *VisitObjCProtocolExpr(ObjCProtocolExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCProtocolExpr);
      return nullptr;
    }
    Expr *VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCIvarRefExpr);
      return nullptr;
    }
    Expr *VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCPropertyRefExpr);
      return nullptr;
    }
    Expr *VisitObjCIsaExpr(ObjCIsaExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCIsaExpr);
      return nullptr;
    }
    Expr *VisitObjCIndirectCopyRestoreExpr(ObjCIndirectCopyRestoreExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCIndirectCopyRestoreExpr);
      return nullptr;
    }
    Expr *VisitObjCBoolLiteralExpr(ObjCBoolLiteralExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCBoolLiteralExpr);
      return nullptr;
    }
    Expr *VisitObjCSubscriptRefExpr(ObjCSubscriptRefExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCSubscriptRefExpr);
      return nullptr;
    }

    // Obj-C ARC Expressions
    Expr *VisitObjCBridgedCastExpr(ObjCBridgedCastExpr *E) {
      HIPACC_NOT_SUPPORTED(ObjCBridgedCastExpr);
      return nullptr;
    }

    // CUDA Expressions
    Expr *VisitCUDAKernelCallExpr(CUDAKernelCallExpr *E);

    // Clang Extensions
    Expr *VisitShuffleVectorExpr(ShuffleVectorExpr *E);
    Expr *VisitConvertVectorExpr(ConvertVectorExpr *E);
    Expr *VisitBlockExpr(BlockExpr *E) {
      HIPACC_NOT_SUPPORTED(BlockExpr);
      return nullptr;
    }
    Expr *VisitOpaqueValueExpr(OpaqueValueExpr *E) {
      HIPACC_NOT_SUPPORTED(OpaqueValueExpr);
      return nullptr;
    }

    // Microsoft Extensions
    Expr *VisitMSPropertyRefExpr(MSPropertyRefExpr *E) {
      HIPACC_NOT_SUPPORTED(MSPropertyRefExpr);
      return nullptr;
    }
    Expr *VisitCXXUuidofExpr(CXXUuidofExpr *E) {
      HIPACC_NOT_SUPPORTED(CXXUuidofExpr);
      return nullptr;
    }
    Stmt *VisitSEHTryStmt(SEHTryStmt *S) {
      HIPACC_NOT_SUPPORTED(SEHTryStmt);
      return nullptr;
    }
    Stmt *VisitSEHExceptStmt(SEHExceptStmt *S) {
      HIPACC_NOT_SUPPORTED(SEHExceptStmt);
      return nullptr;
    }
    Stmt *VisitSEHFinallyStmt(SEHFinallyStmt *S) {
      HIPACC_NOT_SUPPORTED(SEHFinallyStmt);
      return nullptr;
    }
    Stmt *VisitMSDependentExistsStmt(MSDependentExistsStmt *S) {
      HIPACC_NOT_SUPPORTED(MSDependentExistsStmt);
      return nullptr;
    }

    // OpenCL Expressions
    Expr *VisitAsTypeExpr(AsTypeExpr *E);

    // OpenMP Directives
    Stmt *VisitOMPExecutableDirective(OMPExecutableDirective *S) {
      HIPACC_NOT_SUPPORTED(OMPExecutableDirective);
      return nullptr;
    }
    Stmt *VisitOMPParallelDirective(OMPParallelDirective *S) {
      HIPACC_NOT_SUPPORTED(OMPParallelDirective);
      return nullptr;
    }
};
} // end namespace hipacc
} // end namespace clang

#endif  // _ASTTRANSLATE_H_

// vim: set ts=2 sw=2 sts=2 et ai:

