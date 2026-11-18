#include "asn1fix_internal.h"

typedef struct resolver_arg {
	asn1p_expr_t	  *(*resolver)(asn1p_expr_t *, void *arg);
	arg_t		  *arg;
	asn1p_expr_t	  *original_expr;
	asn1p_paramlist_t *lhs_params;
	asn1p_expr_t	  *rhs_pspecs;
} resolver_arg_t;

static asn1p_expr_t *resolve_expr(asn1p_expr_t *, void *resolver_arg);
static int compare_specializations(const asn1p_expr_t *a, const asn1p_expr_t *b);
static asn1p_expr_t *find_target_specialization_byvalueset(resolver_arg_t *rarg, asn1p_constraint_t *ct);
static asn1p_expr_t *find_target_specialization_byref(resolver_arg_t *rarg, asn1p_ref_t *ref);
static asn1p_expr_t *find_target_specialization_bystr(resolver_arg_t *rarg, char *str);

asn1p_expr_t *
asn1f_parameterization_fork(arg_t *arg, asn1p_expr_t *expr, asn1p_expr_t *rhs_pspecs) {
	resolver_arg_t rarg;	/* resolver argument */
	asn1p_expr_t *exc;	/* expr clone */
	asn1p_expr_t *rpc;	/* rhs_pspecs clone */
	asn1p_expr_t *m;	/* expr members */
	asn1p_expr_t *target;
	void *p;
	struct asn1p_pspec_s *pspec;
	int npspecs;

	assert(rhs_pspecs);
	assert(expr->lhs_params);
	assert(expr->parent_expr == 0);

	/*
	 * Find if this exact specialization has been used already.
	 */
	for(npspecs = 0;
		npspecs < expr->specializations.pspecs_count;
			npspecs++) {
		if(compare_specializations(rhs_pspecs,
			expr->specializations.pspec[npspecs].rhs_pspecs) == 0) {
			DEBUG("Reused parameterization for %s",
				expr->Identifier);
			return expr->specializations.pspec[npspecs].my_clone;
		}
	}

	DEBUG("Forking parameterization at %d for %s (%d alr)",
		rhs_pspecs->_lineno, expr->Identifier,
		expr->specializations.pspecs_count);

	rarg.resolver = resolve_expr;
	rarg.arg = arg;
	rarg.original_expr = expr;
	rarg.lhs_params = expr->lhs_params;
	rarg.rhs_pspecs = rhs_pspecs;
	exc = asn1p_expr_clone_with_resolver(expr, resolve_expr, &rarg);
	if(!exc) return NULL;
	rpc = asn1p_expr_clone(rhs_pspecs, 0);
	assert(rpc);

	/*
	 * Create a new specialization.
	 */
	npspecs = expr->specializations.pspecs_count;
	p = realloc(expr->specializations.pspec,
			(npspecs + 1) * sizeof(expr->specializations.pspec[0]));
	assert(p);
	expr->specializations.pspec = p;
	pspec = &expr->specializations.pspec[npspecs];
	memset(pspec, 0, sizeof *pspec);

	pspec->rhs_pspecs = rpc;
	pspec->my_clone = exc;
	exc->spec_index = npspecs;
	asn1p_expr_free(exc->rhs_pspecs);	/* replaced below */
	exc->rhs_pspecs = asn1p_expr_clone_with_resolver(expr->rhs_pspecs ?
					expr->rhs_pspecs : rhs_pspecs,
					resolve_expr, &rarg);

	/* Passing arguments to members and type references */

	target = TQ_FIRST(&expr->members);
	TQ_FOR(m, &exc->members, next) {
		asn1p_expr_free(m->rhs_pspecs);	/* replaced below */
		m->rhs_pspecs = asn1p_expr_clone_with_resolver(target->rhs_pspecs ?
						target->rhs_pspecs : exc->rhs_pspecs,
						resolve_expr, &rarg);
		target = TQ_NEXT(target, next);
	}

	DEBUG("Forked new parameterization for %s", expr->Identifier);

	/* Commit */
	expr->specializations.pspecs_count = npspecs + 1;
	return exc;
}

static int
compare_specializations(const asn1p_expr_t *a, const asn1p_expr_t *b) {
    return asn1p_expr_compare(a, b);
}

static asn1p_expr_t *
resolve_expr(asn1p_expr_t *expr_to_resolve, void *resolver_arg) {
	resolver_arg_t *rarg = resolver_arg;
	arg_t *arg = rarg->arg;
	asn1p_expr_t *expr;
	asn1p_expr_t *nex;

	DEBUG("Resolving %s (meta %d)",
		expr_to_resolve->Identifier, expr_to_resolve->meta_type);

	/*
	 * A class field reference through a dummy class parameter, e.g.
	 * "ALGORITHM-TYPE.&id" in
	 *   AlgorithmIdentifier{ALGORITHM-TYPE, ALGORITHM-TYPE:Set} ::= ...
	 * Replace the dummy by the actual class reference ("ALG.&id"), so
	 * that the specialization no longer depends on the dummy name and
	 * can be fixed in any order.
	 */
	if(expr_to_resolve->meta_type == AMT_TYPEREF
	&& expr_to_resolve->reference
	&& expr_to_resolve->reference->comp_count > 1
	&& expr_to_resolve->reference->components[1].name[0] == '&') {
		asn1p_ref_t *ref = expr_to_resolve->reference;
		asn1p_ref_t *nref;
		size_t i;

		errno = 0;
		expr = find_target_specialization_bystr(rarg,
				ref->components[0].name);
		if(!expr) {
			if(errno != ESRCH) return NULL;
			/* Not a dummy parameter: clone as usual */
			errno = ESRCH;
			return NULL;
		}
		if(expr->meta_type != AMT_TYPEREF || !expr->reference
		|| expr->reference->comp_count != 1) {
			FATAL("Parameterization of %s: actual parameter for %s "
				"at line %d must be a class reference",
				rarg->original_expr->Identifier,
				ref->components[0].name, ref->_lineno);
			errno = EPERM;
			return NULL;
		}

		nref = asn1p_ref_clone(expr->reference);
		if(!nref) return NULL;
		if(!nref->module) nref->module = expr->module;
		for(i = 1; i < ref->comp_count; i++) {
			if(asn1p_ref_add_component(nref, ref->components[i].name,
					ref->components[i].lex_type)) {
				asn1p_ref_free(nref);
				return NULL;
			}
		}

		nex = asn1p_expr_new(expr_to_resolve->_lineno,
				expr_to_resolve->module);
		if(!nex) {
			asn1p_ref_free(nref);
			return NULL;
		}
		nex->meta_type = expr_to_resolve->meta_type;
		nex->expr_type = expr_to_resolve->expr_type;
		nex->reference = nref;
		nex->Identifier = expr_to_resolve->Identifier
			? strdup(expr_to_resolve->Identifier) : 0;
		DEBUG("Class field reference %s replaced by %s",
			asn1p_ref_string(ref), asn1p_ref_string(nref));
		return nex;
	}

	if(expr_to_resolve->meta_type == AMT_TYPEREF) {
		expr = find_target_specialization_byref(rarg,
				expr_to_resolve->reference);
		if(!expr) return NULL;
	} else if(expr_to_resolve->meta_type == AMT_VALUE) {
		if(!expr_to_resolve->value) return NULL;
		expr = find_target_specialization_bystr(rarg,
				expr_to_resolve->Identifier);
		if(!expr) return NULL;
	} else if(expr_to_resolve->meta_type == AMT_VALUESET) {
		assert(expr_to_resolve->constraints);
		expr = find_target_specialization_byvalueset(rarg,
				expr_to_resolve->constraints);
		if(!expr) return NULL;
	} else {
		errno = ESRCH;
		return NULL;
	}

	DEBUG("Found target %s (%d/%x)",
		expr->Identifier, expr->meta_type, expr->expr_type);

	/*
	 * The grammar cannot tell the type NULL from the value NULL in an
	 * actual parameter list and always produces the value. Where a type is
	 * expected (a type reference to a dummy parameter), use the built-in
	 * type NULL instead.
	 */
	if(expr_to_resolve->meta_type == AMT_TYPEREF
	&& expr->meta_type == AMT_VALUE
	&& expr->value && expr->value->type == ATV_NULL) {
		nex = asn1p_expr_clone(expr, 0);
		asn1p_value_free(nex->value);
		nex->value = NULL;
		nex->meta_type = AMT_TYPE;
		nex->expr_type = ASN_BASIC_NULL;
		free(nex->Identifier);
		nex->Identifier = expr_to_resolve->Identifier
			? strdup(expr_to_resolve->Identifier) : 0;
		return nex;
	}

	if(expr->meta_type == AMT_TYPE
	|| expr->meta_type == AMT_VALUE
	|| expr->meta_type == AMT_TYPEREF
	|| expr->meta_type == AMT_VALUESET) {
		DEBUG("Target is a simple type %s",
			ASN_EXPR_TYPE2STR(expr->expr_type));
		nex = asn1p_expr_clone(expr, 0);
		free(nex->Identifier);
		nex->Identifier = expr_to_resolve->Identifier
			? strdup(expr_to_resolve->Identifier) : 0;
		return nex;
	} else {
		FATAL("Feature not implemented for %s (%d/%x), "
			"please contact the asn1c author",
			rarg->original_expr->Identifier,
			expr->meta_type, expr->expr_type);
		errno = EPERM;
		return NULL;
	}

	return NULL;
}

static asn1p_expr_t *
find_target_specialization_byvalueset(resolver_arg_t *rarg, asn1p_constraint_t *ct) {
	asn1p_ref_t *ref;

	if (ct->type != ACT_EL_TYPE) return NULL;

	ref = ct->containedSubtype->value.v_type->reference;

	return find_target_specialization_byref(rarg, ref);
}

static asn1p_expr_t *
find_target_specialization_byref(resolver_arg_t *rarg, asn1p_ref_t *ref) {
	char *refstr;

	if(!ref || ref->comp_count != 1) {
		errno = ESRCH;
		return NULL;
	}

	refstr = ref->components[0].name;	/* T */

	return find_target_specialization_bystr(rarg, refstr);
}

static asn1p_expr_t *
find_target_specialization_bystr(resolver_arg_t *rarg, char *refstr) {
	arg_t *arg = rarg->arg;
	asn1p_expr_t *target;
	int i;

	if(!refstr) return NULL;

	target = TQ_FIRST(&rarg->rhs_pspecs->members);
	for(i = 0; i < rarg->lhs_params->params_count;
			i++, target = TQ_NEXT(target, next)) {
		struct asn1p_param_s *param = &rarg->lhs_params->params[i];
		if(!target) break;

		if(strcmp(param->argument, refstr))
			continue;

		return target;
	}
	if(i != rarg->lhs_params->params_count) {
		FATAL("Parameterization of %s failed: "
			"parameters number mismatch",
				rarg->original_expr->Identifier);
		errno = EPERM;
		return NULL;
	}

	errno = ESRCH;
	return NULL;
}
