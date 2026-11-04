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
 * De-serialize the ASN.1 syntax tree from JSON (the inverse of asn1json.c).
 * Portable C99 code: GCC/Clang and MS Visual C.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include <asn1parser.h>

#include "asn1json.h"

/* ------------------------------------------------------------------ */
/* Generic JSON DOM and parser                                        */
/* ------------------------------------------------------------------ */

typedef enum { JN_NULL, JN_BOOL, JN_NUM, JN_STR, JN_ARR, JN_OBJ } jtype_e;

typedef struct jn_s {
	jtype_e t;
	int b;			/* JN_BOOL */
	char *s;		/* JN_NUM (raw text), JN_STR (decoded) */
	size_t len;		/* JN_STR length */
	struct jn_s **v;	/* JN_ARR / JN_OBJ values */
	char **k;		/* JN_OBJ keys */
	size_t n, cap;
} jn_t;

#define JSON_MAX_DEPTH 2000

typedef struct {
	const char *src;	/* Source name for diagnostics */
	const char *base, *p, *end;
	char err[512];
	int failed;
} ctx_t;

static void
cerr(ctx_t *c, const char *fmt, ...) {
	va_list ap;
	char msg[400];
	if(c->failed) return;
	c->failed = 1;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	snprintf(c->err, sizeof(c->err), "%s: %s", c->src, msg);
}

static void
perr(ctx_t *c, const char *fmt, ...) {
	va_list ap;
	char msg[300];
	const char *q;
	int line = 1;
	if(c->failed) return;
	for(q = c->base; q < c->p && q < c->end; q++)
		if(*q == '\n') line++;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	cerr(c, "JSON syntax error at line %d: %s", line, msg);
}

static void
jn_free(jn_t *n) {
	size_t i;
	if(!n) return;
	if(n->t == JN_ARR || n->t == JN_OBJ) {
		for(i = 0; i < n->n; i++) {
			jn_free(n->v[i]);
			if(n->k) free(n->k[i]);
		}
	}
	free(n->v);
	free(n->k);
	free(n->s);
	free(n);
}

static int
jn_push(jn_t *n, char *key, jn_t *val) {
	if(n->n == n->cap) {
		size_t nc = n->cap ? n->cap * 2 : 8;
		jn_t **nv = (jn_t **)realloc(n->v, nc * sizeof(*nv));
		if(!nv) return -1;
		n->v = nv;
		if(n->t == JN_OBJ) {
			char **nk = (char **)realloc(n->k, nc * sizeof(*nk));
			if(!nk) return -1;
			n->k = nk;
		}
		n->cap = nc;
	}
	n->v[n->n] = val;
	if(n->t == JN_OBJ) n->k[n->n] = key;
	n->n++;
	return 0;
}

static void
skip_ws(ctx_t *c) {
	while(c->p < c->end
	      && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
		c->p++;
}

static void
put_utf8(char *out, size_t *o, unsigned long cp) {
	if(cp < 0x80) {
		out[(*o)++] = (char)cp;
	} else if(cp < 0x800) {
		out[(*o)++] = (char)(0xC0 | (cp >> 6));
		out[(*o)++] = (char)(0x80 | (cp & 0x3F));
	} else if(cp < 0x10000) {
		out[(*o)++] = (char)(0xE0 | (cp >> 12));
		out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*o)++] = (char)(0x80 | (cp & 0x3F));
	} else {
		out[(*o)++] = (char)(0xF0 | (cp >> 18));
		out[(*o)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*o)++] = (char)(0x80 | (cp & 0x3F));
	}
}

static int
hex4(ctx_t *c, unsigned long *out) {
	int i;
	unsigned long v = 0;
	if(c->end - c->p < 4) {
		perr(c, "truncated \\u escape");
		return -1;
	}
	for(i = 0; i < 4; i++) {
		char ch = *c->p++;
		v <<= 4;
		if(ch >= '0' && ch <= '9') v |= (unsigned long)(ch - '0');
		else if(ch >= 'a' && ch <= 'f') v |= (unsigned long)(ch - 'a' + 10);
		else if(ch >= 'A' && ch <= 'F') v |= (unsigned long)(ch - 'A' + 10);
		else {
			perr(c, "bad \\u escape");
			return -1;
		}
	}
	*out = v;
	return 0;
}

/* Parse a string literal (c->p is at the opening quote) */
static int
parse_string(ctx_t *c, char **out, size_t *outlen) {
	size_t maxlen = (size_t)(c->end - c->p);
	char *buf;
	size_t o = 0;

	c->p++; /* opening quote */
	buf = (char *)malloc(maxlen + 8);
	if(!buf) {
		cerr(c, "out of memory");
		return -1;
	}
	for(;;) {
		unsigned char ch;
		if(c->p >= c->end) {
			perr(c, "unterminated string");
			goto fail;
		}
		ch = (unsigned char)*c->p++;
		if(ch == '"') break;
		if(ch < 0x20) {
			perr(c, "control character in string");
			goto fail;
		}
		if(ch != '\\') {
			buf[o++] = (char)ch;
			continue;
		}
		if(c->p >= c->end) {
			perr(c, "unterminated escape");
			goto fail;
		}
		ch = (unsigned char)*c->p++;
		switch(ch) {
		case '"': buf[o++] = '"'; break;
		case '\\': buf[o++] = '\\'; break;
		case '/': buf[o++] = '/'; break;
		case 'b': buf[o++] = '\b'; break;
		case 'f': buf[o++] = '\f'; break;
		case 'n': buf[o++] = '\n'; break;
		case 'r': buf[o++] = '\r'; break;
		case 't': buf[o++] = '\t'; break;
		case 'u': {
			unsigned long cp, lo;
			if(hex4(c, &cp)) goto fail;
			if(cp >= 0xD800 && cp <= 0xDBFF) {
				if(c->end - c->p >= 2 && c->p[0] == '\\' && c->p[1] == 'u') {
					c->p += 2;
					if(hex4(c, &lo)) goto fail;
					if(lo < 0xDC00 || lo > 0xDFFF) {
						perr(c, "bad surrogate pair");
						goto fail;
					}
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				} else {
					perr(c, "lone surrogate");
					goto fail;
				}
			} else if(cp >= 0xDC00 && cp <= 0xDFFF) {
				perr(c, "lone surrogate");
				goto fail;
			}
			put_utf8(buf, &o, cp);
			break;
		}
		default:
			perr(c, "bad escape \\%c", ch);
			goto fail;
		}
	}
	buf[o] = '\0';
	*out = buf;
	if(outlen) *outlen = o;
	return 0;
fail:
	free(buf);
	return -1;
}

static jn_t *parse_value(ctx_t *c, int depth);

static jn_t *
jn_alloc(ctx_t *c, jtype_e t) {
	jn_t *n = (jn_t *)calloc(1, sizeof(*n));
	if(!n) {
		cerr(c, "out of memory");
		return NULL;
	}
	n->t = t;
	return n;
}

static jn_t *
parse_value(ctx_t *c, int depth) {
	jn_t *n = NULL;

	if(depth > JSON_MAX_DEPTH) {
		perr(c, "nesting too deep");
		return NULL;
	}
	skip_ws(c);
	if(c->p >= c->end) {
		perr(c, "unexpected end of input");
		return NULL;
	}
	switch(*c->p) {
	case '{': {
		n = jn_alloc(c, JN_OBJ);
		if(!n) return NULL;
		c->p++;
		skip_ws(c);
		if(c->p < c->end && *c->p == '}') {
			c->p++;
			return n;
		}
		for(;;) {
			char *key = NULL;
			jn_t *val;
			skip_ws(c);
			if(c->p >= c->end || *c->p != '"') {
				perr(c, "expected object key");
				goto fail;
			}
			if(parse_string(c, &key, NULL)) goto fail;
			skip_ws(c);
			if(c->p >= c->end || *c->p != ':') {
				free(key);
				perr(c, "expected ':'");
				goto fail;
			}
			c->p++;
			val = parse_value(c, depth + 1);
			if(!val) {
				free(key);
				goto fail;
			}
			if(jn_push(n, key, val)) {
				free(key);
				jn_free(val);
				cerr(c, "out of memory");
				goto fail;
			}
			skip_ws(c);
			if(c->p < c->end && *c->p == ',') {
				c->p++;
				continue;
			}
			if(c->p < c->end && *c->p == '}') {
				c->p++;
				return n;
			}
			perr(c, "expected ',' or '}'");
			goto fail;
		}
	}
	case '[': {
		n = jn_alloc(c, JN_ARR);
		if(!n) return NULL;
		c->p++;
		skip_ws(c);
		if(c->p < c->end && *c->p == ']') {
			c->p++;
			return n;
		}
		for(;;) {
			jn_t *val = parse_value(c, depth + 1);
			if(!val) goto fail;
			if(jn_push(n, NULL, val)) {
				jn_free(val);
				cerr(c, "out of memory");
				goto fail;
			}
			skip_ws(c);
			if(c->p < c->end && *c->p == ',') {
				c->p++;
				continue;
			}
			if(c->p < c->end && *c->p == ']') {
				c->p++;
				return n;
			}
			perr(c, "expected ',' or ']'");
			goto fail;
		}
	}
	case '"':
		n = jn_alloc(c, JN_STR);
		if(!n) return NULL;
		if(parse_string(c, &n->s, &n->len)) goto fail;
		return n;
	case 't':
	case 'f':
	case 'n': {
		size_t rem = (size_t)(c->end - c->p);
		if(rem >= 4 && !memcmp(c->p, "true", 4)) {
			n = jn_alloc(c, JN_BOOL);
			if(!n) return NULL;
			n->b = 1;
			c->p += 4;
			return n;
		} else if(rem >= 5 && !memcmp(c->p, "false", 5)) {
			n = jn_alloc(c, JN_BOOL);
			if(!n) return NULL;
			c->p += 5;
			return n;
		} else if(rem >= 4 && !memcmp(c->p, "null", 4)) {
			n = jn_alloc(c, JN_NULL);
			if(!n) return NULL;
			c->p += 4;
			return n;
		}
		perr(c, "invalid literal");
		return NULL;
	}
	default: {
		const char *st = c->p;
		size_t l;
		if(*c->p != '-' && (*c->p < '0' || *c->p > '9')) {
			perr(c, "unexpected character '%c'", *c->p);
			return NULL;
		}
		while(c->p < c->end
		      && ((*c->p >= '0' && *c->p <= '9') || *c->p == '-'
		          || *c->p == '+' || *c->p == '.' || *c->p == 'e'
		          || *c->p == 'E'))
			c->p++;
		l = (size_t)(c->p - st);
		n = jn_alloc(c, JN_NUM);
		if(!n) return NULL;
		n->s = (char *)malloc(l + 1);
		if(!n->s) {
			cerr(c, "out of memory");
			goto fail;
		}
		memcpy(n->s, st, l);
		n->s[l] = '\0';
		n->len = l;
		return n;
	}
	}
fail:
	jn_free(n);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* DOM accessors                                                      */
/* ------------------------------------------------------------------ */

/* Member lookup; returns NULL when absent or JSON null */
static jn_t *
jget(const jn_t *o, const char *key) {
	size_t i;
	if(!o || o->t != JN_OBJ) return NULL;
	for(i = 0; i < o->n; i++) {
		if(strcmp(o->k[i], key) == 0)
			return o->v[i]->t == JN_NULL ? NULL : o->v[i];
	}
	return NULL;
}

static int
need(ctx_t *c, const jn_t *n, jtype_e t, const char *what) {
	static const char *names[] = {"null", "boolean", "number",
	                              "string", "array", "object"};
	if(!n || n->t != t) {
		cerr(c, "%s: expected %s", what, names[t]);
		return -1;
	}
	return 0;
}

static char *
jstrdup(ctx_t *c, const jn_t *n, const char *what) {
	char *s;
	if(need(c, n, JN_STR, what)) return NULL;
	s = (char *)malloc(n->len + 1);
	if(!s) {
		cerr(c, "out of memory");
		return NULL;
	}
	memcpy(s, n->s, n->len);
	s[n->len] = '\0';
	return s;
}

static int
jint(ctx_t *c, const jn_t *n, asn1c_integer_t *out, const char *what) {
	const char *p;
	if(need(c, n, JN_NUM, what)) return -1;
	p = n->s;
	if(*p == '-') p++;
	if(!*p) {
		cerr(c, "%s: bad integer \"%s\"", what, n->s);
		return -1;
	}
	for(; *p; p++) {
		if(*p < '0' || *p > '9') {
			cerr(c, "%s: bad integer \"%s\"", what, n->s);
			return -1;
		}
	}
	if(asn1p_atoi(n->s, out)) {
		cerr(c, "%s: integer out of range \"%s\"", what, n->s);
		return -1;
	}
	return 0;
}

static int
jint_key(ctx_t *c, const jn_t *o, const char *key, asn1c_integer_t *out) {
	const jn_t *n = jget(o, key);
	if(!n) return 1; /* absent */
	return jint(c, n, out, key);
}

static int
jbool_key(const jn_t *o, const char *key) {
	const jn_t *n = jget(o, key);
	return n && n->t == JN_BOOL && n->b;
}

/* ------------------------------------------------------------------ */
/* Tree builders                                                      */
/* ------------------------------------------------------------------ */

static int build_expr(ctx_t *c, asn1p_module_t *mod, asn1p_expr_t *e,
                      const jn_t *o);
static asn1p_value_t *build_value(ctx_t *c, asn1p_module_t *mod, const jn_t *o);
static asn1p_constraint_t *build_constraint(ctx_t *c, asn1p_module_t *mod,
                                            const jn_t *o);

static asn1p_ref_t *
build_ref(ctx_t *c, asn1p_module_t *mod, const jn_t *o) {
	asn1p_ref_t *ref;
	const jn_t *comps;
	size_t i;

	if(need(c, o, JN_OBJ, "reference")) return NULL;
	comps = jget(o, "components");
	if(need(c, comps, JN_ARR, "reference.components")) return NULL;
	ref = asn1p_ref_new(0, mod);
	if(!ref) {
		cerr(c, "out of memory");
		return NULL;
	}
	for(i = 0; i < comps->n; i++) {
		const jn_t *comp = comps->v[i];
		char *name = jstrdup(c, jget(comp, "name"), "reference component name");
		if(!name) {
			asn1p_ref_free(ref);
			return NULL;
		}
		/* The lexical type is inferred from the name */
		if(asn1p_ref_add_component(ref, name, RLT_UNKNOWN)) {
			cerr(c, "cannot add reference component \"%s\"", name);
			free(name);
			asn1p_ref_free(ref);
			return NULL;
		}
		free(name);
	}
	return ref;
}

static asn1p_oid_t *
build_oid(ctx_t *c, const jn_t *arr) {
	asn1p_oid_t *oid;
	size_t i;

	if(need(c, arr, JN_ARR, "oid")) return NULL;
	oid = asn1p_oid_new();
	if(!oid) {
		cerr(c, "out of memory");
		return NULL;
	}
	for(i = 0; i < arr->n; i++) {
		asn1p_oid_arc_t arc;
		const jn_t *a = arr->v[i];
		const jn_t *nm = jget(a, "name");
		char *name = NULL;
		int r;

		if(need(c, a, JN_OBJ, "oid arc")) goto fail;
		arc.name = NULL;
		arc.number = -1;
		if(nm) {
			name = jstrdup(c, nm, "oid arc name");
			if(!name) goto fail;
			arc.name = name;
		}
		r = jint_key(c, a, "number", &arc.number);
		if(r < 0) {
			free(name);
			goto fail;
		}
		if(arc.name == NULL && arc.number < 0) {
			cerr(c, "oid arc without name or number");
			goto fail;
		}
		r = asn1p_oid_add_arc(oid, &arc);
		free(name);
		if(r) {
			cerr(c, "out of memory");
			goto fail;
		}
	}
	return oid;
fail:
	asn1p_oid_free(oid);
	return NULL;
}

static asn1p_value_t *
alloc_value(ctx_t *c) {
	asn1p_value_t *v = (asn1p_value_t *)calloc(1, sizeof(*v));
	if(!v) cerr(c, "out of memory");
	return v;
}

static int
hexval(char ch) {
	if(ch >= '0' && ch <= '9') return ch - '0';
	if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	return -1;
}

static asn1p_value_t *
build_bits(ctx_t *c, const jn_t *o) {
	const jn_t *jf = jget(o, "format");
	const jn_t *jd = jget(o, "digits");
	asn1c_integer_t nbits = 0;
	int nb;
	size_t bytes, i;
	uint8_t *bits;
	asn1p_value_t *v;

	if(need(c, jf, JN_STR, "bitvector.format")
	   || need(c, jd, JN_STR, "bitvector.digits")
	   || jint_key(c, o, "bits", &nbits) != 0)
		return NULL;
	if(nbits < 0 || nbits > 0x3fffffff) {
		cerr(c, "bitvector.bits out of range");
		return NULL;
	}
	nb = (int)nbits;
	bytes = ((size_t)nb + 7) >> 3;
	bits = (uint8_t *)calloc(bytes + 1, 1);
	if(!bits) {
		cerr(c, "out of memory");
		return NULL;
	}
	if(jf->len == 1 && jf->s[0] == 'B') {
		if(jd->len != (size_t)nb) goto bad;
		for(i = 0; i < (size_t)nb; i++) {
			if(jd->s[i] == '1') bits[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
			else if(jd->s[i] != '0') goto bad;
		}
	} else if(jf->len == 1 && jf->s[0] == 'H') {
		if((nb & 7) || jd->len != bytes * 2) goto bad;
		for(i = 0; i < bytes; i++) {
			int h = hexval(jd->s[2 * i]), l = hexval(jd->s[2 * i + 1]);
			if(h < 0 || l < 0) goto bad;
			bits[i] = (uint8_t)((h << 4) | l);
		}
	} else {
		goto bad;
	}
	v = asn1p_value_frombits(bits, nb, 0);
	if(!v) {
		free(bits);
		cerr(c, "out of memory");
	}
	return v;
bad:
	free(bits);
	cerr(c, "bitvector: inconsistent format/bits/digits");
	return NULL;
}

static asn1p_value_t *
build_value(ctx_t *c, asn1p_module_t *mod, const jn_t *o) {
	const jn_t *jk, *jv;
	const char *kind;
	asn1p_value_t *v = NULL;

	if(need(c, o, JN_OBJ, "value")) return NULL;
	jk = jget(o, "kind");
	if(need(c, jk, JN_STR, "value.kind")) return NULL;
	kind = jk->s;
	jv = jget(o, "value");

#define SIMPLE(name, ty)                           \
	if(strcmp(kind, name) == 0) {              \
		v = alloc_value(c);                \
		if(v) v->type = (ty);              \
		return v;                          \
	}
	SIMPLE("NOVALUE", ATV_NOVALUE);
	SIMPLE("NULL", ATV_NULL);
	SIMPLE("MAX", ATV_MAX);
	SIMPLE("MIN", ATV_MIN);
	SIMPLE("TRUE", ATV_TRUE);
	SIMPLE("FALSE", ATV_FALSE);
#undef SIMPLE

	if(strcmp(kind, "INTEGER") == 0) {
		asn1c_integer_t i;
		if(jint(c, jv, &i, "INTEGER value")) return NULL;
		v = asn1p_value_fromint(i);
		if(!v) cerr(c, "out of memory");
		return v;
	}
	if(strcmp(kind, "REAL") == 0) {
		char *end;
		double d;
		if(need(c, jv, JN_NUM, "REAL value")) return NULL;
		d = strtod(jv->s, &end);
		if(*end) {
			cerr(c, "bad REAL value \"%s\"", jv->s);
			return NULL;
		}
		v = asn1p_value_fromdouble(d);
		if(!v) cerr(c, "out of memory");
		return v;
	}
	if(strcmp(kind, "TUPLE") == 0 || strcmp(kind, "QUADRUPLE") == 0) {
		int quad = (kind[0] == 'Q');
		size_t cnt = quad ? 4 : 2, i;
		asn1c_integer_t acc = 0;
		if(need(c, jv, JN_ARR, "TUPLE/QUADRUPLE value")) return NULL;
		if(jv->n != cnt) {
			cerr(c, "%s value must have %d elements", kind, (int)cnt);
			return NULL;
		}
		for(i = 0; i < cnt; i++) {
			asn1c_integer_t x;
			if(jint(c, jv->v[i], &x, "TUPLE/QUADRUPLE element")) return NULL;
			if(x < 0 || x > (quad ? 255 : 15)) {
				cerr(c, "%s element out of range", kind);
				return NULL;
			}
			acc = quad ? ((acc << 8) | x) : ((acc << 4) | x);
		}
		v = alloc_value(c);
		if(v) {
			v->value.v_integer = acc;
			v->type = quad ? ATV_QUADRUPLE : ATV_TUPLE;
		}
		return v;
	}
	if(strcmp(kind, "STRING") == 0 || strcmp(kind, "UNPARSED") == 0) {
		int unparsed = (kind[0] == 'U');
		static char empty[1] = "";
		if(jv && need(c, jv, JN_STR, "STRING value")) return NULL;
		v = asn1p_value_frombuf(jv ? jv->s : empty, jv ? (int)jv->len : 0, 1);
		if(!v) {
			cerr(c, "out of memory");
			return NULL;
		}
		if(unparsed) v->type = ATV_UNPARSED;
		return v;
	}
	if(strcmp(kind, "BITVECTOR") == 0) {
		if(need(c, jv, JN_OBJ, "BITVECTOR value")) return NULL;
		return build_bits(c, jv);
	}
	if(strcmp(kind, "REFERENCE") == 0) {
		asn1p_ref_t *ref = build_ref(c, mod, jv);
		if(!ref) return NULL;
		v = asn1p_value_fromref(ref, 0);
		if(!v) {
			asn1p_ref_free(ref);
			cerr(c, "out of memory");
		}
		return v;
	}
	if(strcmp(kind, "TYPE") == 0) {
		asn1p_expr_t *e = asn1p_expr_new(0, mod);
		if(!e) {
			cerr(c, "out of memory");
			return NULL;
		}
		if(build_expr(c, mod, e, jv)) {
			asn1p_expr_free(e);
			return NULL;
		}
		v = asn1p_value_fromtype(e);
		if(!v) {
			asn1p_expr_free(e);
			cerr(c, "out of memory");
		}
		return v;
	}
	if(strcmp(kind, "VALUESET") == 0) {
		asn1p_constraint_t *ct = build_constraint(c, mod, jv);
		if(!ct) {
			if(!c->failed) cerr(c, "VALUESET without constraint");
			return NULL;
		}
		v = asn1p_value_fromconstr(ct, 0);
		if(!v) {
			asn1p_constraint_free(ct);
			cerr(c, "out of memory");
		}
		return v;
	}
	if(strcmp(kind, "CHOICE_IDENTIFIER") == 0) {
		char *id = jstrdup(c, jget(o, "identifier"), "CHOICE_IDENTIFIER");
		if(!id) return NULL;
		v = alloc_value(c);
		if(!v) {
			free(id);
			return NULL;
		}
		v->value.choice_identifier.identifier = id;
		if(jv) {
			v->value.choice_identifier.value = build_value(c, mod, jv);
			if(!v->value.choice_identifier.value) {
				asn1p_value_free(v);
				return NULL;
			}
		}
		v->type = ATV_CHOICE_IDENTIFIER;
		return v;
	}
	cerr(c, "unknown value kind \"%s\"", kind);
	return NULL;
}

/* Optional value member; returns 0 on success (including absent) */
static int
opt_value(ctx_t *c, asn1p_module_t *mod, const jn_t *o, const char *key,
          asn1p_value_t **out) {
	const jn_t *jv = jget(o, key);
	if(!jv) return 0;
	*out = build_value(c, mod, jv);
	return *out ? 0 : -1;
}

static int
constraint_type_from_name(const char *name, const char *range,
                          enum asn1p_constraint_type_e *out) {
	int t;
	if(strcmp(name, "ValueRange") == 0) {
		if(!range || strcmp(range, "..") == 0) *out = ACT_EL_RANGE;
		else if(strcmp(range, "<..") == 0) *out = ACT_EL_LLRANGE;
		else if(strcmp(range, "..<") == 0) *out = ACT_EL_RLRANGE;
		else if(strcmp(range, "<..<") == 0) *out = ACT_EL_ULRANGE;
		else return -1;
		return 0;
	}
	for(t = ACT_INVALID; t <= ACT_CA_AEX; t++) {
		const char *s = asn1p_constraint_type2str((enum asn1p_constraint_type_e)t);
		if(s && strcmp(s, name) == 0) {
			*out = (enum asn1p_constraint_type_e)t;
			return 0;
		}
	}
	return -1;
}

static asn1p_constraint_t *
build_constraint(ctx_t *c, asn1p_module_t *mod, const jn_t *o) {
	asn1p_constraint_t *ct;
	const jn_t *jt, *jr, *jp, *els;
	asn1c_integer_t line = 0;
	size_t i;

	if(!o) return NULL;
	if(need(c, o, JN_OBJ, "constraint")) return NULL;
	jt = jget(o, "type");
	if(need(c, jt, JN_STR, "constraint.type")) return NULL;
	jr = jget(o, "range");
	ct = asn1p_constraint_new(0, mod);
	if(!ct) {
		cerr(c, "out of memory");
		return NULL;
	}
	if(constraint_type_from_name(jt->s, jr && jr->t == JN_STR ? jr->s : NULL,
	                             &ct->type)) {
		cerr(c, "unknown constraint type \"%s\"", jt->s);
		goto fail;
	}
	jp = jget(o, "presence");
	if(jp) {
		if(need(c, jp, JN_STR, "constraint.presence")) goto fail;
		if(!strcmp(jp->s, "PRESENT")) ct->presence = ACPRES_PRESENT;
		else if(!strcmp(jp->s, "ABSENT")) ct->presence = ACPRES_ABSENT;
		else if(!strcmp(jp->s, "OPTIONAL")) ct->presence = ACPRES_OPTIONAL;
		else {
			cerr(c, "unknown constraint presence \"%s\"", jp->s);
			goto fail;
		}
	}
	if(jint_key(c, o, "line", &line) < 0) goto fail;
	ct->_lineno = (int)line;
	if(opt_value(c, mod, o, "containedSubtype", &ct->containedSubtype)
	   || opt_value(c, mod, o, "value", &ct->value)
	   || opt_value(c, mod, o, "rangeStart", &ct->range_start)
	   || opt_value(c, mod, o, "rangeStop", &ct->range_stop))
		goto fail;
	els = jget(o, "elements");
	if(els) {
		if(need(c, els, JN_ARR, "constraint.elements")) goto fail;
		for(i = 0; i < els->n; i++) {
			asn1p_constraint_t *sub = build_constraint(c, mod, els->v[i]);
			if(!sub) {
				if(!c->failed) cerr(c, "null constraint element");
				goto fail;
			}
			if(asn1p_constraint_insert(ct, sub)) {
				asn1p_constraint_free(sub);
				cerr(c, "out of memory");
				goto fail;
			}
		}
	}
	return ct;
fail:
	asn1p_constraint_free(ct);
	return NULL;
}

static asn1p_wsyntx_t *
build_wsyntx(ctx_t *c, const jn_t *arr) {
	asn1p_wsyntx_t *wx;
	size_t i;

	if(need(c, arr, JN_ARR, "withSyntax")) return NULL;
	wx = asn1p_wsyntx_new();
	if(!wx) {
		cerr(c, "out of memory");
		return NULL;
	}
	for(i = 0; i < arr->n; i++) {
		const jn_t *ch = arr->v[i];
		const jn_t *jk = jget(ch, "kind");
		asn1p_wsyntx_chunk_t *wc;

		if(need(c, ch, JN_OBJ, "withSyntax chunk")
		   || need(c, jk, JN_STR, "withSyntax chunk kind"))
			goto fail;
		wc = asn1p_wsyntx_chunk_new();
		if(!wc) {
			cerr(c, "out of memory");
			goto fail;
		}
		if(!strcmp(jk->s, "OPTIONALGROUP")) {
			asn1p_wsyntx_t *sub = build_wsyntx(c, jget(ch, "syntax"));
			if(!sub) {
				asn1p_wsyntx_chunk_free(wc);
				goto fail;
			}
			wc->type = WC_OPTIONALGROUP;
			wc->content.syntax = sub;
			sub->parent = wc;
		} else {
			if(!strcmp(jk->s, "LITERAL")) wc->type = WC_LITERAL;
			else if(!strcmp(jk->s, "WHITESPACE")) wc->type = WC_WHITESPACE;
			else if(!strcmp(jk->s, "FIELD")) wc->type = WC_FIELD;
			else {
				asn1p_wsyntx_chunk_free(wc);
				cerr(c, "unknown withSyntax chunk kind \"%s\"", jk->s);
				goto fail;
			}
			wc->content.token = jstrdup(c, jget(ch, "token"), "withSyntax token");
			if(!wc->content.token) {
				asn1p_wsyntx_chunk_free(wc);
				goto fail;
			}
		}
		TQ_ADD(&(wx->chunks), wc, next);
	}
	return wx;
fail:
	asn1p_wsyntx_free(wx);
	return NULL;
}

static int
expr_meta_from_name(const char *name, asn1p_expr_meta_e *out) {
	int m;
	for(m = AMT_INVALID; m < AMT_EXPR_META_MAX; m++) {
		if(strcmp(asn1json_meta_name((asn1p_expr_meta_e)m), name) == 0) {
			*out = (asn1p_expr_meta_e)m;
			return 0;
		}
	}
	return -1;
}

static int
expr_type_from_name(const char *name, asn1p_expr_type_e *out) {
	int t;
	for(t = A1TC_INVALID; t < (int)ASN_EXPR_TYPE_MAX; t++) {
		const char *s;
		/* Skip holes in the enumeration */
		if(t > A1TC_INSTANCE && t < ASN_CLASSFIELD_MASK) continue;
		if(t > A1TC_CLASSFIELD_OSFS && t < ASN_CONSTR_MASK) continue;
		if(t > ASN_CONSTR_OPEN_TYPE && t < ASN_BASIC_MASK) continue;
		if(t > ASN_BASIC_GeneralizedTime && t < ASN_STRING_KM_MASK) continue;
		if(t > ASN_STRING_BMPString && t < ASN_STRING_NKM_MASK) continue;
		s = asn1json_type_name((asn1p_expr_type_e)t);
		if(s && strcmp(s, name) == 0) {
			*out = (asn1p_expr_type_e)t;
			return 0;
		}
	}
	return -1;
}

static int
build_expr(ctx_t *c, asn1p_module_t *mod, asn1p_expr_t *e, const jn_t *o) {
	const jn_t *j;
	asn1c_integer_t line = 0;
	size_t i;

	if(need(c, o, JN_OBJ, "expression")) return -1;

	if((j = jget(o, "name"))) {
		e->Identifier = jstrdup(c, j, "expression name");
		if(!e->Identifier) return -1;
	}
	if(jint_key(c, o, "line", &line) < 0) return -1;
	e->_lineno = (int)line;

	j = jget(o, "meta");
	if(need(c, j, JN_STR, "expression meta")) return -1;
	if(expr_meta_from_name(j->s, &e->meta_type)) {
		cerr(c, "unknown expression meta \"%s\"", j->s);
		return -1;
	}
	j = jget(o, "type");
	if(need(c, j, JN_STR, "expression type")) return -1;
	if(expr_type_from_name(j->s, &e->expr_type)) {
		cerr(c, "unknown expression type \"%s\"", j->s);
		return -1;
	}

	if((j = jget(o, "reference"))) {
		e->reference = build_ref(c, mod, j);
		if(!e->reference) return -1;
	}

	if((j = jget(o, "parameters"))) {
		if(need(c, j, JN_ARR, "parameters")) return -1;
		e->lhs_params = asn1p_paramlist_new(e->_lineno);
		if(!e->lhs_params) {
			cerr(c, "out of memory");
			return -1;
		}
		for(i = 0; i < j->n; i++) {
			const jn_t *p = j->v[i];
			const jn_t *gov = jget(p, "governor");
			char *arg;
			asn1p_ref_t *gref = NULL;
			int r;
			if(need(c, p, JN_OBJ, "parameter")) return -1;
			arg = jstrdup(c, jget(p, "argument"), "parameter argument");
			if(!arg) return -1;
			if(gov) {
				gref = build_ref(c, mod, gov);
				if(!gref) {
					free(arg);
					return -1;
				}
			}
			r = asn1p_paramlist_add_param(e->lhs_params, gref, arg);
			free(arg);
			asn1p_ref_free(gref);
			if(r) {
				cerr(c, "out of memory");
				return -1;
			}
		}
	}

	if((j = jget(o, "parameterSpecs"))) {
		if(need(c, j, JN_ARR, "parameterSpecs")) return -1;
		e->rhs_pspecs = asn1p_expr_new(e->_lineno, mod);
		if(!e->rhs_pspecs) {
			cerr(c, "out of memory");
			return -1;
		}
		for(i = 0; i < j->n; i++) {
			asn1p_expr_t *ch = asn1p_expr_new(0, mod);
			if(!ch) {
				cerr(c, "out of memory");
				return -1;
			}
			asn1p_expr_add(e->rhs_pspecs, ch);
			if(build_expr(c, mod, ch, j->v[i])) return -1;
		}
	}

	if((j = jget(o, "tag"))) {
		const jn_t *jc = jget(j, "class"), *jm = jget(j, "mode");
		asn1c_integer_t num = 0;
		if(need(c, j, JN_OBJ, "tag") || need(c, jc, JN_STR, "tag.class")
		   || need(c, jm, JN_STR, "tag.mode"))
			return -1;
		if(!strcmp(jc->s, "UNIVERSAL")) e->tag.tag_class = TC_UNIVERSAL;
		else if(!strcmp(jc->s, "APPLICATION")) e->tag.tag_class = TC_APPLICATION;
		else if(!strcmp(jc->s, "CONTEXT")) e->tag.tag_class = TC_CONTEXT_SPECIFIC;
		else if(!strcmp(jc->s, "PRIVATE")) e->tag.tag_class = TC_PRIVATE;
		else {
			cerr(c, "unknown tag class \"%s\"", jc->s);
			return -1;
		}
		if(!strcmp(jm->s, "IMPLICIT")) e->tag.tag_mode = TM_IMPLICIT;
		else if(!strcmp(jm->s, "EXPLICIT")) e->tag.tag_mode = TM_EXPLICIT;
		else if(!strcmp(jm->s, "DEFAULT")) e->tag.tag_mode = TM_DEFAULT;
		else {
			cerr(c, "unknown tag mode \"%s\"", jm->s);
			return -1;
		}
		if(jint_key(c, j, "number", &num) != 0) {
			if(!c->failed) cerr(c, "tag.number missing");
			return -1;
		}
		e->tag.tag_value = num;
	}

	if((j = jget(o, "constraints"))) {
		e->constraints = build_constraint(c, mod, j);
		if(!e->constraints) return -1;
	}
	if((j = jget(o, "combinedConstraints"))) {
		e->combined_constraints = build_constraint(c, mod, j);
		if(!e->combined_constraints) return -1;
	}

	if(opt_value(c, mod, o, "value", &e->value)) return -1;

	if((j = jget(o, "withSyntax"))) {
		e->with_syntax = build_wsyntx(c, j);
		if(!e->with_syntax) return -1;
	}

	if((j = jget(o, "marker"))) {
		int fl = EM_NOMARK;
		if(need(c, j, JN_OBJ, "marker")) return -1;
		if(jbool_key(j, "default")) {
			fl = EM_DEFAULT;
			if(opt_value(c, mod, j, "defaultValue", &e->marker.default_value))
				return -1;
		} else if(jbool_key(j, "optional")) {
			fl = EM_OPTIONAL;
		} else {
			if(jbool_key(j, "indirect")) fl |= EM_INDIRECT;
			if(jbool_key(j, "omitable")) fl |= EM_OMITABLE;
		}
		if(jbool_key(j, "unrecurse")) fl |= EM_UNRECURSE;
		e->marker.flags = (enum asn1p_expr_marker_e)fl;
	}

	e->unique = jbool_key(o, "unique");

	if((j = jget(o, "members"))) {
		if(need(c, j, JN_ARR, "members")) return -1;
		for(i = 0; i < j->n; i++) {
			asn1p_expr_t *ch = asn1p_expr_new(0, mod);
			if(!ch) {
				cerr(c, "out of memory");
				return -1;
			}
			asn1p_expr_add(e, ch);
			if(build_expr(c, mod, ch, j->v[i])) return -1;
		}
	}
	return 0;
}

static asn1p_xports_t *
build_xports(ctx_t *c, asn1p_module_t *mod, const jn_t *o, int is_import) {
	asn1p_xports_t *xp;
	const jn_t *j, *syms;
	size_t i;

	if(need(c, o, JN_OBJ, is_import ? "import" : "export")) return NULL;
	xp = asn1p_xports_new();
	if(!xp) {
		cerr(c, "out of memory");
		return NULL;
	}
	xp->xports_type = is_import ? XPT_IMPORTS : XPT_EXPORTS;
	if((j = jget(o, "fromModule"))) {
		xp->fromModuleName = jstrdup(c, j, "fromModule");
		if(!xp->fromModuleName) goto fail;
	}
	if((j = jget(o, "oid"))) {
		xp->identifier.oid = build_oid(c, j);
		if(!xp->identifier.oid) goto fail;
	}
	if(opt_value(c, mod, o, "identifierValue", &xp->identifier.value))
		goto fail;
	syms = jget(o, "symbols");
	if(need(c, syms, JN_ARR, "symbols")) goto fail;
	for(i = 0; i < syms->n; i++) {
		asn1p_expr_t *e = asn1p_expr_new(0, mod);
		if(!e) {
			cerr(c, "out of memory");
			goto fail;
		}
		e->Identifier = jstrdup(c, syms->v[i], "symbol");
		if(!e->Identifier) {
			asn1p_expr_free(e);
			goto fail;
		}
		e->expr_type = is_import ? A1TC_REFERENCE : A1TC_EXPORTVAR;
		TQ_ADD(&(xp->xp_members), e, next);
	}
	return xp;
fail:
	asn1p_xports_free(xp);
	return NULL;
}

static int
build_module(ctx_t *c, asn1p_t *asn, const jn_t *o) {
	asn1p_module_t *mod;
	const jn_t *j;
	size_t i;

	if(need(c, o, JN_OBJ, "module")) return -1;
	mod = asn1p_module_new();
	if(!mod) {
		cerr(c, "out of memory");
		return -1;
	}
	mod->asn1p = asn;
	TQ_ADD(&(asn->modules), mod, mod_next); /* Owned by asn from now on */

	mod->ModuleName = jstrdup(c, jget(o, "name"), "module name");
	if(!mod->ModuleName) return -1;
	if((j = jget(o, "sourceFile"))) {
		mod->source_file_name = jstrdup(c, j, "sourceFile");
	} else {
		mod->source_file_name = strdup(c->src);
	}
	if(!mod->source_file_name) {
		if(!c->failed) cerr(c, "out of memory");
		return -1;
	}
	if((j = jget(o, "oid"))) {
		mod->module_oid = build_oid(c, j);
		if(!mod->module_oid) return -1;
	}

	if((j = jget(o, "tagging"))) {
		if(need(c, j, JN_STR, "module tagging")) return -1;
		if(!strcmp(j->s, "AUTOMATIC")) mod->module_flags |= MSF_AUTOMATIC_TAGS;
		else if(!strcmp(j->s, "IMPLICIT")) mod->module_flags |= MSF_IMPLICIT_TAGS;
		else if(!strcmp(j->s, "EXPLICIT")) mod->module_flags |= MSF_EXPLICIT_TAGS;
		else if(strcmp(j->s, "DEFAULT")) {
			cerr(c, "unknown module tagging \"%s\"", j->s);
			return -1;
		}
	}
	if((j = jget(o, "instructions"))) {
		if(need(c, j, JN_ARR, "module instructions")) return -1;
		for(i = 0; i < j->n; i++) {
			const jn_t *s = j->v[i];
			if(need(c, s, JN_STR, "module instruction")) return -1;
			if(!strcmp(s->s, "TAG")) mod->module_flags |= MSF_TAG_INSTRUCTIONS;
			else if(!strcmp(s->s, "XER")) mod->module_flags |= MSF_XER_INSTRUCTIONS;
			else if(!strcmp(s->s, "unk")) mod->module_flags |= MSF_unk_INSTRUCTIONS;
			else {
				cerr(c, "unknown module instruction \"%s\"", s->s);
				return -1;
			}
		}
	}
	if(jbool_key(o, "extensibilityImplied"))
		mod->module_flags |= MSF_EXTENSIBILITY_IMPLIED;

	if((j = jget(o, "exports"))) {
		if(need(c, j, JN_ARR, "exports")) return -1;
		for(i = 0; i < j->n; i++) {
			asn1p_xports_t *xp = build_xports(c, mod, j->v[i], 0);
			if(!xp) return -1;
			TQ_ADD(&(mod->exports), xp, xp_next);
		}
	}
	if((j = jget(o, "imports"))) {
		if(need(c, j, JN_ARR, "imports")) return -1;
		for(i = 0; i < j->n; i++) {
			asn1p_xports_t *xp = build_xports(c, mod, j->v[i], 1);
			if(!xp) return -1;
			TQ_ADD(&(mod->imports), xp, xp_next);
		}
	}
	if((j = jget(o, "members"))) {
		if(need(c, j, JN_ARR, "members")) return -1;
		for(i = 0; i < j->n; i++) {
			asn1p_expr_t *e = asn1p_expr_new(0, mod);
			if(!e) {
				cerr(c, "out of memory");
				return -1;
			}
			if(build_expr(c, mod, e, j->v[i])) {
				asn1p_expr_free(e);
				return -1;
			}
			if(!e->Identifier) {
				asn1p_expr_free(e);
				cerr(c, "module member without a name");
				return -1;
			}
			asn1p_module_member_add(mod, e);
		}
	}
	return 0;
}

asn1p_t *
asn1json_parse_buffer(const char *buf, size_t len, const char *source_name) {
	ctx_t c;
	jn_t *root;
	asn1p_t *asn = NULL;
	const jn_t *mods;
	size_t i;

	memset(&c, 0, sizeof(c));
	c.src = source_name ? source_name : "<json>";
	c.base = c.p = buf;
	c.end = buf + len;

	/* Skip UTF-8 byte order mark */
	if(len >= 3 && (unsigned char)buf[0] == 0xEF
	   && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
		c.p += 3;

	root = parse_value(&c, 0);
	if(root) {
		skip_ws(&c);
		if(c.p != c.end) perr(&c, "trailing garbage after JSON document");
	}
	if(!root || c.failed) goto done;

	if(need(&c, root, JN_OBJ, "document")) goto done;
	mods = jget(root, "modules");
	if(need(&c, mods, JN_ARR, "modules")) goto done;
	asn = asn1p_new();
	if(!asn) {
		cerr(&c, "out of memory");
		goto done;
	}
	for(i = 0; i < mods->n; i++) {
		if(build_module(&c, asn, mods->v[i])) break;
	}
done:
	jn_free(root);
	if(c.failed) {
		fprintf(stderr, "%s\n", c.err);
		asn1p_delete(asn);
		errno = EINVAL;
		return NULL;
	}
	return asn;
}

asn1p_t *
asn1json_parse_file(const char *filename) {
	FILE *fp;
	char *buf = NULL;
	size_t len = 0, cap = 0;
	asn1p_t *asn;

	if(!filename) {
		errno = EINVAL;
		return NULL;
	}
	if(strcmp(filename, "-") == 0) {
		fp = stdin;
	} else {
		fp = fopen(filename, "rb");
		if(!fp) {
			fprintf(stderr, "%s: %s\n", filename, strerror(errno));
			return NULL;
		}
	}
	for(;;) {
		size_t r;
		if(cap - len < 4096) {
			size_t nc = cap ? cap * 2 : 65536;
			char *nb = (char *)realloc(buf, nc);
			if(!nb) {
				fprintf(stderr, "%s: out of memory\n", filename);
				free(buf);
				if(fp != stdin) fclose(fp);
				return NULL;
			}
			buf = nb;
			cap = nc;
		}
		r = fread(buf + len, 1, cap - len, fp);
		if(r == 0) break;
		len += r;
	}
	if(ferror(fp)) {
		fprintf(stderr, "%s: read error\n", filename);
		free(buf);
		if(fp != stdin) fclose(fp);
		return NULL;
	}
	if(fp != stdin) fclose(fp);

	asn = asn1json_parse_buffer(buf ? buf : "", len, filename);
	free(buf);
	return asn;
}
