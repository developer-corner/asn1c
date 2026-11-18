# asn1c 0.9.30: JSON serializer / de-serializer

Version 0.9.30 adds a JSON representation of the parsed ASN.1 syntax tree.
asn1c can now

* **write** the parsed (and optionally fixed) ASN.1 tree as JSON, and
* **read** such JSON instead of ASN.1 source text, then continue as usual
  (print it with `-E`, or compile it to C).

This makes it easy to inspect an ASN.1 specification with ordinary JSON tools,
to post-process or generate specifications programmatically, and to feed them
back into the compiler.

## Summary of changes

| Area | Change |
|------|--------|
| Command line | New switch `-J`: print the parsed tree as JSON (implies `-E`). |
| Command line | Input files whose name ends in `.json` (any case) are read as JSON; all other files are read as ASN.1 source. Both kinds may be mixed in one invocation. |
| Library | New files `libasn1print/asn1json.c`, `asn1json.h` (serializer) and `libasn1print/asn1jsonin.c` (de-serializer). Public API: `asn1json()`, `asn1json_parse_file()`, `asn1json_parse_buffer()`. |
| Build | Added to `libasn1print/Makefile.am`, `libasn1print.vcxproj` and its `.filters`. Plain C99, builds with GCC and MS Visual C. |
| Version | Raised from 0.9.29 to 0.9.30 (`configure.ac`, `win64/config.h`, man page, `ChangeLog`). |
| Banner | `asn1c -h` / `-v` now show "...with JSON serializer/de-serializer support." |

The new code is licensed under the same 2-clause BSD license as asn1c.

## Usage

### Write JSON from ASN.1 source

```bash
asn1c -J demo.asn1 > demo.json
```

`-J` implies `-E`: asn1c only parses the input and prints; nothing is compiled.
The JSON goes to standard output.

Add `-F` to run the semantic fixer first (references expanded, tags and
constraints resolved) and serialize the fixed tree:

```bash
asn1c -J -F demo.asn1 > demo.fixed.json
```

Like `-E -F`, this also needs the skeletons directory for the standard modules
(`-S <dir>`, if it is not found automatically). Put options such as `-S`
*before* the input files.

### Read JSON and use it like ASN.1 source

```bash
# Print it back as ASN.1 text
asn1c -E demo.json

# Compile it to C, exactly as if demo.asn1 had been given
asn1c -D out/ demo.json

# Mix ASN.1 and JSON inputs
asn1c -E base.asn1 extra.json
```

The input type is chosen purely by the file name extension (`.json`, any
letter case). A file named `-` is not treated as JSON.

### Round trip

```bash
asn1c -J demo.asn1 > demo.json
asn1c -E demo.json      # same text as: asn1c -E demo.asn1
```

For the 162 ASN.1 files of the asn1c test suite that parse, `-E` and `-E -F`
output was identical between the original source and its JSON round trip, and
for the 128 that compile, the generated C files were byte-identical. (The only
differences were in `converter-example.mk` and `Makefile.am.*`, which record
the input file name.)

### Errors

Malformed JSON or an invalid tree is reported on standard error and exits with
status 65 (`EX_DATAERR`), for example:

```
bad.json: unknown expression type "BOGUS"
Cannot parse "bad.json"
bad2.json: JSON syntax error at line 2: unexpected end of input
```

### Restrictions

* `-J` cannot be combined with `-print-...` or `-X`.
* JSON output of the unfixed tree (`-J`) is the closest to the ASN.1 source.
  A tree written with `-J -F` already has fixer results (for example
  `combinedConstraints`); that is accepted on input, but then run through the
  fixer again by the compiler, which is normally harmless.

## JSON format

The document is an object with a `generator` string and a `modules` array.

```
{ "generator": "asn1c-0.10.0", "modules": [ <module>, ... ] }
```

Standard modules loaded from the skeletons directory are not included.

**Module**: `name`, `sourceFile`, `oid` (array of `{name?, number?}`),
`tagging` (`AUTOMATIC`, `IMPLICIT`, `EXPLICIT` or `DEFAULT` for none given),
`instructions` (optional, `TAG`/`XER`/`unk`), `extensibilityImplied`,
`exports`, `imports` (each `{fromModule, oid?, identifierValue?, symbols[]}`)
and `members` (the module's definitions).

**Expression** (a type, value, value set, class, object, ...): `name` (omitted
for anonymous items), `line`, `meta` (`TYPE`, `TYPEREF`, `VALUE`, `VALUESET`,
`OBJECT`, `OBJECTCLASS`, `OBJECTFIELD`), `type` (`SEQUENCE`, `INTEGER`,
`UTF8String`, `REFERENCE`, ...), and as needed `reference`, `parameters`,
`parameterSpecs`, `tag` (`class`, `mode`, `number`), `constraints`,
`combinedConstraints`, `value`, `withSyntax`, `marker`, `unique` and nested
`members`.

**Marker**: `{"optional": true}` or `{"default": true, "defaultValue": <value>}`,
plus rarely `indirect`, `omitable`, `unrecurse`.

**Value**: always an object with a `kind`: `INTEGER`, `REAL`, `STRING`,
`BITVECTOR`, `TUPLE`, `QUADRUPLE`, `REFERENCE`, `TYPE`, `VALUESET`,
`CHOICE_IDENTIFIER`, `NULL`, `TRUE`, `FALSE`, `MIN`, `MAX`, `UNPARSED`,
`NOVALUE`, normally with a `value` member. Integers are written as exact
decimal numbers, which can exceed 64 bits.

**Constraint**: `type` (e.g. `SET`, `UNION`, `SizeConstraint`, `ValueRange`,
`SingleValue`, `PermittedAlphabet`), optional `range` (`..`, `<..`, `..<`,
`<..<`) for ranges, `presence`, `line`, `value`, `rangeStart`, `rangeStop`,
`containedSubtype` and nested `elements`.

### Example

`demo.asn1`:

```asn1
Demo DEFINITIONS AUTOMATIC TAGS ::= BEGIN
Point ::= SEQUENCE {
  x INTEGER (0..100),
  label UTF8String OPTIONAL
}
END
```

`asn1c -J demo.asn1` produces:

```json
{
  "generator": "asn1c-0.10.0",
  "modules": [
    {
      "name": "Demo",
      "sourceFile": "demo.asn1",
      "tagging": "AUTOMATIC",
      "extensibilityImplied": false,
      "exports": [],
      "imports": [],
      "members": [
        {
          "name": "Point",
          "line": 3,
          "meta": "TYPE",
          "type": "SEQUENCE",
          "members": [
            {
              "name": "x",
              "line": 3,
              "meta": "TYPE",
              "type": "INTEGER",
              "constraints": {
                "type": "SET",
                "line": 3,
                "elements": [
                  {
                    "type": "ValueRange",
                    "range": "..",
                    "line": 3,
                    "rangeStart": { "kind": "INTEGER", "value": 0 },
                    "rangeStop": { "kind": "INTEGER", "value": 100 }
                  }
                ]
              }
            },
            {
              "name": "label",
              "line": 4,
              "meta": "TYPE",
              "type": "UTF8String",
              "marker": { "optional": true }
            }
          ]
        }
      ]
    }
  ]
}
```

(Shown with some small objects put on one line for brevity; the real output
has one key per line.)

## Using the library API

```c
#include <asn1parser.h>
#include <asn1json.h>

asn1p_t *asn = asn1json_parse_file("demo.json");   /* or ..._parse_buffer() */
if(asn) {
    asn1json(asn, AJF_NOFLAGS, stdout);            /* AJF_COMPACT: no whitespace */
    asn1p_delete(asn);
}
```

`asn1json()` returns 0 on success and -1 on failure. The parse functions return
`NULL` after printing a diagnostic to standard error.

## Limitations

The JSON describes the syntax tree as the parser builds it. Information
Object Class tables (`ioc_table`) and other data that only the fixer or
compiler derives internally are not serialized; they are recomputed when the
fixer runs on the de-serialized tree. Source line numbers are preserved for
expressions and constraints but not for import/export symbol names.
