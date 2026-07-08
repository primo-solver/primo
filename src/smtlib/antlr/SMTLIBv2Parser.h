
// Generated from SMTLIBv2.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  SMTLIBv2Parser : public antlr4::Parser {
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

  enum {
    RuleStart_ = 0, RuleGeneralReservedWord = 1, RuleSimpleSymbol = 2, RuleQuotedSymbol = 3, 
    RulePredefSymbol = 4, RulePredefKeyword = 5, RuleSymbol = 6, RuleNumeral = 7, 
    RuleDecimal = 8, RuleHexadecimal = 9, RuleBinary = 10, RuleString = 11, 
    RuleKeyword = 12, RuleSpec_constant = 13, RuleS_expr = 14, RuleIndex = 15, 
    RuleIdentifier = 16, RuleAttribute_value = 17, RuleAttribute = 18, RuleSort = 19, 
    RuleQual_identifer = 20, RuleVar_binding = 21, RuleSorted_var = 22, 
    RulePattern = 23, RuleMatch_case = 24, RuleTerm = 25, RuleSort_symbol_decl = 26, 
    RuleMeta_spec_constant = 27, RuleFun_symbol_decl = 28, RulePar_fun_symbol_decl = 29, 
    RuleTheory_attribute = 30, RuleTheory_decl = 31, RuleLogic_attribue = 32, 
    RuleLogic = 33, RuleSort_dec = 34, RuleSelector_dec = 35, RuleConstructor_dec = 36, 
    RuleDatatype_dec = 37, RuleFunction_dec = 38, RuleFunction_def = 39, 
    RuleProp_literal = 40, RuleScript = 41, RuleCmd_assert = 42, RuleCmd_checkSat = 43, 
    RuleCmd_checkSatAssuming = 44, RuleCmd_declareConst = 45, RuleCmd_declareDatatype = 46, 
    RuleCmd_declareDatatypes = 47, RuleCmd_declareFun = 48, RuleCmd_declareSort = 49, 
    RuleCmd_defineFun = 50, RuleCmd_defineFunRec = 51, RuleCmd_defineFunsRec = 52, 
    RuleCmd_defineSort = 53, RuleCmd_echo = 54, RuleCmd_exit = 55, RuleCmd_getAssertions = 56, 
    RuleCmd_getAssignment = 57, RuleCmd_getInfo = 58, RuleCmd_getModel = 59, 
    RuleCmd_getOption = 60, RuleCmd_getProof = 61, RuleCmd_getUnsatAssumptions = 62, 
    RuleCmd_getUnsatCore = 63, RuleCmd_getValue = 64, RuleCmd_pop = 65, 
    RuleCmd_push = 66, RuleCmd_reset = 67, RuleCmd_resetAssertions = 68, 
    RuleCmd_setInfo = 69, RuleCmd_setLogic = 70, RuleCmd_setOption = 71, 
    RuleCommand = 72, RuleB_value = 73, RuleOption = 74, RuleInfo_flag = 75, 
    RuleError_behaviour = 76, RuleReason_unknown = 77, RuleModel_response = 78, 
    RuleInfo_response = 79, RuleValuation_pair = 80, RuleT_valuation_pair = 81, 
    RuleCheck_sat_response = 82, RuleEcho_response = 83, RuleGet_assertions_response = 84, 
    RuleGet_assignment_response = 85, RuleGet_info_response = 86, RuleGet_model_response = 87, 
    RuleGet_option_response = 88, RuleGet_proof_response = 89, RuleGet_unsat_assump_response = 90, 
    RuleGet_unsat_core_response = 91, RuleGet_value_response = 92, RuleSpecific_success_response = 93, 
    RuleGeneral_response = 94
  };

  explicit SMTLIBv2Parser(antlr4::TokenStream *input);

  SMTLIBv2Parser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~SMTLIBv2Parser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class Start_Context;
  class GeneralReservedWordContext;
  class SimpleSymbolContext;
  class QuotedSymbolContext;
  class PredefSymbolContext;
  class PredefKeywordContext;
  class SymbolContext;
  class NumeralContext;
  class DecimalContext;
  class HexadecimalContext;
  class BinaryContext;
  class StringContext;
  class KeywordContext;
  class Spec_constantContext;
  class S_exprContext;
  class IndexContext;
  class IdentifierContext;
  class Attribute_valueContext;
  class AttributeContext;
  class SortContext;
  class Qual_identiferContext;
  class Var_bindingContext;
  class Sorted_varContext;
  class PatternContext;
  class Match_caseContext;
  class TermContext;
  class Sort_symbol_declContext;
  class Meta_spec_constantContext;
  class Fun_symbol_declContext;
  class Par_fun_symbol_declContext;
  class Theory_attributeContext;
  class Theory_declContext;
  class Logic_attribueContext;
  class LogicContext;
  class Sort_decContext;
  class Selector_decContext;
  class Constructor_decContext;
  class Datatype_decContext;
  class Function_decContext;
  class Function_defContext;
  class Prop_literalContext;
  class ScriptContext;
  class Cmd_assertContext;
  class Cmd_checkSatContext;
  class Cmd_checkSatAssumingContext;
  class Cmd_declareConstContext;
  class Cmd_declareDatatypeContext;
  class Cmd_declareDatatypesContext;
  class Cmd_declareFunContext;
  class Cmd_declareSortContext;
  class Cmd_defineFunContext;
  class Cmd_defineFunRecContext;
  class Cmd_defineFunsRecContext;
  class Cmd_defineSortContext;
  class Cmd_echoContext;
  class Cmd_exitContext;
  class Cmd_getAssertionsContext;
  class Cmd_getAssignmentContext;
  class Cmd_getInfoContext;
  class Cmd_getModelContext;
  class Cmd_getOptionContext;
  class Cmd_getProofContext;
  class Cmd_getUnsatAssumptionsContext;
  class Cmd_getUnsatCoreContext;
  class Cmd_getValueContext;
  class Cmd_popContext;
  class Cmd_pushContext;
  class Cmd_resetContext;
  class Cmd_resetAssertionsContext;
  class Cmd_setInfoContext;
  class Cmd_setLogicContext;
  class Cmd_setOptionContext;
  class CommandContext;
  class B_valueContext;
  class OptionContext;
  class Info_flagContext;
  class Error_behaviourContext;
  class Reason_unknownContext;
  class Model_responseContext;
  class Info_responseContext;
  class Valuation_pairContext;
  class T_valuation_pairContext;
  class Check_sat_responseContext;
  class Echo_responseContext;
  class Get_assertions_responseContext;
  class Get_assignment_responseContext;
  class Get_info_responseContext;
  class Get_model_responseContext;
  class Get_option_responseContext;
  class Get_proof_responseContext;
  class Get_unsat_assump_responseContext;
  class Get_unsat_core_responseContext;
  class Get_value_responseContext;
  class Specific_success_responseContext;
  class General_responseContext; 

  class  Start_Context : public antlr4::ParserRuleContext {
  public:
    Start_Context(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LogicContext *logic();
    antlr4::tree::TerminalNode *EOF();
    Theory_declContext *theory_decl();
    ScriptContext *script();
    General_responseContext *general_response();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Start_Context* start_();

  class  GeneralReservedWordContext : public antlr4::ParserRuleContext {
  public:
    GeneralReservedWordContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *GRW_Exclamation();
    antlr4::tree::TerminalNode *GRW_Underscore();
    antlr4::tree::TerminalNode *GRW_As();
    antlr4::tree::TerminalNode *GRW_Binary();
    antlr4::tree::TerminalNode *GRW_Decimal();
    antlr4::tree::TerminalNode *GRW_Exists();
    antlr4::tree::TerminalNode *GRW_Hexadecimal();
    antlr4::tree::TerminalNode *GRW_Forall();
    antlr4::tree::TerminalNode *GRW_Let();
    antlr4::tree::TerminalNode *GRW_Match();
    antlr4::tree::TerminalNode *GRW_Numeral();
    antlr4::tree::TerminalNode *GRW_Par();
    antlr4::tree::TerminalNode *GRW_String();
    antlr4::tree::TerminalNode *RS_Model();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  GeneralReservedWordContext* generalReservedWord();

  class  SimpleSymbolContext : public antlr4::ParserRuleContext {
  public:
    SimpleSymbolContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PredefSymbolContext *predefSymbol();
    antlr4::tree::TerminalNode *UndefinedSymbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SimpleSymbolContext* simpleSymbol();

  class  QuotedSymbolContext : public antlr4::ParserRuleContext {
  public:
    QuotedSymbolContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *QuotedSymbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  QuotedSymbolContext* quotedSymbol();

  class  PredefSymbolContext : public antlr4::ParserRuleContext {
  public:
    PredefSymbolContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PS_Not();
    antlr4::tree::TerminalNode *PS_Bool();
    antlr4::tree::TerminalNode *PS_ContinuedExecution();
    antlr4::tree::TerminalNode *PS_Error();
    antlr4::tree::TerminalNode *PS_False();
    antlr4::tree::TerminalNode *PS_ImmediateExit();
    antlr4::tree::TerminalNode *PS_Incomplete();
    antlr4::tree::TerminalNode *PS_Logic();
    antlr4::tree::TerminalNode *PS_Memout();
    antlr4::tree::TerminalNode *PS_Sat();
    antlr4::tree::TerminalNode *PS_Success();
    antlr4::tree::TerminalNode *PS_Theory();
    antlr4::tree::TerminalNode *PS_True();
    antlr4::tree::TerminalNode *PS_Unknown();
    antlr4::tree::TerminalNode *PS_Unsupported();
    antlr4::tree::TerminalNode *PS_Unsat();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PredefSymbolContext* predefSymbol();

  class  PredefKeywordContext : public antlr4::ParserRuleContext {
  public:
    PredefKeywordContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PK_AllStatistics();
    antlr4::tree::TerminalNode *PK_AssertionStackLevels();
    antlr4::tree::TerminalNode *PK_Authors();
    antlr4::tree::TerminalNode *PK_Category();
    antlr4::tree::TerminalNode *PK_Chainable();
    antlr4::tree::TerminalNode *PK_Definition();
    antlr4::tree::TerminalNode *PK_DiagnosticOutputChannel();
    antlr4::tree::TerminalNode *PK_ErrorBehaviour();
    antlr4::tree::TerminalNode *PK_Extension();
    antlr4::tree::TerminalNode *PK_Funs();
    antlr4::tree::TerminalNode *PK_FunsDescription();
    antlr4::tree::TerminalNode *PK_GlobalDeclarations();
    antlr4::tree::TerminalNode *PK_InteractiveMode();
    antlr4::tree::TerminalNode *PK_Language();
    antlr4::tree::TerminalNode *PK_LeftAssoc();
    antlr4::tree::TerminalNode *PK_License();
    antlr4::tree::TerminalNode *PK_Named();
    antlr4::tree::TerminalNode *PK_Name();
    antlr4::tree::TerminalNode *PK_Notes();
    antlr4::tree::TerminalNode *PK_Pattern();
    antlr4::tree::TerminalNode *PK_PrintSuccess();
    antlr4::tree::TerminalNode *PK_ProduceAssertions();
    antlr4::tree::TerminalNode *PK_ProduceAssignments();
    antlr4::tree::TerminalNode *PK_ProduceModels();
    antlr4::tree::TerminalNode *PK_ProduceProofs();
    antlr4::tree::TerminalNode *PK_ProduceUnsatAssumptions();
    antlr4::tree::TerminalNode *PK_ProduceUnsatCores();
    antlr4::tree::TerminalNode *PK_RandomSeed();
    antlr4::tree::TerminalNode *PK_ReasonUnknown();
    antlr4::tree::TerminalNode *PK_RegularOutputChannel();
    antlr4::tree::TerminalNode *PK_ReproducibleResourceLimit();
    antlr4::tree::TerminalNode *PK_RightAssoc();
    antlr4::tree::TerminalNode *PK_SmtLibVersion();
    antlr4::tree::TerminalNode *PK_Sorts();
    antlr4::tree::TerminalNode *PK_SortsDescription();
    antlr4::tree::TerminalNode *PK_Source();
    antlr4::tree::TerminalNode *PK_Status();
    antlr4::tree::TerminalNode *PK_Theories();
    antlr4::tree::TerminalNode *PK_Values();
    antlr4::tree::TerminalNode *PK_Verbosity();
    antlr4::tree::TerminalNode *PK_Version();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PredefKeywordContext* predefKeyword();

  class  SymbolContext : public antlr4::ParserRuleContext {
  public:
    SymbolContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SimpleSymbolContext *simpleSymbol();
    QuotedSymbolContext *quotedSymbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SymbolContext* symbol();

  class  NumeralContext : public antlr4::ParserRuleContext {
  public:
    NumeralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *Numeral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  NumeralContext* numeral();

  class  DecimalContext : public antlr4::ParserRuleContext {
  public:
    DecimalContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *Decimal();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DecimalContext* decimal();

  class  HexadecimalContext : public antlr4::ParserRuleContext {
  public:
    HexadecimalContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *HexDecimal();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  HexadecimalContext* hexadecimal();

  class  BinaryContext : public antlr4::ParserRuleContext {
  public:
    BinaryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *Binary();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BinaryContext* binary();

  class  StringContext : public antlr4::ParserRuleContext {
  public:
    StringContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *String();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  StringContext* string();

  class  KeywordContext : public antlr4::ParserRuleContext {
  public:
    KeywordContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    PredefKeywordContext *predefKeyword();
    antlr4::tree::TerminalNode *Colon();
    SimpleSymbolContext *simpleSymbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  KeywordContext* keyword();

  class  Spec_constantContext : public antlr4::ParserRuleContext {
  public:
    Spec_constantContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    NumeralContext *numeral();
    DecimalContext *decimal();
    HexadecimalContext *hexadecimal();
    BinaryContext *binary();
    StringContext *string();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Spec_constantContext* spec_constant();

  class  S_exprContext : public antlr4::ParserRuleContext {
  public:
    S_exprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Spec_constantContext *spec_constant();
    SymbolContext *symbol();
    KeywordContext *keyword();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<S_exprContext *> s_expr();
    S_exprContext* s_expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  S_exprContext* s_expr();

  class  IndexContext : public antlr4::ParserRuleContext {
  public:
    IndexContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    NumeralContext *numeral();
    SymbolContext *symbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IndexContext* index();

  class  IdentifierContext : public antlr4::ParserRuleContext {
  public:
    IdentifierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *GRW_Underscore();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<IndexContext *> index();
    IndexContext* index(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentifierContext* identifier();

  class  Attribute_valueContext : public antlr4::ParserRuleContext {
  public:
    Attribute_valueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Spec_constantContext *spec_constant();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<S_exprContext *> s_expr();
    S_exprContext* s_expr(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Attribute_valueContext* attribute_value();

  class  AttributeContext : public antlr4::ParserRuleContext {
  public:
    AttributeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    KeywordContext *keyword();
    Attribute_valueContext *attribute_value();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AttributeContext* attribute();

  class  SortContext : public antlr4::ParserRuleContext {
  public:
    SortContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<SortContext *> sort();
    SortContext* sort(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SortContext* sort();

  class  Qual_identiferContext : public antlr4::ParserRuleContext {
  public:
    Qual_identiferContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentifierContext *identifier();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *GRW_As();
    SortContext *sort();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Qual_identiferContext* qual_identifer();

  class  Var_bindingContext : public antlr4::ParserRuleContext {
  public:
    Var_bindingContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    SymbolContext *symbol();
    TermContext *term();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Var_bindingContext* var_binding();

  class  Sorted_varContext : public antlr4::ParserRuleContext {
  public:
    Sorted_varContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    SymbolContext *symbol();
    SortContext *sort();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Sorted_varContext* sorted_var();

  class  PatternContext : public antlr4::ParserRuleContext {
  public:
    PatternContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PatternContext* pattern();

  class  Match_caseContext : public antlr4::ParserRuleContext {
  public:
    Match_caseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    PatternContext *pattern();
    TermContext *term();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Match_caseContext* match_case();

  class  TermContext : public antlr4::ParserRuleContext {
  public:
    TermContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Spec_constantContext *spec_constant();
    Qual_identiferContext *qual_identifer();
    std::vector<antlr4::tree::TerminalNode *> ParOpen();
    antlr4::tree::TerminalNode* ParOpen(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ParClose();
    antlr4::tree::TerminalNode* ParClose(size_t i);
    std::vector<TermContext *> term();
    TermContext* term(size_t i);
    antlr4::tree::TerminalNode *GRW_Let();
    std::vector<Var_bindingContext *> var_binding();
    Var_bindingContext* var_binding(size_t i);
    antlr4::tree::TerminalNode *GRW_Forall();
    std::vector<Sorted_varContext *> sorted_var();
    Sorted_varContext* sorted_var(size_t i);
    antlr4::tree::TerminalNode *GRW_Exists();
    antlr4::tree::TerminalNode *GRW_Match();
    std::vector<Match_caseContext *> match_case();
    Match_caseContext* match_case(size_t i);
    antlr4::tree::TerminalNode *GRW_Exclamation();
    std::vector<AttributeContext *> attribute();
    AttributeContext* attribute(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TermContext* term();

  class  Sort_symbol_declContext : public antlr4::ParserRuleContext {
  public:
    Sort_symbol_declContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    IdentifierContext *identifier();
    NumeralContext *numeral();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<AttributeContext *> attribute();
    AttributeContext* attribute(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Sort_symbol_declContext* sort_symbol_decl();

  class  Meta_spec_constantContext : public antlr4::ParserRuleContext {
  public:
    Meta_spec_constantContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *GRW_Numeral();
    antlr4::tree::TerminalNode *GRW_Decimal();
    antlr4::tree::TerminalNode *GRW_String();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Meta_spec_constantContext* meta_spec_constant();

  class  Fun_symbol_declContext : public antlr4::ParserRuleContext {
  public:
    Fun_symbol_declContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    Spec_constantContext *spec_constant();
    std::vector<SortContext *> sort();
    SortContext* sort(size_t i);
    antlr4::tree::TerminalNode *ParClose();
    std::vector<AttributeContext *> attribute();
    AttributeContext* attribute(size_t i);
    Meta_spec_constantContext *meta_spec_constant();
    IdentifierContext *identifier();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Fun_symbol_declContext* fun_symbol_decl();

  class  Par_fun_symbol_declContext : public antlr4::ParserRuleContext {
  public:
    Par_fun_symbol_declContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Fun_symbol_declContext *fun_symbol_decl();
    std::vector<antlr4::tree::TerminalNode *> ParOpen();
    antlr4::tree::TerminalNode* ParOpen(size_t i);
    antlr4::tree::TerminalNode *GRW_Par();
    std::vector<antlr4::tree::TerminalNode *> ParClose();
    antlr4::tree::TerminalNode* ParClose(size_t i);
    IdentifierContext *identifier();
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);
    std::vector<SortContext *> sort();
    SortContext* sort(size_t i);
    std::vector<AttributeContext *> attribute();
    AttributeContext* attribute(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Par_fun_symbol_declContext* par_fun_symbol_decl();

  class  Theory_attributeContext : public antlr4::ParserRuleContext {
  public:
    Theory_attributeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PK_Sorts();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Sort_symbol_declContext *> sort_symbol_decl();
    Sort_symbol_declContext* sort_symbol_decl(size_t i);
    antlr4::tree::TerminalNode *PK_Funs();
    std::vector<Par_fun_symbol_declContext *> par_fun_symbol_decl();
    Par_fun_symbol_declContext* par_fun_symbol_decl(size_t i);
    antlr4::tree::TerminalNode *PK_SortsDescription();
    StringContext *string();
    antlr4::tree::TerminalNode *PK_FunsDescription();
    antlr4::tree::TerminalNode *PK_Definition();
    antlr4::tree::TerminalNode *PK_Values();
    antlr4::tree::TerminalNode *PK_Notes();
    AttributeContext *attribute();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Theory_attributeContext* theory_attribute();

  class  Theory_declContext : public antlr4::ParserRuleContext {
  public:
    Theory_declContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *PS_Theory();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Theory_attributeContext *> theory_attribute();
    Theory_attributeContext* theory_attribute(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Theory_declContext* theory_decl();

  class  Logic_attribueContext : public antlr4::ParserRuleContext {
  public:
    Logic_attribueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PK_Theories();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);
    antlr4::tree::TerminalNode *PK_Language();
    StringContext *string();
    antlr4::tree::TerminalNode *PK_Extension();
    antlr4::tree::TerminalNode *PK_Values();
    antlr4::tree::TerminalNode *PK_Notes();
    AttributeContext *attribute();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Logic_attribueContext* logic_attribue();

  class  LogicContext : public antlr4::ParserRuleContext {
  public:
    LogicContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *PS_Logic();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Logic_attribueContext *> logic_attribue();
    Logic_attribueContext* logic_attribue(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LogicContext* logic();

  class  Sort_decContext : public antlr4::ParserRuleContext {
  public:
    Sort_decContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    SymbolContext *symbol();
    NumeralContext *numeral();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Sort_decContext* sort_dec();

  class  Selector_decContext : public antlr4::ParserRuleContext {
  public:
    Selector_decContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    SymbolContext *symbol();
    SortContext *sort();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Selector_decContext* selector_dec();

  class  Constructor_decContext : public antlr4::ParserRuleContext {
  public:
    Constructor_decContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Selector_decContext *> selector_dec();
    Selector_decContext* selector_dec(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Constructor_decContext* constructor_dec();

  class  Datatype_decContext : public antlr4::ParserRuleContext {
  public:
    Datatype_decContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> ParOpen();
    antlr4::tree::TerminalNode* ParOpen(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ParClose();
    antlr4::tree::TerminalNode* ParClose(size_t i);
    std::vector<Constructor_decContext *> constructor_dec();
    Constructor_decContext* constructor_dec(size_t i);
    antlr4::tree::TerminalNode *GRW_Par();
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Datatype_decContext* datatype_dec();

  class  Function_decContext : public antlr4::ParserRuleContext {
  public:
    Function_decContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> ParOpen();
    antlr4::tree::TerminalNode* ParOpen(size_t i);
    SymbolContext *symbol();
    std::vector<antlr4::tree::TerminalNode *> ParClose();
    antlr4::tree::TerminalNode* ParClose(size_t i);
    SortContext *sort();
    std::vector<Sorted_varContext *> sorted_var();
    Sorted_varContext* sorted_var(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Function_decContext* function_dec();

  class  Function_defContext : public antlr4::ParserRuleContext {
  public:
    Function_defContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    SortContext *sort();
    TermContext *term();
    std::vector<Sorted_varContext *> sorted_var();
    Sorted_varContext* sorted_var(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Function_defContext* function_def();

  class  Prop_literalContext : public antlr4::ParserRuleContext {
  public:
    Prop_literalContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *PS_Not();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Prop_literalContext* prop_literal();

  class  ScriptContext : public antlr4::ParserRuleContext {
  public:
    ScriptContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<CommandContext *> command();
    CommandContext* command(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ScriptContext* script();

  class  Cmd_assertContext : public antlr4::ParserRuleContext {
  public:
    Cmd_assertContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_Assert();
    TermContext *term();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_assertContext* cmd_assert();

  class  Cmd_checkSatContext : public antlr4::ParserRuleContext {
  public:
    Cmd_checkSatContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_CheckSat();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_checkSatContext* cmd_checkSat();

  class  Cmd_checkSatAssumingContext : public antlr4::ParserRuleContext {
  public:
    Cmd_checkSatAssumingContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_CheckSatAssuming();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Prop_literalContext *> prop_literal();
    Prop_literalContext* prop_literal(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_checkSatAssumingContext* cmd_checkSatAssuming();

  class  Cmd_declareConstContext : public antlr4::ParserRuleContext {
  public:
    Cmd_declareConstContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DeclareConst();
    SymbolContext *symbol();
    SortContext *sort();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_declareConstContext* cmd_declareConst();

  class  Cmd_declareDatatypeContext : public antlr4::ParserRuleContext {
  public:
    Cmd_declareDatatypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DeclareDatatype();
    SymbolContext *symbol();
    Datatype_decContext *datatype_dec();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_declareDatatypeContext* cmd_declareDatatype();

  class  Cmd_declareDatatypesContext : public antlr4::ParserRuleContext {
  public:
    Cmd_declareDatatypesContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DeclareDatatypes();
    std::vector<antlr4::tree::TerminalNode *> ParOpen();
    antlr4::tree::TerminalNode* ParOpen(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ParClose();
    antlr4::tree::TerminalNode* ParClose(size_t i);
    std::vector<Sort_decContext *> sort_dec();
    Sort_decContext* sort_dec(size_t i);
    std::vector<Datatype_decContext *> datatype_dec();
    Datatype_decContext* datatype_dec(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_declareDatatypesContext* cmd_declareDatatypes();

  class  Cmd_declareFunContext : public antlr4::ParserRuleContext {
  public:
    Cmd_declareFunContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DeclareFun();
    SymbolContext *symbol();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<SortContext *> sort();
    SortContext* sort(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_declareFunContext* cmd_declareFun();

  class  Cmd_declareSortContext : public antlr4::ParserRuleContext {
  public:
    Cmd_declareSortContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DeclareSort();
    SymbolContext *symbol();
    NumeralContext *numeral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_declareSortContext* cmd_declareSort();

  class  Cmd_defineFunContext : public antlr4::ParserRuleContext {
  public:
    Cmd_defineFunContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DefineFun();
    Function_defContext *function_def();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_defineFunContext* cmd_defineFun();

  class  Cmd_defineFunRecContext : public antlr4::ParserRuleContext {
  public:
    Cmd_defineFunRecContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DefineFunRec();
    Function_defContext *function_def();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_defineFunRecContext* cmd_defineFunRec();

  class  Cmd_defineFunsRecContext : public antlr4::ParserRuleContext {
  public:
    Cmd_defineFunsRecContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DefineFunsRec();
    std::vector<antlr4::tree::TerminalNode *> ParOpen();
    antlr4::tree::TerminalNode* ParOpen(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ParClose();
    antlr4::tree::TerminalNode* ParClose(size_t i);
    std::vector<Function_decContext *> function_dec();
    Function_decContext* function_dec(size_t i);
    std::vector<TermContext *> term();
    TermContext* term(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_defineFunsRecContext* cmd_defineFunsRec();

  class  Cmd_defineSortContext : public antlr4::ParserRuleContext {
  public:
    Cmd_defineSortContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_DefineSort();
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    SortContext *sort();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_defineSortContext* cmd_defineSort();

  class  Cmd_echoContext : public antlr4::ParserRuleContext {
  public:
    Cmd_echoContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_Echo();
    StringContext *string();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_echoContext* cmd_echo();

  class  Cmd_exitContext : public antlr4::ParserRuleContext {
  public:
    Cmd_exitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_Exit();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_exitContext* cmd_exit();

  class  Cmd_getAssertionsContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getAssertionsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetAssertions();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getAssertionsContext* cmd_getAssertions();

  class  Cmd_getAssignmentContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getAssignmentContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetAssignment();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getAssignmentContext* cmd_getAssignment();

  class  Cmd_getInfoContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getInfoContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetInfo();
    Info_flagContext *info_flag();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getInfoContext* cmd_getInfo();

  class  Cmd_getModelContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getModelContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetModel();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getModelContext* cmd_getModel();

  class  Cmd_getOptionContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getOptionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetOption();
    KeywordContext *keyword();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getOptionContext* cmd_getOption();

  class  Cmd_getProofContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getProofContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetProof();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getProofContext* cmd_getProof();

  class  Cmd_getUnsatAssumptionsContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getUnsatAssumptionsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetUnsatAssumptions();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getUnsatAssumptionsContext* cmd_getUnsatAssumptions();

  class  Cmd_getUnsatCoreContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getUnsatCoreContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetUnsatCore();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getUnsatCoreContext* cmd_getUnsatCore();

  class  Cmd_getValueContext : public antlr4::ParserRuleContext {
  public:
    Cmd_getValueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_GetValue();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<TermContext *> term();
    TermContext* term(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_getValueContext* cmd_getValue();

  class  Cmd_popContext : public antlr4::ParserRuleContext {
  public:
    Cmd_popContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_Pop();
    NumeralContext *numeral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_popContext* cmd_pop();

  class  Cmd_pushContext : public antlr4::ParserRuleContext {
  public:
    Cmd_pushContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_Push();
    NumeralContext *numeral();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_pushContext* cmd_push();

  class  Cmd_resetContext : public antlr4::ParserRuleContext {
  public:
    Cmd_resetContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_Reset();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_resetContext* cmd_reset();

  class  Cmd_resetAssertionsContext : public antlr4::ParserRuleContext {
  public:
    Cmd_resetAssertionsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_ResetAssertions();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_resetAssertionsContext* cmd_resetAssertions();

  class  Cmd_setInfoContext : public antlr4::ParserRuleContext {
  public:
    Cmd_setInfoContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_SetInfo();
    AttributeContext *attribute();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_setInfoContext* cmd_setInfo();

  class  Cmd_setLogicContext : public antlr4::ParserRuleContext {
  public:
    Cmd_setLogicContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_SetLogic();
    SymbolContext *symbol();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_setLogicContext* cmd_setLogic();

  class  Cmd_setOptionContext : public antlr4::ParserRuleContext {
  public:
    Cmd_setOptionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CMD_SetOption();
    OptionContext *option();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Cmd_setOptionContext* cmd_setOption();

  class  CommandContext : public antlr4::ParserRuleContext {
  public:
    CommandContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    Cmd_assertContext *cmd_assert();
    antlr4::tree::TerminalNode *ParClose();
    Cmd_checkSatContext *cmd_checkSat();
    Cmd_checkSatAssumingContext *cmd_checkSatAssuming();
    Cmd_declareConstContext *cmd_declareConst();
    Cmd_declareDatatypeContext *cmd_declareDatatype();
    Cmd_declareDatatypesContext *cmd_declareDatatypes();
    Cmd_declareFunContext *cmd_declareFun();
    Cmd_declareSortContext *cmd_declareSort();
    Cmd_defineFunContext *cmd_defineFun();
    Cmd_defineFunRecContext *cmd_defineFunRec();
    Cmd_defineFunsRecContext *cmd_defineFunsRec();
    Cmd_defineSortContext *cmd_defineSort();
    Cmd_echoContext *cmd_echo();
    Cmd_exitContext *cmd_exit();
    Cmd_getAssertionsContext *cmd_getAssertions();
    Cmd_getAssignmentContext *cmd_getAssignment();
    Cmd_getInfoContext *cmd_getInfo();
    Cmd_getModelContext *cmd_getModel();
    Cmd_getOptionContext *cmd_getOption();
    Cmd_getProofContext *cmd_getProof();
    Cmd_getUnsatAssumptionsContext *cmd_getUnsatAssumptions();
    Cmd_getUnsatCoreContext *cmd_getUnsatCore();
    Cmd_getValueContext *cmd_getValue();
    Cmd_popContext *cmd_pop();
    Cmd_pushContext *cmd_push();
    Cmd_resetContext *cmd_reset();
    Cmd_resetAssertionsContext *cmd_resetAssertions();
    Cmd_setInfoContext *cmd_setInfo();
    Cmd_setLogicContext *cmd_setLogic();
    Cmd_setOptionContext *cmd_setOption();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CommandContext* command();

  class  B_valueContext : public antlr4::ParserRuleContext {
  public:
    B_valueContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PS_True();
    antlr4::tree::TerminalNode *PS_False();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  B_valueContext* b_value();

  class  OptionContext : public antlr4::ParserRuleContext {
  public:
    OptionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PK_DiagnosticOutputChannel();
    StringContext *string();
    antlr4::tree::TerminalNode *PK_GlobalDeclarations();
    B_valueContext *b_value();
    antlr4::tree::TerminalNode *PK_InteractiveMode();
    antlr4::tree::TerminalNode *PK_PrintSuccess();
    antlr4::tree::TerminalNode *PK_ProduceAssertions();
    antlr4::tree::TerminalNode *PK_ProduceAssignments();
    antlr4::tree::TerminalNode *PK_ProduceModels();
    antlr4::tree::TerminalNode *PK_ProduceProofs();
    antlr4::tree::TerminalNode *PK_ProduceUnsatAssumptions();
    antlr4::tree::TerminalNode *PK_ProduceUnsatCores();
    antlr4::tree::TerminalNode *PK_RandomSeed();
    NumeralContext *numeral();
    antlr4::tree::TerminalNode *PK_RegularOutputChannel();
    antlr4::tree::TerminalNode *PK_ReproducibleResourceLimit();
    antlr4::tree::TerminalNode *PK_Verbosity();
    AttributeContext *attribute();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  OptionContext* option();

  class  Info_flagContext : public antlr4::ParserRuleContext {
  public:
    Info_flagContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PK_AllStatistics();
    antlr4::tree::TerminalNode *PK_AssertionStackLevels();
    antlr4::tree::TerminalNode *PK_Authors();
    antlr4::tree::TerminalNode *PK_ErrorBehaviour();
    antlr4::tree::TerminalNode *PK_Name();
    antlr4::tree::TerminalNode *PK_ReasonUnknown();
    antlr4::tree::TerminalNode *PK_Version();
    KeywordContext *keyword();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Info_flagContext* info_flag();

  class  Error_behaviourContext : public antlr4::ParserRuleContext {
  public:
    Error_behaviourContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PS_ImmediateExit();
    antlr4::tree::TerminalNode *PS_ContinuedExecution();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Error_behaviourContext* error_behaviour();

  class  Reason_unknownContext : public antlr4::ParserRuleContext {
  public:
    Reason_unknownContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PS_Memout();
    antlr4::tree::TerminalNode *PS_Incomplete();
    S_exprContext *s_expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Reason_unknownContext* reason_unknown();

  class  Model_responseContext : public antlr4::ParserRuleContext {
  public:
    Model_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    Cmd_defineFunContext *cmd_defineFun();
    antlr4::tree::TerminalNode *ParClose();
    Cmd_defineFunRecContext *cmd_defineFunRec();
    Cmd_defineFunsRecContext *cmd_defineFunsRec();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Model_responseContext* model_response();

  class  Info_responseContext : public antlr4::ParserRuleContext {
  public:
    Info_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PK_AssertionStackLevels();
    NumeralContext *numeral();
    antlr4::tree::TerminalNode *PK_Authors();
    StringContext *string();
    antlr4::tree::TerminalNode *PK_ErrorBehaviour();
    Error_behaviourContext *error_behaviour();
    antlr4::tree::TerminalNode *PK_Name();
    antlr4::tree::TerminalNode *PK_ReasonUnknown();
    Reason_unknownContext *reason_unknown();
    antlr4::tree::TerminalNode *PK_Version();
    AttributeContext *attribute();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Info_responseContext* info_response();

  class  Valuation_pairContext : public antlr4::ParserRuleContext {
  public:
    Valuation_pairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    std::vector<TermContext *> term();
    TermContext* term(size_t i);
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Valuation_pairContext* valuation_pair();

  class  T_valuation_pairContext : public antlr4::ParserRuleContext {
  public:
    T_valuation_pairContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    SymbolContext *symbol();
    B_valueContext *b_value();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  T_valuation_pairContext* t_valuation_pair();

  class  Check_sat_responseContext : public antlr4::ParserRuleContext {
  public:
    Check_sat_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PS_Sat();
    antlr4::tree::TerminalNode *PS_Unsat();
    antlr4::tree::TerminalNode *PS_Unknown();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Check_sat_responseContext* check_sat_response();

  class  Echo_responseContext : public antlr4::ParserRuleContext {
  public:
    Echo_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    StringContext *string();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Echo_responseContext* echo_response();

  class  Get_assertions_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_assertions_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<TermContext *> term();
    TermContext* term(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_assertions_responseContext* get_assertions_response();

  class  Get_assignment_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_assignment_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<T_valuation_pairContext *> t_valuation_pair();
    T_valuation_pairContext* t_valuation_pair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_assignment_responseContext* get_assignment_response();

  class  Get_info_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_info_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Info_responseContext *> info_response();
    Info_responseContext* info_response(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_info_responseContext* get_info_response();

  class  Get_model_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_model_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *RS_Model();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Model_responseContext *> model_response();
    Model_responseContext* model_response(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_model_responseContext* get_model_response();

  class  Get_option_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_option_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Attribute_valueContext *attribute_value();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_option_responseContext* get_option_response();

  class  Get_proof_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_proof_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    S_exprContext *s_expr();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_proof_responseContext* get_proof_response();

  class  Get_unsat_assump_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_unsat_assump_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_unsat_assump_responseContext* get_unsat_assump_response();

  class  Get_unsat_core_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_unsat_core_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<SymbolContext *> symbol();
    SymbolContext* symbol(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_unsat_core_responseContext* get_unsat_core_response();

  class  Get_value_responseContext : public antlr4::ParserRuleContext {
  public:
    Get_value_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *ParClose();
    std::vector<Valuation_pairContext *> valuation_pair();
    Valuation_pairContext* valuation_pair(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Get_value_responseContext* get_value_response();

  class  Specific_success_responseContext : public antlr4::ParserRuleContext {
  public:
    Specific_success_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Check_sat_responseContext *check_sat_response();
    Echo_responseContext *echo_response();
    Get_assertions_responseContext *get_assertions_response();
    Get_assignment_responseContext *get_assignment_response();
    Get_info_responseContext *get_info_response();
    Get_model_responseContext *get_model_response();
    Get_option_responseContext *get_option_response();
    Get_proof_responseContext *get_proof_response();
    Get_unsat_assump_responseContext *get_unsat_assump_response();
    Get_unsat_core_responseContext *get_unsat_core_response();
    Get_value_responseContext *get_value_response();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  Specific_success_responseContext* specific_success_response();

  class  General_responseContext : public antlr4::ParserRuleContext {
  public:
    General_responseContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PS_Success();
    Specific_success_responseContext *specific_success_response();
    antlr4::tree::TerminalNode *PS_Unsupported();
    antlr4::tree::TerminalNode *ParOpen();
    antlr4::tree::TerminalNode *PS_Error();
    StringContext *string();
    antlr4::tree::TerminalNode *ParClose();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  General_responseContext* general_response();


  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

