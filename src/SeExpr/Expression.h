/*
 SEEXPR SOFTWARE
 Copyright 2011 Disney Enterprises, Inc. All rights reserved
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:
 
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in
 the documentation and/or other materials provided with the
 distribution.
 
 * The names "Disney", "Walt Disney Pictures", "Walt Disney Animation
 Studios" or the names of its contributors may NOT be used to
 endorse or promote products derived from this software without
 specific prior written permission from Walt Disney Pictures.
 
 Disclaimer: THIS SOFTWARE IS PROVIDED BY WALT DISNEY PICTURES AND
 CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
 BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE, NONINFRINGEMENT AND TITLE ARE DISCLAIMED.
 IN NO EVENT SHALL WALT DISNEY PICTURES, THE COPYRIGHT HOLDER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND BASED ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
*/
#ifndef Expression_h
#define Expression_h

#include <string>
#include <map>
#include <set>
#include <vector>
#include <iomanip>
#include <stdint.h>
#include "Vec3d.h"
#include "ExprEnv.h"

namespace llvm {
    class ExecutionEngine;
    class LLVMContext;
}

namespace SeExpr2 {

class ExprNode;
class ExprVarNode;
class ExprFunc;
class Expression;
class Interpreter;

//! abstract class for implementing variable references
class ExprVarRef
{
    ExprVarRef()
        : _type(ExprType().Error().Varying())
    {};

 public:
    ExprVarRef(const ExprType & type)
        : _type(type)
    {};

    virtual ~ExprVarRef() {}

    //! sets (current) type to given type
    virtual void setType(const ExprType & type) { _type = type; };

    //! returns (current) type
    virtual ExprType type() const { return _type; };

    // TODO: this is deprecated!
    //! returns this variable's value by setting result, node refers to 
    //! where in the parse tree the evaluation is occurring
    //virtual void eval(double* result,char**)=0;
    virtual void eval(double* result)=0;
    virtual void eval(const char** resultStr)=0;

private:
    ExprType _type;
};

/// simple vector variable reference reference base class
class ExprVectorVarRef : public ExprVarRef
{
 public:
    ExprVectorVarRef(int dim = 3)
        : ExprVarRef(ExprType().FP(dim).Varying())
    {};

    virtual bool isVec() { return 1; }
    virtual void eval(const ExprVarNode* node, Vec3d& result)=0;
    virtual void eval(double* result){
        Vec3d ret;
        eval(0,ret);
        for(int k=0;k<3;k++) result[k]=ret[k];
    }
    virtual void eval(const char** result){assert(false);}
};


/// simple scalar variable reference reference base class
class ExprScalarVarRef : public ExprVarRef
{
 public:
    ExprScalarVarRef()
        : ExprVarRef(ExprType().FP(1).Varying())
    {};

    virtual bool isVec() { return 0; }
    virtual void eval(const ExprVarNode* node, Vec3d& result)=0;
    virtual void eval(double* result){
        Vec3d ret;
        eval(0,ret);
        for(int k=0;k<1;k++) result[k]=ret[k];
    }
    virtual void eval(const char** result){assert(false);}

};

#if 0
/// uses internally to represent local variables
class ExprLocalVarRef : public ExprVarRef
{
    ExprLocalVarRef()
        : ExprVarRef(ExprType().Error().Varying()), val(0)
    {}

 public:
    union{
        double *val;
        const char* s;
    };
    ExprLocalVarRef(const ExprType & intype)
        :ExprVarRef(intype),val(0)
    {
        if(type().isFP()) val=new double[type().dim()];
    }

    virtual ~ExprLocalVarRef()
    {delete [] val;}

    virtual void evaluate(const ExprVarNode* node,const ExprEvalResult& evalResult){
        if(type().isString()) *evalResult.str=s;
        else{
            int d=type().dim();
            for(int k=0;k<d;k++) evalResult.fp[k]=val[k];
        }
    }
};
#endif


enum EvaluationStrategy {UseInterpreter, UseLLVM};
#ifdef SEEXPR_ENABLE_LLVM
    static EvaluationStrategy defaultEvaluationStrategy = UseLLVM;
#else
    static EvaluationStrategy defaultEvaluationStrategy = UseInterpreter;
#endif

/// main expression class
class Expression
{
 public:
    //typedef std::map<std::string, ExprLocalVarRef> LocalVarTable;

    //! Represents a parse or type checking error in an expression
    struct Error
    {
        //! Text of error
        std::string error;

        //! Error start offset in expression string
        int startPos;

        //! Error end offset in expression string
        int endPos;

        Error(const std::string& errorIn,const int startPosIn,const int endPosIn)
            :error(errorIn),startPos(startPosIn),endPos(endPosIn)
        {}
    };

    Expression(EvaluationStrategy be = defaultEvaluationStrategy);
    Expression( const std::string &e, const ExprType & type = ExprType().FP(3), EvaluationStrategy be = defaultEvaluationStrategy);

    virtual ~Expression();

    /** Sets desired return value.
        This will allow the evaluation to potentially be optimized. */
    void setDesiredReturnType(const ExprType & type);

    /** Set expression string to e.  
        This invalidates all parsed state. */
    void setExpr(const std::string& e);

    //! Get the string that this expression is currently set to evaluate
    const std::string& getExpr() const { return _expression; }

    /** Check expression syntax.  Expr will be parsed if needed.  If
	this returns false, the error message can be accessed via
	parseError() */
    bool syntaxOK() const;

    /** Check if expression is valid.  Expr will be parsed if
    needed. Variables and functions will also be bound.  If this
    returns false, the error message can be accessed via
    parseError() */
    bool isValid() const { prepIfNeeded(); return _isValid; }

    /** Get parse error (if any).  First call syntaxOK or isValid
	to parse (and optionally bind) the expression. */
    const std::string& parseError() const { return _parseError; }

    /** Get a reference to a list of parse errors in the expression.
        The error structure gives location information as well as the errors itself. */
    const std::vector<Error>& getErrors() const
    {return _errors;}

    /** Get a reference to a list of the ranges where comments occurred */
    const std::vector<std::pair<int,int> >& getComments() const
    {return _comments;}

    /** Check if expression is constant.
	Expr will be parsed if needed.  No binding is required. */
    bool isConstant() const;

    /** Determine whether expression uses a particular external variable. 
	Expr will be parsed if needed.  No binding is required. */
    bool usesVar(const std::string& name) const;

    /** Determine whether expression uses a particular function.
	Expr will be parsed if needed.  No binding is required. */
    bool usesFunc(const std::string& name) const;

    /** Returns whether the expression contains and calls to non-threadsafe */
    bool isThreadSafe() const
    {return _threadUnsafeFunctionCalls.size()==0;}

    /** Internal function where parse tree nodes can register violations in
        thread safety with the main class. */
    void setThreadUnsafe(const std::string& functionName) const
    {_threadUnsafeFunctionCalls.push_back(functionName);}

    /** Returns a list of functions that are not threadSafe **/
    const std::vector<std::string>& getThreadUnsafeFunctionCalls() const
    {return _threadUnsafeFunctionCalls;}
    
    /** Get wantVec setting */
    bool wantVec() const { return _wantVec; }

    /** Determine if expression computes a vector (may be false even
	if wantVec is true).  Expr will be parsed and variables and
	functions will be bound if needed. */
    bool isVec() const;

    /** Return the return type of the expression.  Currently may not
        match the type set in setReturnType.  Expr will be parsed and
        variables and functions will be bound if needed. */
    const ExprType & returnType() const;

    // TODO: make this deprecated
    /** Evaluates and returns float (check returnType()!) */
    const double* evalFP() const;

    // TODO: make this deprecated
    /** Evaluates and returns string (check returnType()!) */
    const char* evalStr() const;

    /** Reset expr - force reparse/rebind */
    void reset();

    /** override resolveVar to add external variables */
    virtual ExprVarRef* resolveVar(const std::string& name) const {return 0;}

    /** override resolveFunc to add external functions */
    virtual ExprFunc* resolveFunc(const std::string& name) const {return 0;}

    /** records an error in prep or parse stage */
    void addError(const std::string& error,const int startPos,const int endPos) const

    {_errors.push_back(Error(error,startPos,endPos));}

    /** records a comment */
    void addComment(int pos,int length)
    {_comments.push_back(std::pair<int,int>(pos,length+pos-1));}

    /** Returns a read only map of local variables that were set **/
    //const LocalVarTable& getLocalVars() const {return _localVars;}

 private:
    /** No definition by design. */
    Expression( const Expression &e );
    Expression &operator=( const Expression &e );

    /** Parse, and remember parse error if any */
    void parse() const;

    /** Parse, but only if not yet parsed */
    void parseIfNeeded() const { if (!_parsed) parse(); }

    /** Prepare expression (bind vars/functions, etc.)
    and remember error if any */
    void prep() const;

    /** True if the expression wants a vector */
    bool _wantVec;

    /** Computed return type. */
    mutable ExprType _returnType;

    /** The expression. */
    std::string _expression;
    
    EvaluationStrategy _evaluationStrategy;
 protected:

    /** Computed return type. */
    mutable ExprType _desiredReturnType;

    /** Variable environment */
    mutable ExprVarEnv* _varEnv;
    /** Parse tree (null if syntax is bad). */
    mutable ExprNode *_parseTree;

    /** Prepare, but only if not yet prepped */
    void prepIfNeeded() const { if (!_prepped) prep(); }

 private:
    /** Flag if we are valid or not */
    mutable bool _isValid;
    /** Flag set once expr is parsed/prepped (parsing is automatic and lazy) */
    mutable bool _parsed, _prepped;
    
    /** Cached parse error (returned by isValid) */
    mutable std::string _parseError;

    /** Cached parse error location {startline,startcolumn,endline,endcolumn} */
    mutable std::vector<Error> _errors;

    /** Cached comments */
    mutable std::vector<std::pair<int,int> > _comments;

    /** Variables used in this expr */
    mutable std::set<std::string> _vars;

    /** Functions used in this expr */
    mutable std::set<std::string> _funcs;

    /** Local variable table */
    //mutable LocalVarTable _localVars;

    /** Whether or not we have unsafe functions */
    mutable std::vector<std::string> _threadUnsafeFunctionCalls;

#ifdef SEEXPR_ENABLE_LLVM
    // TODO: let the dev code allocate memory?
    // FP is the native function for this expression.
    template<class T>
    class LLVMEvaluationContext{
        typedef void (*FunctionPtr)(T*);
        FunctionPtr functionPtr;
        T* resultData;
    public:
        LLVMEvaluationContext()
            :functionPtr(0), resultData(0)
        {}
        void init(void* fp,int dim){
            reset();
            functionPtr=reinterpret_cast<FunctionPtr>(fp);
            resultData=new T[dim];
        }
        void reset(){
            delete resultData; resultData=0;
            functionPtr=0;
            resultData=0;
        }
        const T* operator()()
        {
            assert(functionPtr && resultData);
            functionPtr(resultData);
            return resultData;
        }
    };
    mutable LLVMEvaluationContext<double> _llvmEvalFP;
    mutable LLVMEvaluationContext<char*> _llvmEvalStr;

    mutable llvm::LLVMContext *Context;
    mutable llvm::ExecutionEngine *TheExecutionEngine;
    void prepLLVM() const;
    std::string getUniqueName() const {
        std::ostringstream o;
        o << std::setbase(16) << (uint64_t)(this);
        return ("_" + o.str());
    }
#endif

    /** Interpreter */
public:
    // TODO: make this public for debugging during devel
    mutable Interpreter* _interpreter;
    mutable int _returnSlot;
private:

    /* internal */ public:

    //! add local variable (this is for internal use)
    void addVar(const char* n) const { _vars.insert(n); }

    //! add function evaluation (this is for internal use)
    void addFunc(const char* n) const { _funcs.insert(n); }

    ////! get local variable reference (this is for internal use)
    //ExprVarRef* resolveLocalVar(const char* n) const {
    //    LocalVarTable::iterator iter = _localVars.find(n);
    //    if (iter != _localVars.end()) return &iter->second;
    //    return 0;
    //}

    /** get local variable reference. This is potentially useful for expression debuggers
        and/or uses of expressions where mutable variables are desired */
    /* ExprLocalVarRef* getLocalVar(const char* n) const { */
    /*     return &_localVars[n];  */
    /* } */
};

}

#endif