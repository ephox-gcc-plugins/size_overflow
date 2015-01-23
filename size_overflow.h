#ifndef SIZE_OVERFLOW_H
#define SIZE_OVERFLOW_H

#define CREATE_NEW_VAR NULL_TREE
#define MAX_PARAM 31
#define CANNOT_FIND_ARG 32
#define NONE_ARGNUM 32

#define BEFORE_STMT true
#define AFTER_STMT false

#define TURN_OFF_ASM_STR "# size_overflow MARK_TURN_OFF "
#define YES_ASM_STR "# size_overflow MARK_YES "
#define END_INTENTIONAL_ASM_STR "# size_overflow MARK_END_INTENTIONAL "
#define SO_ASM_STR "# size_overflow "
#define OK_ASM_STR "# size_overflow MARK_NO"

#include "gcc-common.h"

#include <string.h>

enum intentional_mark {
	MARK_NO, MARK_YES, MARK_END_INTENTIONAL, MARK_TURN_OFF
};

enum intentional_overflow_type {
	NO_INTENTIONAL_OVERFLOW, RHS1_INTENTIONAL_OVERFLOW, RHS2_INTENTIONAL_OVERFLOW
};

enum size_overflow_mark {
	NO_SO_MARK, YES_SO_MARK, ASM_STMT_SO_MARK
};


struct visited {
	struct pointer_set_t *stmts;
	struct pointer_set_t *my_stmts;
	struct pointer_set_t *skip_expr_casts;
	struct pointer_set_t *no_cast_check;
};

struct next_interesting_function;
typedef struct next_interesting_function *  next_interesting_function_t;

#if BUILDING_GCC_VERSION <= 4007
DEF_VEC_P(next_interesting_function_t);
DEF_VEC_ALLOC_P(next_interesting_function_t, heap);
#endif

/*
 *  * children: callers with data flow into the integer parameter of decl
 *  * decl: function with an integer parameter or integer return
 *  * num: parameter number (1-31) or return value (0)
 *  * marked: meg van-e jelolve a fuggveny, ha nem asm_stmt-n dependal a decl, akkor az execute-ben lesz csak allitva, a marked_fn()-ben
 *  * orig_next_node: ha a decl egy clone, akkor ide kerul az eredeti deklaracioja (ha az eredeti node torolve van, akkor ez nem lesz orig_next_node, de bekerul a hash tablaba, ugy hogy levagjuk a declname klonozott reszet)
 */
struct next_interesting_function {
	next_interesting_function_t next;
#if BUILDING_GCC_VERSION <= 4007
	VEC(next_interesting_function_t, heap) *children;
#else
	vec<next_interesting_function_t, va_heap, vl_embed> *children;
#endif
	tree decl;
	unsigned int num;
	enum size_overflow_mark marked;
	next_interesting_function_t orig_next_node;
};

// size_overflow_plugin.c
extern tree report_size_overflow_decl;
extern tree size_overflow_type_HI;
extern tree size_overflow_type_SI;
extern tree size_overflow_type_DI;
extern tree size_overflow_type_TI;


// size_overflow_plugin_hash.c
struct size_overflow_hash {
	const struct size_overflow_hash * const next;
	const char * const name;
	const unsigned int param;
};

extern bool is_size_overflow_asm(const_gimple stmt);
extern void print_missing_function(next_interesting_function_t node);
extern const struct size_overflow_hash *get_function_hash(const_tree fndecl);
extern unsigned int find_arg_number_tree(const_tree arg, const_tree func);


// intentional_overflow.c
extern tree get_size_overflow_asm_input(const_gimple stmt);
extern enum intentional_mark check_intentional_asm(const_gimple stmt, unsigned int argnum);
extern bool is_size_overflow_insert_check_asm(const_gimple stmt);
extern enum intentional_mark check_intentional_attribute(const_gimple stmt, unsigned int argnum);
extern enum intentional_mark get_so_asm_type(const_gimple stmt);
extern const_tree get_attribute(const char* attr_name, const_tree decl);
extern bool is_a_cast_and_const_overflow(const_tree no_const_rhs);
extern bool is_const_plus_unsigned_signed_truncation(const_tree lhs);
extern bool is_a_constant_overflow(const_gimple stmt, const_tree rhs);
extern tree handle_intentional_overflow(struct visited *visited, bool check_overflow, gimple stmt, tree change_rhs, tree new_rhs2);
extern tree handle_integer_truncation(struct visited *visited, const_tree lhs);
extern bool is_a_neg_overflow(const_gimple stmt, const_tree rhs);
extern enum intentional_overflow_type add_mul_intentional_overflow(const_gimple def_stmt);
extern void unsigned_signed_cast_intentional_overflow(struct visited *visited, gimple stmt);


// insert_size_overflow_asm.c
#if BUILDING_GCC_VERSION >= 4009
extern opt_pass *make_insert_size_overflow_asm_pass(void);
#else
extern struct opt_pass *make_insert_size_overflow_asm_pass(void);
#endif
extern bool search_interesting_args(tree fndecl, bool *argnums);


// misc.c
extern void set_dominance_info(void);
extern void unset_dominance_info(void);
extern tree get_interesting_orig_fndecl_from_stmt(const_gimple stmt);
extern tree get_orig_fndecl(const_tree clone_fndecl);
extern unsigned int get_correct_argnum(struct cgraph_node *node, struct cgraph_node *correct_argnum_of_node, unsigned int argnum);
extern bool is_valid_cgraph_node(struct cgraph_node *node);
extern void set_current_function_decl(tree fndecl);
extern void unset_current_function_decl(void);
extern gimple get_def_stmt(const_tree node);
extern tree create_new_var(tree type);
extern gimple build_cast_stmt(struct visited *visited, tree dst_type, tree rhs, tree lhs, gimple_stmt_iterator *gsi, bool before, bool force);
extern bool skip_types(const_tree var);
extern tree cast_a_tree(tree type, tree var);
extern bool is_size_overflow_type(const_tree var);
extern bool made_by_compiler(const_tree decl);


// size_overflow_transform.c
extern unsigned int size_overflow_transform(struct cgraph_node *node);


// size_overflow_transform_core.c
extern tree expand(struct visited *visited, tree lhs);
extern void check_size_overflow(gimple stmt, tree size_overflow_type, tree cast_rhs, tree rhs, bool before);
extern tree dup_assign(struct visited *visited, gimple oldstmt, const_tree node, tree rhs1, tree rhs2, tree __unused rhs3);
extern tree create_assign(struct visited *visited, gimple oldstmt, tree rhs1, bool before);


// remove_unnecessary_dup.c
extern struct opt_pass *make_remove_unnecessary_dup_pass(void);
extern void insert_cast_expr(struct visited *visited, gimple stmt, enum intentional_overflow_type type);
extern bool skip_expr_on_double_type(const_gimple stmt);
extern void create_up_and_down_cast(struct visited *visited, gimple use_stmt, tree orig_type, tree rhs);


// size_overflow_ipa.c
extern struct cgraph_node *get_cnode(const_tree fndecl);
extern next_interesting_function_t global_next_interesting_function_head;
extern next_interesting_function_t get_next_interesting_function(next_interesting_function_t head, const_tree decl, unsigned int num, enum size_overflow_mark marked);
extern void size_overflow_register_hooks(void);
#if BUILDING_GCC_VERSION >= 4009
extern opt_pass *make_size_overflow_functions_pass(void);
extern opt_pass *make_size_overflow_free_pass(void);
#else
extern struct opt_pass *make_size_overflow_functions_pass(void);
extern struct opt_pass *make_size_overflow_free_pass(void);
#endif
extern void size_overflow_node_removal_hook(struct cgraph_node *node, void *data);
extern tree get_orig_decl_from_global_next_interesting_function_by_str(const_tree clone_fndecl);
extern next_interesting_function_t get_and_create_next_node_from_global_next_nodes(tree decl, unsigned int num, enum size_overflow_mark marked, next_interesting_function_t orig_next_node);


// size_overflow_lto.c
extern void size_overflow_read_summary_lto(void);
#if BUILDING_GCC_VERSION >= 4008
extern void size_overflow_write_summary_lto(void);
extern void size_overflow_write_optimization_summary_lto(void);
#elif BUILDING_GCC_VERSION >= 4006
extern void size_overflow_write_summary_lto(cgraph_node_set set, varpool_node_set vset);
extern void size_overflow_write_optimization_summary_lto(cgraph_node_set set, varpool_node_set vset);
#else
extern void size_overflow_write_summary_lto(cgraph_node_set set);
extern void size_overflow_write_optimization_summary_lto(cgraph_node_set set);
#endif


// size_overflow_debug.c
extern void __unused print_intentional_mark(enum intentional_mark mark);
extern unsigned int __unused size_overflow_dump_function(FILE *file, struct cgraph_node *node);
extern void __unused print_next_interesting_functions(next_interesting_function_t head, bool only_this);
extern void __unused print_children_chain_list(next_interesting_function_t next_node);
extern void __unused print_all_next_node_children_chain_list(next_interesting_function_t next_node);
extern const char * __unused print_so_mark_name(enum size_overflow_mark mark);

#endif
