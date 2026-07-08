
// Generated from SMTLIBv2.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "SMTLIBv2Visitor.h"


/**
 * This class provides an empty implementation of SMTLIBv2Visitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  SMTLIBv2BaseVisitor : public SMTLIBv2Visitor {
public:

  virtual std::any visitStart_(SMTLIBv2Parser::Start_Context *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGeneralReservedWord(SMTLIBv2Parser::GeneralReservedWordContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSimpleSymbol(SMTLIBv2Parser::SimpleSymbolContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQuotedSymbol(SMTLIBv2Parser::QuotedSymbolContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPredefSymbol(SMTLIBv2Parser::PredefSymbolContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPredefKeyword(SMTLIBv2Parser::PredefKeywordContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSymbol(SMTLIBv2Parser::SymbolContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNumeral(SMTLIBv2Parser::NumeralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDecimal(SMTLIBv2Parser::DecimalContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitHexadecimal(SMTLIBv2Parser::HexadecimalContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBinary(SMTLIBv2Parser::BinaryContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitString(SMTLIBv2Parser::StringContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitKeyword(SMTLIBv2Parser::KeywordContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSpec_constant(SMTLIBv2Parser::Spec_constantContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitS_expr(SMTLIBv2Parser::S_exprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIndex(SMTLIBv2Parser::IndexContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentifier(SMTLIBv2Parser::IdentifierContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAttribute_value(SMTLIBv2Parser::Attribute_valueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAttribute(SMTLIBv2Parser::AttributeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSort(SMTLIBv2Parser::SortContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQual_identifer(SMTLIBv2Parser::Qual_identiferContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVar_binding(SMTLIBv2Parser::Var_bindingContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSorted_var(SMTLIBv2Parser::Sorted_varContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPattern(SMTLIBv2Parser::PatternContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMatch_case(SMTLIBv2Parser::Match_caseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTerm(SMTLIBv2Parser::TermContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSort_symbol_decl(SMTLIBv2Parser::Sort_symbol_declContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMeta_spec_constant(SMTLIBv2Parser::Meta_spec_constantContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFun_symbol_decl(SMTLIBv2Parser::Fun_symbol_declContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPar_fun_symbol_decl(SMTLIBv2Parser::Par_fun_symbol_declContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTheory_attribute(SMTLIBv2Parser::Theory_attributeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTheory_decl(SMTLIBv2Parser::Theory_declContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLogic_attribue(SMTLIBv2Parser::Logic_attribueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLogic(SMTLIBv2Parser::LogicContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSort_dec(SMTLIBv2Parser::Sort_decContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSelector_dec(SMTLIBv2Parser::Selector_decContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstructor_dec(SMTLIBv2Parser::Constructor_decContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDatatype_dec(SMTLIBv2Parser::Datatype_decContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFunction_dec(SMTLIBv2Parser::Function_decContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFunction_def(SMTLIBv2Parser::Function_defContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitProp_literal(SMTLIBv2Parser::Prop_literalContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitScript(SMTLIBv2Parser::ScriptContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_assert(SMTLIBv2Parser::Cmd_assertContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_checkSat(SMTLIBv2Parser::Cmd_checkSatContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_checkSatAssuming(SMTLIBv2Parser::Cmd_checkSatAssumingContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_declareConst(SMTLIBv2Parser::Cmd_declareConstContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_declareDatatype(SMTLIBv2Parser::Cmd_declareDatatypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_declareDatatypes(SMTLIBv2Parser::Cmd_declareDatatypesContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_declareFun(SMTLIBv2Parser::Cmd_declareFunContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_declareSort(SMTLIBv2Parser::Cmd_declareSortContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_defineFun(SMTLIBv2Parser::Cmd_defineFunContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_defineFunRec(SMTLIBv2Parser::Cmd_defineFunRecContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_defineFunsRec(SMTLIBv2Parser::Cmd_defineFunsRecContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_defineSort(SMTLIBv2Parser::Cmd_defineSortContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_echo(SMTLIBv2Parser::Cmd_echoContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_exit(SMTLIBv2Parser::Cmd_exitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getAssertions(SMTLIBv2Parser::Cmd_getAssertionsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getAssignment(SMTLIBv2Parser::Cmd_getAssignmentContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getInfo(SMTLIBv2Parser::Cmd_getInfoContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getModel(SMTLIBv2Parser::Cmd_getModelContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getOption(SMTLIBv2Parser::Cmd_getOptionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getProof(SMTLIBv2Parser::Cmd_getProofContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getUnsatAssumptions(SMTLIBv2Parser::Cmd_getUnsatAssumptionsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getUnsatCore(SMTLIBv2Parser::Cmd_getUnsatCoreContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_getValue(SMTLIBv2Parser::Cmd_getValueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_pop(SMTLIBv2Parser::Cmd_popContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_push(SMTLIBv2Parser::Cmd_pushContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_reset(SMTLIBv2Parser::Cmd_resetContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_resetAssertions(SMTLIBv2Parser::Cmd_resetAssertionsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_setInfo(SMTLIBv2Parser::Cmd_setInfoContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_setLogic(SMTLIBv2Parser::Cmd_setLogicContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCmd_setOption(SMTLIBv2Parser::Cmd_setOptionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCommand(SMTLIBv2Parser::CommandContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitB_value(SMTLIBv2Parser::B_valueContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOption(SMTLIBv2Parser::OptionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInfo_flag(SMTLIBv2Parser::Info_flagContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitError_behaviour(SMTLIBv2Parser::Error_behaviourContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReason_unknown(SMTLIBv2Parser::Reason_unknownContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitModel_response(SMTLIBv2Parser::Model_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInfo_response(SMTLIBv2Parser::Info_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitValuation_pair(SMTLIBv2Parser::Valuation_pairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitT_valuation_pair(SMTLIBv2Parser::T_valuation_pairContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCheck_sat_response(SMTLIBv2Parser::Check_sat_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEcho_response(SMTLIBv2Parser::Echo_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_assertions_response(SMTLIBv2Parser::Get_assertions_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_assignment_response(SMTLIBv2Parser::Get_assignment_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_info_response(SMTLIBv2Parser::Get_info_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_model_response(SMTLIBv2Parser::Get_model_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_option_response(SMTLIBv2Parser::Get_option_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_proof_response(SMTLIBv2Parser::Get_proof_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_unsat_assump_response(SMTLIBv2Parser::Get_unsat_assump_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_unsat_core_response(SMTLIBv2Parser::Get_unsat_core_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGet_value_response(SMTLIBv2Parser::Get_value_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSpecific_success_response(SMTLIBv2Parser::Specific_success_responseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGeneral_response(SMTLIBv2Parser::General_responseContext *ctx) override {
    return visitChildren(ctx);
  }


};

