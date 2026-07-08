
// Generated from SMTLIBv2.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  SMTLIBv2Lexer : public antlr4::Lexer {
public:
  enum {
    Comment = 1, ParOpen = 2, ParClose = 3, Semicolon = 4, String = 5, QuotedSymbol = 6, 
    PS_Not = 7, PS_Bool = 8, PS_ContinuedExecution = 9, PS_Error = 10, PS_False = 11, 
    PS_ImmediateExit = 12, PS_Incomplete = 13, PS_Logic = 14, PS_Memout = 15, 
    PS_Sat = 16, PS_Success = 17, PS_Theory = 18, PS_True = 19, PS_Unknown = 20, 
    PS_Unsupported = 21, PS_Unsat = 22, CMD_Assert = 23, CMD_CheckSat = 24, 
    CMD_CheckSatAssuming = 25, CMD_DeclareConst = 26, CMD_DeclareDatatype = 27, 
    CMD_DeclareDatatypes = 28, CMD_DeclareFun = 29, CMD_DeclareSort = 30, 
    CMD_DefineFun = 31, CMD_DefineFunRec = 32, CMD_DefineFunsRec = 33, CMD_DefineSort = 34, 
    CMD_Echo = 35, CMD_Exit = 36, CMD_GetAssertions = 37, CMD_GetAssignment = 38, 
    CMD_GetInfo = 39, CMD_GetModel = 40, CMD_GetOption = 41, CMD_GetProof = 42, 
    CMD_GetUnsatAssumptions = 43, CMD_GetUnsatCore = 44, CMD_GetValue = 45, 
    CMD_Pop = 46, CMD_Push = 47, CMD_Reset = 48, CMD_ResetAssertions = 49, 
    CMD_SetInfo = 50, CMD_SetLogic = 51, CMD_SetOption = 52, GRW_Exclamation = 53, 
    GRW_Underscore = 54, GRW_As = 55, GRW_Binary = 56, GRW_Decimal = 57, 
    GRW_Exists = 58, GRW_Hexadecimal = 59, GRW_Forall = 60, GRW_Let = 61, 
    GRW_Match = 62, GRW_Numeral = 63, GRW_Par = 64, GRW_String = 65, Numeral = 66, 
    Binary = 67, HexDecimal = 68, Decimal = 69, Colon = 70, PK_AllStatistics = 71, 
    PK_AssertionStackLevels = 72, PK_Authors = 73, PK_Category = 74, PK_Chainable = 75, 
    PK_Definition = 76, PK_DiagnosticOutputChannel = 77, PK_ErrorBehaviour = 78, 
    PK_Extension = 79, PK_Funs = 80, PK_FunsDescription = 81, PK_GlobalDeclarations = 82, 
    PK_InteractiveMode = 83, PK_Language = 84, PK_LeftAssoc = 85, PK_License = 86, 
    PK_Named = 87, PK_Name = 88, PK_Notes = 89, PK_Pattern = 90, PK_PrintSuccess = 91, 
    PK_ProduceAssertions = 92, PK_ProduceAssignments = 93, PK_ProduceModels = 94, 
    PK_ProduceProofs = 95, PK_ProduceUnsatAssumptions = 96, PK_ProduceUnsatCores = 97, 
    PK_RandomSeed = 98, PK_ReasonUnknown = 99, PK_RegularOutputChannel = 100, 
    PK_ReproducibleResourceLimit = 101, PK_RightAssoc = 102, PK_SmtLibVersion = 103, 
    PK_Sorts = 104, PK_SortsDescription = 105, PK_Source = 106, PK_Status = 107, 
    PK_Theories = 108, PK_Values = 109, PK_Verbosity = 110, PK_Version = 111, 
    RS_Model = 112, UndefinedSymbol = 113, WS = 114
  };

  explicit SMTLIBv2Lexer(antlr4::CharStream *input);

  ~SMTLIBv2Lexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

