/*
 * Copyright (c) 2026 Ingo A. Kubbilun and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Serialize the ASN.1 syntax tree as JSON.
 * Portable C99 code: GCC/Clang and MS Visual C.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <asn1parser.h>

#include "asn1json.h"

/* ------------------------------------------------------------------ */
/* Minimal JSON writer                                                */
/* ------------------------------------------------------------------ */

typedef struct {
	char close;	/* '}' or ']' */
	int count;	/* Elements emitted so far */
} jlevel_t;

typedef struct {
	FILE *fp;
	int pretty;
	int after_key;
	int error;
	jlevel_t *stack;
	size_t depth;
	size_t cap;
} jw_t;

static void
jw_raw(jw_t *w, const char *s) {
	if(fputs(s, w->fp) < 0) w->error = 1;
}

static void
jw_newline(jw_t *w) {
	size_t i;
	if(!w->pretty) return;
	if(fputc('\n', w->fp) < 0) w->error = 1;
	for(i = 0; i < w->depth; i++) jw_raw(w, "  ");
}

/* Emit separator before a value or a key */
static void
jw_sep(jw_t *w) {
	if(w->after_key) {
		w->after_key = 0;
		return;
	}
	if(w->depth > 0) {
		if(w->stack[w->depth - 1].count++ > 0) jw_raw(w, ",");
		jw_newline(w);
	}
}

static void
jw_open(jw_t *w, char open, char close) {
	char b[2];
	jw_sep(w);
	if(w->depth == w->cap) {
		size_t ncap = w->cap ? w->cap * 2 : 32;
		jlevel_t *n = (jlevel_t *)realloc(w->stack, ncap * sizeof(*n));
		if(!n) {
			w->error = 1;
			return;
		}
		w->stack = n;
		w->cap = ncap;
	}
	b[0] = open;
	b[1] = 0;
	jw_raw(w, b);
	w->stack[w->depth].close = close;
	w->stack[w->depth].count = 0;
	w->depth++;
}

static void
jw_close(jw_t *w) {
	char b[2];
	int had;
	if(w->depth == 0) return;
	w->depth--;
	had = w->stack[w->depth].count;
	b[0] = w->stack[w->depth].close;
	b[1] = 0;
	if(had) jw_newline(w);
	jw_raw(w, b);
}

#define jw_obj(w) jw_open((w), '{', '}')
#define jw_arr(w) jw_open((w), '[', ']')

static void
jw_str_n(jw_t *w, const char *s, size_t len) {
	size_t i;
	jw_sep(w);
	jw_raw(w, "\"");
	for(i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		switch(c) {
		case '"': jw_raw(w, "\\\""); break;
		case '\\': jw_raw(w, "\\\\"); break;
		case '\n': jw_raw(w, "\\n"); break;
		case '\r': jw_raw(w, "\\r"); break;
		case '\t': jw_raw(w, "\\t"); break;
		case '\b': jw_raw(w, "\\b"); break;
		case '\f': jw_raw(w, "\\f"); break;
		default:
			if(c < 0x20 || c == 0x7f) {
				char b[8];
				snprintf(b, sizeof(b), "\\u%04x", c);
				jw_raw(w, b);
			} else {
				if(fputc(c, w->fp) < 0) w->error = 1;
			}
		}
	}
	jw_raw(w, "\"");
}

static void
jw_str(jw_t *w, const char *s) {
	jw_str_n(w, s ? s : "", s ? strlen(s) : 0);
}

static void
jw_key(jw_t *w, const char *k) {
	jw_str(w, k);
	jw_raw(w, w->pretty ? ": " : ":");
	w->after_key = 1;
}

static void
jw_int(jw_t *w, asn1c_integer_t v) {
	jw_sep(w);
	jw_raw(w, asn1p_itoa(v));
}

static void
jw_bool(jw_t *w, int v) {
	jw_sep(w);
	jw_raw(w, v ? "true" : "false");
}

static void
jw_null(jw_t *w) {
	jw_sep(w);
	jw_raw(w, "null");
}

static void
jw_double(jw_t *w, double d) {
	char b[64];
	jw_sep(w);
	/* JSON has no Inf/NaN */
	if(d != d || d - d != 0.0) {
		jw_raw(w, "null");
		return;
	}
	snprintf(b, sizeof(b), "%.17g", d);
	jw_raw(w, b);
}

/* Convenience "key: value" helpers */
static void kstr(jw_t *w, const char *k, const char *v) { jw_key(w, k); jw_str(w, v); }
static void kint(jw_t *w, const char *k, asn1c_integer_t v) { jw_key(w, k); jw_int(w, v); }
static void kbool(jw_t *w, const char *k, int v) { jw_key(w, k); jw_bool(w, v); }

/* ------------------------------------------------------------------ */
/* Tree serialization                                                 */
/* ------------------------------------------------------------------ */

static void j_expr(jw_t *w, const asn1p_expr_t *expr);
static void j_value(jw_t *w, const asn1p_value_t *val);
static void j_constraint(jw_t *w, const asn1p_constraint_t *ct);

const char *
asn1json_meta_name(asn1p_expr_meta_e m) {
	switch(m) {
	case AMT_TYPE: return "TYPE";
	case AMT_TYPEREF: return "TYPEREF";
	case AMT_VALUE: return "VALUE";
	case AMT_VALUESET: return "VALUESET";
	case AMT_OBJECT: return "OBJECT";
	case AMT_OBJECTCLASS: return "OBJECTCLASS";
	case AMT_OBJECTFIELD: return "OBJECTFIELD";
	default: return "INVALID";
	}
}

const char *
asn1json_type_name(asn1p_expr_type_e t) {
	switch(t) {
	case A1TC_INVALID: return "INVALID";
	case A1TC_REFERENCE: return "REFERENCE";
	case A1TC_EXPORTVAR: return "EXPORTVAR";
	case A1TC_UNIVERVAL: return "UNIVERVAL";
	case A1TC_BITVECTOR: return "BITVECTOR";
	case A1TC_OPAQUE: return "OPAQUE";
	case A1TC_EXTENSIBLE: return "EXTENSIBLE";
	case A1TC_COMPONENTS_OF: return "COMPONENTS OF";
	case A1TC_VALUESET: return "VALUESET";
	case A1TC_CLASSDEF: return "CLASS";
	case A1TC_INSTANCE: return "INSTANCE";
	case A1TC_CLASSFIELD_TFS: return "CLASSFIELD_TypeFieldSpec";
	case A1TC_CLASSFIELD_FTVFS: return "CLASSFIELD_FixedTypeValueFieldSpec";
	case A1TC_CLASSFIELD_VTVFS: return "CLASSFIELD_VariableTypeValueFieldSpec";
	case A1TC_CLASSFIELD_FTVSFS: return "CLASSFIELD_FixedTypeValueSetFieldSpec";
	case A1TC_CLASSFIELD_VTVSFS: return "CLASSFIELD_VariableTypeValueSetFieldSpec";
	case A1TC_CLASSFIELD_OFS: return "CLASSFIELD_ObjectFieldSpec";
	case A1TC_CLASSFIELD_OSFS: return "CLASSFIELD_ObjectSetFieldSpec";
	default: break;
	}
	if((int)t > 0 && (int)t < (int)ASN_EXPR_TYPE_MAX) {
		const char *s = ASN_EXPR_TYPE2STR(t);
		if(s) return s;
	}
	return "UNKNOWN";
}

const char *
asn1json_lex_name(enum asn1p_ref_lex_type_e t) {
	switch(t) {
	case RLT_CAPITALS: return "CAPITALS";
	case RLT_Uppercase: return "Uppercase";
	case RLT_lowercase: return "lowercase";
	case RLT_AmpUppercase: return "&Uppercase";
	case RLT_Amplowercase: return "&lowercase";
	case RLT_Atlowercase: return "@lowercase";
	case RLT_AtDotlowercase: return "@.lowercase";
	default: return "UNKNOWN";
	}
}

static void
j_ref(jw_t *w, const asn1p_ref_t *ref) {
	size_t i;
	jw_obj(w);
	jw_key(w, "text");
	jw_str(w, asn1p_ref_string(ref));
	jw_key(w, "components");
	jw_arr(w);
	for(i = 0; i < ref->comp_count; i++) {
		jw_obj(w);
		kstr(w, "name", ref->components[i].name);
		kstr(w, "lexType", asn1json_lex_name(ref->components[i].lex_type));
		jw_close(w);
	}
	jw_close(w);
	jw_close(w);
}

static void
j_oid(jw_t *w, const asn1p_oid_t *oid) {
	int i;
	jw_arr(w);
	for(i = 0; i < oid->arcs_count; i++) {
		jw_obj(w);
		if(oid->arcs[i].name) kstr(w, "name", oid->arcs[i].name);
		if(oid->arcs[i].number >= 0) kint(w, "number", oid->arcs[i].number);
		jw_close(w);
	}
	jw_close(w);
}

static void
j_bits(jw_t *w, const asn1p_value_t *val) {
	static const char hex[] = "0123456789ABCDEF";
	const uint8_t *bits = val->value.binary_vector.bits;
	int n = val->value.binary_vector.size_in_bits;
	int i;
	size_t len;
	char *buf;

	if(n < 0) n = 0;
	len = (size_t)n + 4;
	buf = (char *)malloc(len + (size_t)n / 4 + 4);
	if(!buf) {
		w->error = 1;
		return;
	}
	len = 0;
	if(n % 8) {
		for(i = 0; i < n; i++)
			buf[len++] = ((bits[i >> 3] >> (7 - (i % 8))) & 1) ? '1' : '0';
	} else {
		for(i = 0; i < (n >> 3); i++) {
			buf[len++] = hex[bits[i] >> 4];
			buf[len++] = hex[bits[i] & 0x0f];
		}
	}
	buf[len] = 0;
	jw_obj(w);
	kstr(w, "format", (n % 8) ? "B" : "H");
	kint(w, "bits", n);
	kstr(w, "digits", buf);
	jw_close(w);
	free(buf);
}

/* Every value is an object: {"kind": "...", ...} */
static void
j_value(jw_t *w, const asn1p_value_t *val) {
	if(!val) {
		jw_null(w);
		return;
	}
	jw_obj(w);
	switch(val->type) {
	case ATV_NOVALUE: kstr(w, "kind", "NOVALUE"); break;
	case ATV_NULL: kstr(w, "kind", "NULL"); break;
	case ATV_MAX: kstr(w, "kind", "MAX"); break;
	case ATV_MIN: kstr(w, "kind", "MIN"); break;
	case ATV_TRUE: kstr(w, "kind", "TRUE"); break;
	case ATV_FALSE: kstr(w, "kind", "FALSE"); break;
	case ATV_REAL:
		kstr(w, "kind", "REAL");
		jw_key(w, "value");
		jw_double(w, val->value.v_double);
		break;
	case ATV_INTEGER:
		kstr(w, "kind", "INTEGER");
		kint(w, "value", val->value.v_integer);
		break;
	case ATV_TUPLE:
		kstr(w, "kind", "TUPLE");
		jw_key(w, "value");
		jw_arr(w);
		jw_int(w, (val->value.v_integer >> 4) & 0x0f);
		jw_int(w, val->value.v_integer & 0x0f);
		jw_close(w);
		break;
	case ATV_QUADRUPLE:
		kstr(w, "kind", "QUADRUPLE");
		jw_key(w, "value");
		jw_arr(w);
		jw_int(w, (val->value.v_integer >> 24) & 0xff);
		jw_int(w, (val->value.v_integer >> 16) & 0xff);
		jw_int(w, (val->value.v_integer >> 8) & 0xff);
		jw_int(w, val->value.v_integer & 0xff);
		jw_close(w);
		break;
	case ATV_STRING:
		kstr(w, "kind", "STRING");
		jw_key(w, "value");
		if(val->value.string.buf)
			jw_str_n(w, (const char *)val->value.string.buf,
			         val->value.string.size > 0
			             ? (size_t)val->value.string.size : 0);
		else
			jw_null(w);
		break;
	case ATV_UNPARSED:
		kstr(w, "kind", "UNPARSED");
		jw_key(w, "value");
		jw_str(w, (const char *)val->value.string.buf);
		break;
	case ATV_BITVECTOR:
		kstr(w, "kind", "BITVECTOR");
		jw_key(w, "value");
		j_bits(w, val);
		break;
	case ATV_REFERENCED:
		kstr(w, "kind", "REFERENCE");
		jw_key(w, "value");
		if(val->value.reference) j_ref(w, val->value.reference);
		else jw_null(w);
		break;
	case ATV_TYPE:
		kstr(w, "kind", "TYPE");
		jw_key(w, "value");
		if(val->value.v_type) j_expr(w, val->value.v_type);
		else jw_null(w);
		break;
	case ATV_VALUESET:
		kstr(w, "kind", "VALUESET");
		jw_key(w, "value");
		j_constraint(w, val->value.constraint);
		break;
	case ATV_CHOICE_IDENTIFIER:
		kstr(w, "kind", "CHOICE_IDENTIFIER");
		kstr(w, "identifier", val->value.choice_identifier.identifier);
		jw_key(w, "value");
		j_value(w, val->value.choice_identifier.value);
		break;
	default:
		kstr(w, "kind", "UNKNOWN");
		break;
	}
	jw_close(w);
}

const char *
asn1json_presence_name(enum asn1p_constr_pres_e p) {
	switch(p) {
	case ACPRES_PRESENT: return "PRESENT";
	case ACPRES_ABSENT: return "ABSENT";
	case ACPRES_OPTIONAL: return "OPTIONAL";
	default: return NULL;
	}
}

static void
j_constraint(jw_t *w, const asn1p_constraint_t *ct) {
	unsigned int i;
	const char *pres;
	if(!ct) {
		jw_null(w);
		return;
	}
	jw_obj(w);
	kstr(w, "type", asn1p_constraint_type2str(ct->type));
	switch(ct->type) {
	case ACT_EL_RANGE: kstr(w, "range", ".."); break;
	case ACT_EL_LLRANGE: kstr(w, "range", "<.."); break;
	case ACT_EL_RLRANGE: kstr(w, "range", "..<"); break;
	case ACT_EL_ULRANGE: kstr(w, "range", "<..<"); break;
	default: break;
	}
	pres = asn1json_presence_name(ct->presence);
	if(pres) kstr(w, "presence", pres);
	if(ct->_lineno) kint(w, "line", ct->_lineno);
	if(ct->containedSubtype) {
		jw_key(w, "containedSubtype");
		j_value(w, ct->containedSubtype);
	}
	if(ct->value) {
		jw_key(w, "value");
		j_value(w, ct->value);
	}
	if(ct->range_start) {
		jw_key(w, "rangeStart");
		j_value(w, ct->range_start);
	}
	if(ct->range_stop) {
		jw_key(w, "rangeStop");
		j_value(w, ct->range_stop);
	}
	if(ct->el_count) {
		jw_key(w, "elements");
		jw_arr(w);
		for(i = 0; i < ct->el_count; i++) j_constraint(w, ct->elements[i]);
		jw_close(w);
	}
	jw_close(w);
}

static void
j_wsyntx(jw_t *w, const asn1p_wsyntx_t *wx) {
	const asn1p_wsyntx_chunk_t *c;
	jw_arr(w);
	TQ_FOR(c, &(wx->chunks), next) {
		jw_obj(w);
		switch(c->type) {
		case WC_LITERAL:
			kstr(w, "kind", "LITERAL");
			kstr(w, "token", c->content.token);
			break;
		case WC_WHITESPACE:
			kstr(w, "kind", "WHITESPACE");
			kstr(w, "token", c->content.token);
			break;
		case WC_FIELD:
			kstr(w, "kind", "FIELD");
			kstr(w, "token", c->content.token);
			break;
		case WC_OPTIONALGROUP:
			kstr(w, "kind", "OPTIONALGROUP");
			jw_key(w, "syntax");
			if(c->content.syntax) j_wsyntx(w, c->content.syntax);
			else jw_null(w);
			break;
		}
		jw_close(w);
	}
	jw_close(w);
}

const char *
asn1json_tag_class_name(int c) {
	switch(c) {
	case TC_UNIVERSAL: return "UNIVERSAL";
	case TC_APPLICATION: return "APPLICATION";
	case TC_CONTEXT_SPECIFIC: return "CONTEXT";
	case TC_PRIVATE: return "PRIVATE";
	default: return "NONE";
	}
}

const char *
asn1json_tag_mode_name(int m) {
	switch(m) {
	case TM_IMPLICIT: return "IMPLICIT";
	case TM_EXPLICIT: return "EXPLICIT";
	default: return "DEFAULT";
	}
}

static void
j_expr(jw_t *w, const asn1p_expr_t *expr) {
	const asn1p_expr_t *m;
	int i;

	if(!expr) {
		jw_null(w);
		return;
	}
	jw_obj(w);
	if(expr->Identifier) kstr(w, "name", expr->Identifier);
	kint(w, "line", expr->_lineno);
	kstr(w, "meta", asn1json_meta_name(expr->meta_type));
	kstr(w, "type", asn1json_type_name(expr->expr_type));

	if(expr->reference) {
		jw_key(w, "reference");
		j_ref(w, expr->reference);
	}

	if(expr->lhs_params && expr->lhs_params->params_count > 0) {
		jw_key(w, "parameters");
		jw_arr(w);
		for(i = 0; i < expr->lhs_params->params_count; i++) {
			const struct asn1p_param_s *p = &expr->lhs_params->params[i];
			jw_obj(w);
			kstr(w, "argument", p->argument);
			if(p->governor) {
				jw_key(w, "governor");
				j_ref(w, p->governor);
			}
			jw_close(w);
		}
		jw_close(w);
	}

	if(expr->rhs_pspecs) {
		jw_key(w, "parameterSpecs");
		jw_arr(w);
		TQ_FOR(m, &(expr->rhs_pspecs->members), next) j_expr(w, m);
		jw_close(w);
	}

	if(expr->tag.tag_class != TC_NOCLASS) {
		jw_key(w, "tag");
		jw_obj(w);
		kstr(w, "class", asn1json_tag_class_name(expr->tag.tag_class));
		kstr(w, "mode", asn1json_tag_mode_name(expr->tag.tag_mode));
		kint(w, "number", expr->tag.tag_value);
		jw_close(w);
	}

	if(expr->constraints) {
		jw_key(w, "constraints");
		j_constraint(w, expr->constraints);
	}
	if(expr->combined_constraints) {
		jw_key(w, "combinedConstraints");
		j_constraint(w, expr->combined_constraints);
	}

	if(expr->value) {
		jw_key(w, "value");
		j_value(w, expr->value);
	}

	if(expr->with_syntax) {
		jw_key(w, "withSyntax");
		j_wsyntx(w, expr->with_syntax);
	}

	if(expr->marker.flags != EM_NOMARK) {
		int fl = (int)expr->marker.flags;
		jw_key(w, "marker");
		jw_obj(w);
		if((fl & EM_DEFAULT) == EM_DEFAULT) {
			kbool(w, "default", 1);
			jw_key(w, "defaultValue");
			j_value(w, expr->marker.default_value);
		} else if((fl & EM_OPTIONAL) == EM_OPTIONAL) {
			kbool(w, "optional", 1);
		} else {
			if(fl & EM_INDIRECT) kbool(w, "indirect", 1);
			if(fl & EM_OMITABLE) kbool(w, "omitable", 1);
		}
		if(fl & EM_UNRECURSE) kbool(w, "unrecurse", 1);
		jw_close(w);
	}

	if(expr->unique) kbool(w, "unique", 1);

	if(expr->members.tq_head) {
		jw_key(w, "members");
		jw_arr(w);
		TQ_FOR(m, &(expr->members), next) j_expr(w, m);
		jw_close(w);
	}

	jw_close(w);
}

static void
j_xports(jw_t *w, const asn1p_xports_t *xp) {
	const asn1p_expr_t *m;
	jw_obj(w);
	if(xp->fromModuleName) kstr(w, "fromModule", xp->fromModuleName);
	if(xp->identifier.oid) {
		jw_key(w, "oid");
		j_oid(w, xp->identifier.oid);
	}
	if(xp->identifier.value) {
		jw_key(w, "identifierValue");
		j_value(w, xp->identifier.value);
	}
	jw_key(w, "symbols");
	jw_arr(w);
	TQ_FOR(m, &(xp->xp_members), next) jw_str(w, m->Identifier);
	jw_close(w);
	jw_close(w);
}

const char *
asn1json_tagging_name(asn1p_module_flags_e f) {
	if(f & MSF_AUTOMATIC_TAGS) return "AUTOMATIC";
	if(f & MSF_IMPLICIT_TAGS) return "IMPLICIT";
	if(f & MSF_EXPLICIT_TAGS) return "EXPLICIT";
	return "DEFAULT";	/* Nothing specified in the module header */
}

static void
j_module(jw_t *w, const asn1p_module_t *mod) {
	const asn1p_xports_t *xp;
	const asn1p_expr_t *m;

	jw_obj(w);
	kstr(w, "name", mod->ModuleName);
	if(mod->source_file_name) kstr(w, "sourceFile", mod->source_file_name);
	if(mod->module_oid) {
		jw_key(w, "oid");
		j_oid(w, mod->module_oid);
	}
	kstr(w, "tagging", asn1json_tagging_name(mod->module_flags));
	if(mod->module_flags & MSF_MASK_INSTRUCTIONS) {
		jw_key(w, "instructions");
		jw_arr(w);
		if(mod->module_flags & MSF_TAG_INSTRUCTIONS) jw_str(w, "TAG");
		if(mod->module_flags & MSF_XER_INSTRUCTIONS) jw_str(w, "XER");
		if(mod->module_flags & MSF_unk_INSTRUCTIONS) jw_str(w, "unk");
		jw_close(w);
	}
	kbool(w, "extensibilityImplied",
	      (mod->module_flags & MSF_EXTENSIBILITY_IMPLIED) != 0);

	jw_key(w, "exports");
	jw_arr(w);
	TQ_FOR(xp, &(mod->exports), xp_next) j_xports(w, xp);
	jw_close(w);

	jw_key(w, "imports");
	jw_arr(w);
	TQ_FOR(xp, &(mod->imports), xp_next) j_xports(w, xp);
	jw_close(w);

	jw_key(w, "members");
	jw_arr(w);
	TQ_FOR(m, &(mod->members), next) j_expr(w, m);
	jw_close(w);

	jw_close(w);
}

int
asn1json(asn1p_t *asn, enum asn1json_flags flags, FILE *fp) {
	jw_t w;
	asn1p_module_t *mod;

	if(asn == NULL) {
		errno = EINVAL;
		return -1;
	}

	memset(&w, 0, sizeof(w));
	w.fp = fp ? fp : stdout;
	w.pretty = !(flags & AJF_COMPACT);

	jw_obj(&w);
	kstr(&w, "generator", "asn1c-" VERSION);
	jw_key(&w, "modules");
	jw_arr(&w);
	TQ_FOR(mod, &(asn->modules), mod_next) {
		if(mod->_tags & MT_STANDARD_MODULE) continue;
		j_module(&w, mod);
	}
	jw_close(&w);
	jw_close(&w);
	jw_raw(&w, "\n");

	free(w.stack);
	if(fflush(w.fp) != 0) w.error = 1;
	if(w.error) {
		if(!errno) errno = EIO;
		return -1;
	}
	return 0;
}
