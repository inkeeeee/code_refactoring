#include "clang/AST/Attr.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"

#include <fstream>
#include <string>
#include <unordered_set>

#include "RefactorTool.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolCategory("refactor-tool options");

static bool tokenSpells(const Token &Tok, llvm::StringRef Expected, SourceManager &SM, const LangOptions &LangOpts) {
    SourceLocation Begin = Tok.getLocation();
    SourceLocation End = Begin.getLocWithOffset(Tok.getLength());

    if (!Begin.isValid() || !End.isValid()) {
        return false;
    }

    llvm::StringRef Text = Lexer::getSourceText(CharSourceRange::getCharRange(Begin, End), SM, LangOpts);

    return Text == Expected;
}

static void logChange(const char *Kind, SourceLocation Loc, SourceManager &SM, llvm::StringRef Message) {
    SourceLocation SpellingLoc = SM.getSpellingLoc(Loc);

    if (!SpellingLoc.isValid()) {
        return;
    }

    PresumedLoc PLoc = SM.getPresumedLoc(SpellingLoc);

    if (PLoc.isInvalid()) {
        return;
    }

    std::ofstream Log("refactor_tool_changes.log", std::ios::app);

    if (!Log) {
        return;
    }

    Log << Kind << ": " << PLoc.getFilename() << ':' << PLoc.getLine() << ':' << PLoc.getColumn() << " - "
        << Message.str() << '\n';
}

static SourceLocation findOverrideInsertionLoc(const CXXMethodDecl *Method, SourceManager &SM,
                                               const LangOptions &LangOpts) {
    SourceLocation StartLoc = SM.getSpellingLoc(Method->getNameInfo().getEndLoc());

    if (!StartLoc.isValid()) {
        return {};
    }

    FileID FID = SM.getFileID(StartLoc);

    bool Invalid = false;
    llvm::StringRef Buffer = SM.getBufferData(FID, &Invalid);

    if (Invalid) {
        return {};
    }

    const unsigned StartOffset = SM.getFileOffset(StartLoc);

    const char *BufferStart = Buffer.data();
    const char *BufferEnd = BufferStart + Buffer.size();
    const char *LexStart = BufferStart + StartOffset;

    Lexer RawLexer(SM.getLocForStartOfFile(FID), LangOpts, BufferStart, LexStart, BufferEnd);

    RawLexer.SetCommentRetentionState(true);

    Token Tok;
    SourceLocation PreviousTokenEnd;

    int ParenDepth = 0;
    bool SawParameterList = false;
    bool FinishedParameterList = false;

    while (!RawLexer.LexFromRawLexer(Tok)) {
        if (Tok.is(tok::eof)) {
            break;
        }

        if (Tok.is(tok::l_paren)) {
            ++ParenDepth;
            SawParameterList = true;
            PreviousTokenEnd = Lexer::getLocForEndOfToken(Tok.getLocation(), 0, SM, LangOpts);
            continue;
        }

        if (Tok.is(tok::r_paren) && ParenDepth > 0) {
            --ParenDepth;

            PreviousTokenEnd = Lexer::getLocForEndOfToken(Tok.getLocation(), 0, SM, LangOpts);

            if (SawParameterList && ParenDepth == 0) {
                FinishedParameterList = true;
            }

            continue;
        }

        if (!FinishedParameterList || ParenDepth != 0) {
            PreviousTokenEnd = Lexer::getLocForEndOfToken(Tok.getLocation(), 0, SM, LangOpts);
            continue;
        }

        if (tokenSpells(Tok, "final", SM, LangOpts)) {
            return PreviousTokenEnd;
        }

        if (Tok.is(tok::l_brace) || Tok.is(tok::semi) || Tok.is(tok::equal)) {
            return PreviousTokenEnd;
        }

        PreviousTokenEnd = Lexer::getLocForEndOfToken(Tok.getLocation(), 0, SM, LangOpts);
    }

    return {};
}

static SourceLocation findRangeForAmpInsertionLoc(const VarDecl *LoopVar, SourceManager &SM,
                                                  const LangOptions &LangOpts) {
    const TypeSourceInfo *TypeInfo = LoopVar->getTypeSourceInfo();

    if (!TypeInfo) {
        return {};
    }

    SourceLocation TypeEndLoc = SM.getSpellingLoc(TypeInfo->getTypeLoc().getEndLoc());

    if (!TypeEndLoc.isValid()) {
        return {};
    }

    return Lexer::getLocForEndOfToken(TypeEndLoc, 0, SM, LangOpts);
}

void RefactorHandler::run(const MatchFinder::MatchResult &Result) {
    auto &Diag = Result.Context->getDiagnostics();
    auto &SM = *Result.SourceManager;

    if (const auto *Dtor = Result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(Dtor, Diag, SM);
    }

    if (const auto *Method = Result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride")) {
        handle_miss_override(Method, Diag, SM);
    }

    if (const auto *LoopVar = Result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(LoopVar, Diag, SM);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *Dtor, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Dtor) {
        return;
    }

    SourceLocation InsertLoc = SM.getSpellingLoc(Dtor->getLocation());

    if (!InsertLoc.isValid() || !SM.isWrittenInMainFile(InsertLoc)) {
        return;
    }

    const unsigned Offset = SM.getFileOffset(InsertLoc);

    if (!virtualDtorLocations.insert(Offset).second) {
        return;
    }

    Rewrite.InsertTextBefore(InsertLoc, "virtual ");

    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Remark, "added 'virtual' to non-virtual base class destructor");

    Diag.Report(InsertLoc, DiagID);

    logChange("virtual-dtor", InsertLoc, SM, "added 'virtual' to non-virtual base class destructor");
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *Method, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!Method) {
        return;
    }

    if (llvm::isa<CXXDestructorDecl>(Method)) {
        return;
    }

    if (Method->isOutOfLine()) {
        return;
    }

    if (Method->size_overridden_methods() == 0 || Method->hasAttr<OverrideAttr>()) {
        return;
    }

    SourceLocation MethodLoc = SM.getSpellingLoc(Method->getLocation());

    if (!MethodLoc.isValid() || !SM.isWrittenInMainFile(MethodLoc)) {
        return;
    }

    SourceLocation InsertLoc = findOverrideInsertionLoc(Method, SM, Rewrite.getLangOpts());

    InsertLoc = SM.getSpellingLoc(InsertLoc);

    if (!InsertLoc.isValid() || !SM.isWrittenInMainFile(InsertLoc)) {
        return;
    }

    Rewrite.InsertTextBefore(InsertLoc, " override");

    const unsigned DiagID = Diag.getCustomDiagID(DiagnosticsEngine::Remark, "added 'override' to overriding method");

    Diag.Report(MethodLoc, DiagID);

    logChange("override", MethodLoc, SM, "added 'override' to overriding method");
}

void RefactorHandler::handle_crange_for(const VarDecl *LoopVar, DiagnosticsEngine &Diag, SourceManager &SM) {
    if (!LoopVar) {
        return;
    }

    SourceLocation VarLoc = SM.getSpellingLoc(LoopVar->getLocation());

    if (!VarLoc.isValid() || !SM.isWrittenInMainFile(VarLoc)) {
        return;
    }

    QualType LoopType = LoopVar->getType();

    if (!LoopType.isConstQualified()) {
        return;
    }

    if (LoopType->isReferenceType()) {
        return;
    }

    QualType CanonicalType = LoopType.getCanonicalType();

    if (CanonicalType->isFundamentalType() || CanonicalType->isPointerType() || CanonicalType->isEnumeralType()) {
        return;
    }

    SourceLocation InsertLoc = findRangeForAmpInsertionLoc(LoopVar, SM, Rewrite.getLangOpts());

    InsertLoc = SM.getSpellingLoc(InsertLoc);

    if (!InsertLoc.isValid() || !SM.isWrittenInMainFile(InsertLoc)) {
        return;
    }

    Rewrite.InsertTextBefore(InsertLoc, "&");

    const unsigned DiagID =
        Diag.getCustomDiagID(DiagnosticsEngine::Remark, "added '&' to const range-for variable to avoid copying");

    Diag.Report(VarLoc, DiagID);

    logChange("range-for", VarLoc, SM, "added '&' to const range-for variable to avoid copying");
}

auto NvDtorMatcher() {
    return cxxRecordDecl(
        isDefinition(),
        hasAnyBase(cxxBaseSpecifier(hasType(
            cxxRecordDecl(isDefinition(),
                          has(cxxDestructorDecl(unless(isImplicit()), unless(isVirtual())).bind("nonVirtualDtor")))))));
}

auto NoOverrideMatcher() {
    return cxxMethodDecl(isOverride(), unless(isImplicit()), unless(hasAttr(attr::Override))).bind("missingOverride");
}

auto NoRefConstVarInRangeLoopMatcher() {
    return cxxForRangeStmt(hasLoopVariable(varDecl(hasType(qualType(isConstQualified()))).bind("loopVar")));
}

ComplexConsumer::ComplexConsumer(Rewriter &Rewrite) : Handler(Rewrite) {
    Finder.addMatcher(NvDtorMatcher(), &Handler);
    Finder.addMatcher(NoOverrideMatcher(), &Handler);
    Finder.addMatcher(NoRefConstVarInRangeLoopMatcher(), &Handler);
}

void ComplexConsumer::HandleTranslationUnit(ASTContext &Context) {
    Finder.matchAST(Context);
}

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &CI, StringRef file) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());

    return std::make_unique<ComplexConsumer>(RewriterForCodeRefactor);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &CI) {
    RewriterForCodeRefactor.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());

    return true;
}

void CodeRefactorAction::EndSourceFileAction() {
    if (RewriterForCodeRefactor.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);

    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    return Tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}
