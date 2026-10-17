// Clang plugin for tools/x87s_rewrite.py: lists the floating-point operations
// of the game source that the original computed at single precision. Once the
// Direct3D device exists the x87 runs with a 24-bit precision control
// (CMR2Decomp/X87Single.h), so every add, subtract, multiply, divide and
// square root rounds to single precision; float operations of other compilers
// already do, double ones and integer loads do not.
//
// Writes JSON lines:
//   op     a double add/sub/mul/div to mark with X87S_ADD/SUB/MUL/DIV: the
//          ranges of its operands
//   sqrt   a call of sqrt to mark with X87S_SQRT: the callee token
//   fild   an integer converted to float as an arithmetic operand, which the
//          x87 loads exactly: the operand range, and the cast prefix if any
//          (X87S_FILD for a cast, X87S_LOAD for an implicit conversion)
//   skip   an operation that cannot be marked in place (compound assignment,
//          inside a macro body), or a math call to review
//
// Usage: clang++ -fsyntax-only -fplugin=X87Single.so
//            -fplugin-arg-x87s-out=FILE -fplugin-arg-x87s-dir=SOURCEDIR ...
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>

using namespace clang;
namespace json = llvm::json;

namespace {

bool isDouble(QualType t)
{
    const BuiltinType *b = t.getCanonicalType()->getAs<BuiltinType>();
    return b && (b->getKind() == BuiltinType::Double || b->getKind() == BuiltinType::LongDouble);
}

bool isFloat(QualType t)
{
    const BuiltinType *b = t.getCanonicalType()->getAs<BuiltinType>();
    return b && b->getKind() == BuiltinType::Float;
}

class Finder : public RecursiveASTVisitor<Finder> {
public:
    Finder(ASTContext &ctx, std::string dir, llvm::raw_ostream &out)
        : Ctx(ctx), SM(ctx.getSourceManager()), Dir(std::move(dir)), Out(out) {}

    bool shouldVisitTemplateInstantiations() const { return false; }

    bool TraverseFunctionDecl(FunctionDecl *fd) { return inFunction([&] { return RecursiveASTVisitor::TraverseFunctionDecl(fd); }); }
    bool TraverseCXXMethodDecl(CXXMethodDecl *fd) { return inFunction([&] { return RecursiveASTVisitor::TraverseCXXMethodDecl(fd); }); }
    bool TraverseCXXConstructorDecl(CXXConstructorDecl *fd) { return inFunction([&] { return RecursiveASTVisitor::TraverseCXXConstructorDecl(fd); }); }
    bool TraverseCXXDestructorDecl(CXXDestructorDecl *fd) { return inFunction([&] { return RecursiveASTVisitor::TraverseCXXDestructorDecl(fd); }); }
    bool TraverseUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *) { return true; }

    bool VisitBinaryOperator(BinaryOperator *e)
    {
        BinaryOperatorKind op = e->getOpcode();
        if (op != BO_Add && op != BO_Sub && op != BO_Mul && op != BO_Div)
            return true;
        if (!needsMark(e) || !inDir(e->getExprLoc()) || Functions == 0)
            return true;
        // MSVC6 folds constant operations at compile time, at full precision.
        if (e->isEvaluatable(Ctx))
            return true;
        if (markedOperator(e->getOperatorLoc()))
            return true;
        SourceLocation lb = fileLoc(e->getLHS()->getBeginLoc()), le = fileLoc(e->getLHS()->getEndLoc());
        SourceLocation rb = fileLoc(e->getRHS()->getBeginLoc()), re = fileLoc(e->getRHS()->getEndLoc());
        if (fileLoc(e->getOperatorLoc()).isValid()) {
            // An operand that is a whole macro invocation: wrap the invocation.
            lb = invocationLoc(e->getLHS()->getBeginLoc(), false);
            le = invocationLoc(e->getLHS()->getEndLoc(), true);
            rb = invocationLoc(e->getRHS()->getBeginLoc(), false);
            re = invocationLoc(e->getRHS()->getEndLoc(), true);
        }
        if (lb.isInvalid() || le.isInvalid() || rb.isInvalid() || re.isInvalid() || fileLoc(e->getOperatorLoc()).isInvalid()) {
            // Written whole in the body of one of the file's macros: mark it there.
            lb = bodyLoc(e->getLHS()->getBeginLoc());
            le = bodyLoc(e->getLHS()->getEndLoc());
            rb = bodyLoc(e->getRHS()->getBeginLoc());
            re = bodyLoc(e->getRHS()->getEndLoc());
            SourceLocation ol = bodyLoc(e->getOperatorLoc());
            if (!sameBody(e->getOperatorLoc(), {e->getLHS()->getBeginLoc(), e->getLHS()->getEndLoc(),
                                                 e->getRHS()->getBeginLoc(), e->getRHS()->getEndLoc()}))
                return skip(e, "operation inside a macro body");
            if (lb.isInvalid() || le.isInvalid() || rb.isInvalid() || re.isInvalid() || ol.isInvalid() ||
                SM.getFileID(lb) != SM.getFileID(re) || !(SM.getFileOffset(lb) <= SM.getFileOffset(le) &&
                SM.getFileOffset(le) < SM.getFileOffset(ol) && SM.getFileOffset(ol) < SM.getFileOffset(rb) &&
                SM.getFileOffset(rb) <= SM.getFileOffset(re)) || !inDir(lb))
                return skip(e, "operation inside a macro body");
        }
        SourceLocation lend = Lexer::getLocForEndOfToken(le, 0, SM, Ctx.getLangOpts());
        SourceLocation rend = Lexer::getLocForEndOfToken(re, 0, SM, Ctx.getLangOpts());
        if (SM.getFileID(lb) != SM.getFileID(rend))
            return skip(e, "operation spans files");
        static const char *names[] = {"X87S_MUL", "X87S_DIV", "", "X87S_ADD", "X87S_SUB"};
        emit(json::Object{{"t", "op"},
                          {"file", realPath(lb)},
                          {"line", (int64_t)SM.getSpellingLineNumber(lb)},
                          {"macro", names[op == BO_Mul ? 0 : op == BO_Div ? 1 : op == BO_Add ? 3 : 4]},
                          {"begin", (int64_t)SM.getFileOffset(lb)},
                          {"lhsEnd", (int64_t)SM.getFileOffset(lend)},
                          {"rhsBegin", (int64_t)SM.getFileOffset(rb)},
                          {"end", (int64_t)SM.getFileOffset(rend)}});
        return true;
    }

    bool VisitCompoundAssignOperator(CompoundAssignOperator *e)
    {
        if (!inDir(e->getExprLoc()) || Functions == 0)
            return true;
        if (isDouble(e->getComputationResultType()) && !markedOperator(e->getOperatorLoc()))
            skip(e, "compound assignment computed in double");
        return true;
    }

    bool VisitCallExpr(CallExpr *e)
    {
        const FunctionDecl *fd = e->getDirectCallee();
        if (!fd || !fd->getIdentifier() || !inDir(e->getExprLoc()) || Functions == 0)
            return true;
        llvm::StringRef name = fd->getName();
        static const std::set<std::string> review = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2",
                                                     "pow", "exp", "log", "log10", "fmod", "sinf", "cosf"};
        if (name == "sqrt") {
            if (markedOperator(e->getCallee()->getBeginLoc()))
                return true;
            SourceLocation b = fileLoc(e->getCallee()->getBeginLoc());
            if (b.isInvalid())
                return skip(e, "sqrt inside a macro body");
            emit(json::Object{{"t", "sqrt"},
                              {"file", realPath(b)},
                              {"line", (int64_t)SM.getSpellingLineNumber(b)},
                              {"begin", (int64_t)SM.getFileOffset(b)},
                              {"end", (int64_t)SM.getFileOffset(b) + 4}});
        } else if (review.count(name.str())) {
            skip(e, "math call " + name.str());
        }
        return true;
    }

    // An integer converted to float and used as an arithmetic operand: the x87
    // loads it exactly (FILD), other compilers round it to single precision.
    bool VisitCastExpr(CastExpr *e)
    {
        if (e->getCastKind() != CK_IntegralToFloating || !isFloat(e->getType()))
            return true;
        if (!inDir(e->getExprLoc()) || Functions == 0 || e->isEvaluatable(Ctx))
            return true;
        if (Ctx.getTypeSize(e->getSubExpr()->getType()) <= 16)
            return true; // exact in single precision
        // An implicit conversion under an explicit (float) cast is the cast's.
        const Expr *outer = e;
        const CStyleCastExpr *cs = dyn_cast<CStyleCastExpr>(e);
        if (isa<ImplicitCastExpr>(e)) {
            auto parents = Ctx.getParents(*e);
            if (!parents.empty() && (cs = parents[0].get<CStyleCastExpr>()) && cs->getCastKind() == CK_NoOp)
                outer = cs;
            else
                cs = nullptr;
        }
        if (!arithmeticOperand(outer))
            return true;
        if (markedOperator(cs ? cs->getLParenLoc() : e->getExprLoc()) || markedLoad(e->getSubExpr()->getBeginLoc()))
            return true;
        const Expr *value = e->getSubExpr();
        SourceLocation vb = fileLoc(value->getBeginLoc()), ve = fileLoc(value->getEndLoc());
        bool body = false;
        if (vb.isInvalid() || ve.isInvalid()) {
            if (!sameBody(e->getExprLoc(), {value->getBeginLoc(), value->getEndLoc()}))
                return skip(e, "integer load inside a macro body");
            vb = bodyLoc(value->getBeginLoc());
            ve = bodyLoc(value->getEndLoc());
            body = true;
            if (vb.isInvalid() || ve.isInvalid() || SM.getFileID(vb) != SM.getFileID(ve) || !inDir(vb) ||
                SM.getFileOffset(vb) > SM.getFileOffset(ve))
                return skip(e, "integer load inside a macro body");
        }
        SourceLocation vend = Lexer::getLocForEndOfToken(ve, 0, SM, Ctx.getLangOpts());
        json::Object row{{"t", "fild"},
                         {"file", realPath(vb)},
                         {"line", (int64_t)SM.getSpellingLineNumber(vb)},
                         {"begin", (int64_t)SM.getFileOffset(vb)},
                         {"end", (int64_t)SM.getFileOffset(vend)}};
        if (cs) {
            SourceLocation pb = body ? bodyLoc(cs->getLParenLoc()) : fileLoc(cs->getLParenLoc());
            SourceLocation pe = body ? bodyLoc(cs->getRParenLoc()) : fileLoc(cs->getRParenLoc());
            if (pb.isInvalid() || pe.isInvalid())
                return skip(e, "float cast inside a macro body");
            row["prefix"] = (int64_t)SM.getFileOffset(pb);
            row["prefixEnd"] = (int64_t)SM.getFileOffset(pe) + 1;
        }
        emit(std::move(row));
        return true;
    }

private:
    ASTContext &Ctx;
    SourceManager &SM;
    std::string Dir;
    llvm::raw_ostream &Out;
    std::map<FileID, std::string> Paths;
    int Functions = 0;

    template <typename F> bool inFunction(F traverse)
    {
        Functions++;
        bool result = traverse();
        Functions--;
        return result;
    }

    static const Expr *operand(const Expr *e)
    {
        for (;;) {
            if (const ParenExpr *p = dyn_cast<ParenExpr>(e))
                e = p->getSubExpr();
            else if (const ImplicitCastExpr *c = dyn_cast<ImplicitCastExpr>(e);
                     c && c->getCastKind() != CK_IntegralToFloating)
                e = c->getSubExpr();
            else
                return e;
        }
    }

    static bool isIntegerLoad(const Expr *e)
    {
        e = operand(e);
        if (const CStyleCastExpr *cs = dyn_cast<CStyleCastExpr>(e))
            e = cs->getSubExpr();
        const CastExpr *c = dyn_cast<CastExpr>(e);
        return c && c->getCastKind() == CK_IntegralToFloating && isFloat(c->getType());
    }

    // A double operation, or a float one whose operand the other compilers
    // would not hold at the x87's precision: an integer load or a marked
    // operation (both become doubles there).
    bool needsMark(const Expr *e)
    {
        e = operand(e);
        const BinaryOperator *b = dyn_cast<BinaryOperator>(e);
        if (!b)
            return false;
        BinaryOperatorKind op = b->getOpcode();
        if (op != BO_Add && op != BO_Sub && op != BO_Mul && op != BO_Div)
            return false;
        if (b->isEvaluatable(Ctx))
            return false;
        if (isDouble(b->getType()))
            return true;
        if (!isFloat(b->getType()))
            return false;
        return isIntegerLoad(b->getLHS()) || isIntegerLoad(b->getRHS()) || needsMark(b->getLHS()) || needsMark(b->getRHS());
    }

    // Whether the expression is an operand of an add/sub/mul/div (directly or
    // through parentheses and implicit conversions).
    bool arithmeticOperand(const Expr *e)
    {
        for (;;) {
            auto parents = Ctx.getParents(*e);
            if (parents.empty())
                return false;
            if (const Expr *p = parents[0].get<Expr>()) {
                if (isa<ParenExpr>(p) || isa<ImplicitCastExpr>(p)) {
                    e = p;
                    continue;
                }
                if (const BinaryOperator *b = dyn_cast<BinaryOperator>(p)) {
                    BinaryOperatorKind op = b->getOpcode();
                    return op == BO_Add || op == BO_Sub || op == BO_Mul || op == BO_Div ||
                           op == BO_AddAssign || op == BO_SubAssign || op == BO_MulAssign || op == BO_DivAssign;
                }
            }
            return false;
        }
    }

    // An operand already written as X87S_LOAD(i).
    bool markedLoad(SourceLocation loc)
    {
        return loc.isMacroID() && SM.isMacroArgExpansion(loc) &&
               Lexer::getImmediateMacroName(loc, SM, Ctx.getLangOpts()) == "X87S_LOAD";
    }

    // Already marked: the operator comes from an X87S_ macro body.
    bool markedOperator(SourceLocation loc)
    {
        if (!loc.isMacroID())
            return false;
        return llvm::StringRef(SM.getFilename(SM.getSpellingLoc(loc))).ends_with("X87Single.h");
    }

    std::string realPath(SourceLocation loc)
    {
        FileID id = SM.getFileID(loc);
        auto it = Paths.find(id);
        if (it != Paths.end())
            return it->second;
        llvm::SmallString<256> real;
        if (llvm::sys::fs::real_path(SM.getFilename(loc), real))
            real = SM.getFilename(loc);
        return Paths[id] = real.str().str();
    }

    bool inDir(SourceLocation loc)
    {
        loc = SM.getExpansionLoc(loc);
        return loc.isValid() && llvm::StringRef(realPath(loc)).starts_with(Dir);
    }

    // A location written in the file (directly or as a macro argument).
    // The file location of `loc`, or of the macro invocation it was expanded
    // from (its first or last token).
    SourceLocation invocationLoc(SourceLocation loc, bool end)
    {
        while (loc.isMacroID()) {
            if (SM.isMacroArgExpansion(loc))
                loc = SM.getImmediateSpellingLoc(loc);
            else {
                CharSourceRange range = SM.getImmediateExpansionRange(loc);
                loc = end ? range.getEnd() : range.getBegin();
            }
        }
        return loc;
    }

    // Whether every location is a token of the same macro body as `anchor`
    // (not of an argument written where the macro is used).
    bool sameBody(SourceLocation anchor, std::initializer_list<SourceLocation> locs)
    {
        if (fileLoc(anchor).isValid() || !anchor.isMacroID())
            return false;
        SourceLocation expansion = SM.getExpansionLoc(anchor);
        llvm::StringRef name = Lexer::getImmediateMacroName(anchor, SM, Ctx.getLangOpts());
        for (SourceLocation loc : locs)
            if (fileLoc(loc).isValid() || !loc.isMacroID() || SM.getExpansionLoc(loc) != expansion ||
                Lexer::getImmediateMacroName(loc, SM, Ctx.getLangOpts()) != name)
                return false;
        return true;
    }

    // Where a token of a macro body is written (in its #define).
    SourceLocation bodyLoc(SourceLocation loc)
    {
        loc = SM.getSpellingLoc(loc);
        return loc.isFileID() ? loc : SourceLocation();
    }

    // Only through macro arguments: text from a macro body is not in place.
    SourceLocation fileLoc(SourceLocation loc)
    {
        while (loc.isMacroID()) {
            if (!SM.isMacroArgExpansion(loc))
                return SourceLocation();
            loc = SM.getImmediateSpellingLoc(loc);
        }
        return loc;
    }

    bool skip(const Expr *e, const std::string &why)
    {
        SourceLocation loc = SM.getExpansionLoc(e->getExprLoc());
        std::string macro;
        if (e->getExprLoc().isMacroID())
            macro = Lexer::getImmediateMacroName(e->getExprLoc(), SM, Ctx.getLangOpts()).str();
        emit(json::Object{{"t", "skip"},
                          {"file", realPath(loc)},
                          {"line", (int64_t)SM.getExpansionLineNumber(loc)},
                          {"why", why},
                          {"macro", macro}});
        return true;
    }

    void emit(json::Object obj) { Out << json::Value(std::move(obj)) << "\n"; }
};

class Consumer : public ASTConsumer {
public:
    Consumer(std::string dir, std::string out) : Dir(std::move(dir)), OutPath(std::move(out)) {}

    void HandleTranslationUnit(ASTContext &ctx) override
    {
        std::error_code ec;
        llvm::raw_fd_ostream out(OutPath, ec);
        if (ec) {
            llvm::errs() << "x87s: cannot write " << OutPath << ": " << ec.message() << "\n";
            return;
        }
        Finder(ctx, Dir, out).TraverseDecl(ctx.getTranslationUnitDecl());
    }

private:
    std::string Dir, OutPath;
};

class Action : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<Consumer>(Dir, Out);
    }

    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &args) override
    {
        for (const std::string &arg : args) {
            if (llvm::StringRef(arg).starts_with("out="))
                Out = arg.substr(4);
            else if (llvm::StringRef(arg).starts_with("dir="))
                Dir = arg.substr(4);
        }
        return !Out.empty() && !Dir.empty();
    }

    ActionType getActionType() override { return AddAfterMainAction; }

private:
    std::string Dir, Out;
};

} // namespace

static FrontendPluginRegistry::Add<Action> X("x87s", "list the x87 single-precision operations");
