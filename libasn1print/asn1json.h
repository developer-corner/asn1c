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
#ifndef	ASN1JSON_H
#define	ASN1JSON_H

enum asn1json_flags {
	AJF_NOFLAGS,
	AJF_COMPACT	= 0x01,	/* No indentation / newlines */
};

/*
 * Serialize the parsed (and optionally fixed) ASN.1 syntax tree as JSON
 * to the given stream (stdout if fp is NULL).
 * Returns 0 on success, -1 on failure (errno is set).
 */
int asn1json(asn1p_t *asn, enum asn1json_flags flags, FILE *fp);

/*
 * Read a JSON document produced by asn1json() and rebuild the ASN.1
 * syntax tree (as the ASN.1 parser would have produced it).
 * On failure returns NULL and prints a diagnostic to stderr.
 */
asn1p_t *asn1json_parse_file(const char *filename);
asn1p_t *asn1json_parse_buffer(const char *buf, size_t len,
                               const char *source_name);

/* Name tables shared by the serializer and the de-serializer */
const char *asn1json_meta_name(asn1p_expr_meta_e);
const char *asn1json_type_name(asn1p_expr_type_e);
const char *asn1json_lex_name(enum asn1p_ref_lex_type_e);
const char *asn1json_presence_name(enum asn1p_constr_pres_e);
const char *asn1json_tag_class_name(int);
const char *asn1json_tag_mode_name(int);
const char *asn1json_tagging_name(asn1p_module_flags_e);

#endif	/* ASN1JSON_H */
