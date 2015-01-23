/*
 * Copyright 2011-2015 by Emese Revfy <re.emese@gmail.com>
 * Licensed under the GPL v2, or (at your option) v3
 *
 * Homepage:
 * http://www.grsecurity.net/~ephox/overflow_plugin/
 *
 * Documentation:
 * http://forums.grsecurity.net/viewtopic.php?f=7&t=3043
 *
 * This plugin recomputes expressions of function arguments marked by a size_overflow attribute
 * with double integer precision (DImode/TImode for 32/64 bit integer types).
 * The recomputed argument is checked against TYPE_MAX and an event is logged on overflow and the triggering process is killed.
 *
 * Usage:
 * $ make
 * $ make run
 */

#include "size_overflow.h"

// Was the function created by the compiler itself?
bool made_by_compiler(const_tree decl)
{
	struct cgraph_node *node;

	gcc_assert(TREE_CODE(decl) == FUNCTION_DECL);
	if (DECL_ARTIFICIAL(decl))
		return true;

	node = get_cnode(decl);
	if (!node)
		return false;
	return node->clone_of != NULL;
}

bool skip_types(const_tree var)
{
	const_tree type;

	type = TREE_TYPE(var);
	if (type == NULL_TREE)
		return true;

	switch (TREE_CODE(type)) {
		case INTEGER_TYPE:
		case ENUMERAL_TYPE:
			return false;
		default:
			return true;
	}
}

gimple get_def_stmt(const_tree node)
{
	gcc_assert(node != NULL_TREE);

	if (TREE_CODE(node) != SSA_NAME)
		return NULL;
	return SSA_NAME_DEF_STMT(node);
}

tree create_new_var(tree type)
{
	tree new_var = create_tmp_var(type, "cicus");

	add_referenced_var(new_var);
	return new_var;
}

static bool skip_cast(tree dst_type, const_tree rhs, bool force)
{
	const_gimple def_stmt = get_def_stmt(rhs);

	if (force)
		return false;

	if (is_gimple_constant(rhs))
		return false;

	if (!def_stmt || gimple_code(def_stmt) == GIMPLE_NOP)
		return false;

	if (!types_compatible_p(dst_type, TREE_TYPE(rhs)))
		return false;

	// DI type can be on 32 bit (from create_assign) but overflow type stays DI
	if (LONG_TYPE_SIZE == GET_MODE_BITSIZE(SImode))
		return false;

	return true;
}

tree cast_a_tree(tree type, tree var)
{
	gcc_assert(type != NULL_TREE);
	gcc_assert(var != NULL_TREE);
	gcc_assert(fold_convertible_p(type, var));

	return fold_convert(type, var);
}

gimple build_cast_stmt(struct visited *visited, tree dst_type, tree rhs, tree lhs, gimple_stmt_iterator *gsi, bool before, bool force)
{
	gimple assign, def_stmt;

	gcc_assert(dst_type != NULL_TREE && rhs != NULL_TREE);
	gcc_assert(!is_gimple_constant(rhs));
	if (gsi_end_p(*gsi) && before == AFTER_STMT)
		gcc_unreachable();

	def_stmt = get_def_stmt(rhs);
	if (def_stmt && gimple_code(def_stmt) != GIMPLE_NOP && skip_cast(dst_type, rhs, force) && pointer_set_contains(visited->my_stmts, def_stmt))
		return def_stmt;

	if (lhs == CREATE_NEW_VAR)
		lhs = create_new_var(dst_type);

	assign = gimple_build_assign(lhs, cast_a_tree(dst_type, rhs));

	if (!gsi_end_p(*gsi)) {
		location_t loc = gimple_location(gsi_stmt(*gsi));
		gimple_set_location(assign, loc);
	}

	gimple_assign_set_lhs(assign, make_ssa_name(lhs, assign));

	if (before)
		gsi_insert_before(gsi, assign, GSI_NEW_STMT);
	else
		gsi_insert_after(gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);
	return assign;
}

bool is_size_overflow_type(const_tree var)
{
	const char *name;
	const_tree type_name, type;

	if (var == NULL_TREE)
		return false;

	type = TREE_TYPE(var);
	type_name = TYPE_NAME(type);
	if (type_name == NULL_TREE)
		return false;

	if (DECL_P(type_name))
		name = DECL_NAME_POINTER(type_name);
	else
		name = IDENTIFIER_POINTER(type_name);

	if (!strncmp(name, "size_overflow_type", 18))
		return true;
	return false;
}

// Determine if a cloned function has all the original arguments
static bool unchanged_arglist(struct cgraph_node *new_node)
{
	if (!new_node->clone_of || !new_node->clone.tree_map)
		return true;
	return !new_node->clone.args_to_skip;
}

// Find the specified argument in the originally cloned function
static unsigned int clone_argnum_on_orig(struct cgraph_node *new_node, unsigned int clone_argnum)
{
	bitmap args_to_skip;
	unsigned int i, new_argnum = clone_argnum;

	if (unchanged_arglist(new_node))
		return clone_argnum;

	args_to_skip = new_node->clone.args_to_skip;
	for (i = 0; i < clone_argnum; i++) {
		if (bitmap_bit_p(args_to_skip, i))
			new_argnum++;
	}
	return new_argnum;
}

// Find the specified argument in the clone
static unsigned int orig_argnum_on_clone(struct cgraph_node *new_node, unsigned int orig_argnum)
{
	bitmap args_to_skip;
	unsigned int i, new_argnum = orig_argnum;

	if (unchanged_arglist(new_node))
		return orig_argnum;

	args_to_skip = new_node->clone.args_to_skip;
	if (bitmap_bit_p(args_to_skip, orig_argnum - 1))
		// XXX torolni kellene a nodeot
		return CANNOT_FIND_ARG;

	for (i = 0; i < orig_argnum; i++) {
		if (bitmap_bit_p(args_to_skip, i))
			new_argnum--;
	}
	return new_argnum;
}

// Associate the argument between a clone and a cloned function
unsigned int get_correct_argnum(struct cgraph_node *node, struct cgraph_node *correct_argnum_of_node, unsigned int argnum)
{
	bool node_clone, correct_argnum_of_node_clone;

	gcc_assert(correct_argnum_of_node);

	if (node == correct_argnum_of_node)
		return argnum;
	if (argnum == 0)
		return argnum;

	if (node)
		node_clone = made_by_compiler(NODE_DECL(node));
	else
		node_clone = false;

	correct_argnum_of_node_clone = made_by_compiler(NODE_DECL(correct_argnum_of_node));
	// the original decl is lost if both nodes are clones
	if (node_clone && correct_argnum_of_node_clone) {
		gcc_assert(unchanged_arglist(node));
		return argnum;
	}

	if (node_clone && !correct_argnum_of_node_clone)
		return clone_argnum_on_orig(correct_argnum_of_node, argnum);
	else if (!node_clone && correct_argnum_of_node_clone)
		return orig_argnum_on_clone(correct_argnum_of_node, argnum);
	return argnum;
}

// Find the original cloned function in our own database because gcc can lose this information
static tree get_orig_decl_from_global_next_interesting_function(const_tree clone_fndecl)
{
	next_interesting_function_t next_node;

	next_node = get_next_interesting_function(global_next_interesting_function_head, clone_fndecl, NONE_ARGNUM, NO_SO_MARK);

	if (!next_node)
		return NULL_TREE;
	if (next_node->orig_next_node)
		return next_node->orig_next_node->decl;
	return next_node->decl;
}

// Find the original cloned function
tree get_orig_fndecl(const_tree clone_fndecl)
{
	tree orig_fndecl;
	struct cgraph_node *node = get_cnode(clone_fndecl);

	if (!node) {
		orig_fndecl = get_orig_decl_from_global_next_interesting_function(clone_fndecl);
		if (orig_fndecl != NULL_TREE)
			return orig_fndecl;
		return (tree)clone_fndecl;
	}

	if (!made_by_compiler(clone_fndecl))
		return (tree)clone_fndecl;

	orig_fndecl = (tree)DECL_ORIGIN(clone_fndecl);
	if (!made_by_compiler(orig_fndecl))
		return orig_fndecl;

	while (node->clone_of)
		node = node->clone_of;
	if (!made_by_compiler(NODE_DECL(node)))
		return NODE_DECL(node);

	while (node) {
		orig_fndecl = get_orig_decl_from_global_next_interesting_function(clone_fndecl);
		if (orig_fndecl != NULL_TREE)
			return orig_fndecl;
		node = node->clone_of;
	}

	orig_fndecl = get_orig_decl_from_global_next_interesting_function_by_str(clone_fndecl);
	if (orig_fndecl != NULL_TREE)
		return orig_fndecl;

	return (tree)clone_fndecl;
}

static tree get_interesting_fndecl_from_stmt(const_gimple stmt)
{
	if (gimple_call_num_args(stmt) == 0)
		return NULL_TREE;
	return gimple_call_fndecl(stmt);
}

tree get_interesting_orig_fndecl_from_stmt(const_gimple stmt)
{
	tree fndecl;

	fndecl = get_interesting_fndecl_from_stmt(stmt);
	if (fndecl == NULL_TREE)
		return NULL_TREE;
	return get_orig_fndecl(fndecl);
}

void set_dominance_info(void)
{
	calculate_dominance_info(CDI_DOMINATORS);
	calculate_dominance_info(CDI_POST_DOMINATORS);
}

void unset_dominance_info(void)
{
	free_dominance_info(CDI_DOMINATORS);
	free_dominance_info(CDI_POST_DOMINATORS);
}

void set_current_function_decl(tree fndecl)
{
	gcc_assert(fndecl != NULL_TREE);

	push_cfun(DECL_STRUCT_FUNCTION(fndecl));
#if BUILDING_GCC_VERSION <= 4007
	current_function_decl = fndecl;
#endif
	set_dominance_info();
}

void unset_current_function_decl(void)
{
	unset_dominance_info();
#if BUILDING_GCC_VERSION <= 4007
	current_function_decl = NULL_TREE;
#endif
	pop_cfun();
}

bool is_valid_cgraph_node(struct cgraph_node *node)
{
	if (cgraph_function_body_availability(node) == AVAIL_NOT_AVAILABLE)
		return false;
	if (node->thunk.thunk_p || node->alias)
		return false;
	return true;
}

