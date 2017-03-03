//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_select.h"

#include <functional>

#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

PTValues::PTValues(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   PTExprListNode::SharedPtr tuple)
    : PTCollection(memctx, loc),
      tuples_(memctx, loc) {
  Append(tuple);
}

PTValues::~PTValues() {
}

void PTValues::Append(const PTExprListNode::SharedPtr& tuple) {
  tuples_.Append(tuple);
}

void PTValues::Prepend(const PTExprListNode::SharedPtr& tuple) {
  tuples_.Prepend(tuple);
}

CHECKED_STATUS PTValues::Analyze(SemContext *sem_context) {
  return Status::OK();
}

void PTValues::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

PTExprListNode::SharedPtr PTValues::Tuple(int index) const {
  DCHECK_GE(index, 0);
  return tuples_.element(index);
}

//--------------------------------------------------------------------------------------------------

PTSelectStmt::PTSelectStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTListNode::SharedPtr target,
                           PTTableRefListNode::SharedPtr from_clause,
                           PTExpr::SharedPtr where_clause,
                           PTListNode::SharedPtr group_by_clause,
                           PTListNode::SharedPtr having_clause,
                           PTListNode::SharedPtr order_by_clause,
                           PTExpr::SharedPtr limit_clause)
    : PTDmlStmt(memctx, loc, false),
      target_(target),
      from_clause_(from_clause),
      where_clause_(where_clause),
      group_by_clause_(group_by_clause),
      having_clause_(having_clause),
      order_by_clause_(order_by_clause),
      limit_clause_(limit_clause),
      selected_columns_(memctx) {
}

PTSelectStmt::~PTSelectStmt() {
}

CHECKED_STATUS PTSelectStmt::Analyze(SemContext *sem_context) {
  // Get the table descriptor.
  if (from_clause_->size() > 1) {
    return sem_context->Error(from_clause_->loc(), "Only one selected table is allowed",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  RETURN_NOT_OK(from_clause_->Analyze(sem_context));
  if (is_system()) {
    return Status::OK();
  }

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  // Run error checking on the select list 'target_'.
  // Check that all targets are valid references to table columns.
  selected_columns_.reserve(num_columns());
  TreeNodePtrOperator<SemContext> analyze = std::bind(&PTSelectStmt::AnalyzeTarget,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2);
  RETURN_NOT_OK(target_->Analyze(sem_context, analyze));

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  return Status::OK();
}

// TODO(Mihnea) Some where in this function, we must call expr->Analyze() even if it is a const.
CHECKED_STATUS PTSelectStmt::AnalyzeTarget(TreeNode *target, SemContext *sem_context) {
  // Walking through the target expressions and collect all columns. Currently, CQL doesn't allow
  // any expression except for references to table column.
  if (target->opcode() != TreeNodeOpcode::kPTRef) {
    return sem_context->Error(target->loc(), "Selecting expression is not allowed in CQL",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  PTRef *ref = static_cast<PTRef *>(target);
  RETURN_NOT_OK(ref->Analyze(sem_context));

  // Add the column descriptor to select_list_.
  const ColumnDesc *col_desc = ref->desc();
  if (col_desc == nullptr) {
    // This ref is pointing to the whole table (SELECT *).
    if (target_->size() != 1) {
      return sem_context->Error(target->loc(), "Selecting '*' is not allowed in this context",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
    int num_cols = num_columns();
    for (int idx = 0; idx < num_cols; idx++) {
      selected_columns_.push_back(&table_columns_[idx]);
    }
  } else {
    selected_columns_.push_back(col_desc);
  }

  return Status::OK();
}

void PTSelectStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTOrderBy::PTOrderBy(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const PTExpr::SharedPtr& name,
                     const Direction direction,
                     const NullPlacement null_placement)
  : TreeNode(memctx, loc),
    name_(name),
    direction_(direction),
    null_placement_(null_placement) {
}

PTOrderBy::~PTOrderBy() {
}

//--------------------------------------------------------------------------------------------------

PTTableRef::PTTableRef(MemoryContext *memctx,
                       YBLocation::SharedPtr loc,
                       const PTQualifiedName::SharedPtr& name,
                       MCString::SharedPtr alias)
    : TreeNode(memctx, loc),
      name_(name),
      alias_(alias) {
}

PTTableRef::~PTTableRef() {
}

CHECKED_STATUS PTTableRef::Analyze(SemContext *sem_context) {
  if (alias_ != nullptr) {
    return sem_context->Error(loc(), "Alias is not allowed", ErrorCode::CQL_STATEMENT_INVALID);
  }
  return name_->Analyze(sem_context);
}

//--------------------------------------------------------------------------------------------------

}  // namespace sql
}  // namespace yb
