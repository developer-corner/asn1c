% asn1c(1) ASN.1 Compiler
% Lev Walkin <vlm@lionet.info>
% 2017-09-01

# NAME

asn1c -- the ASN.1 Compiler

# SYNOPSIS

asn1c [**-E** [**-F**] | **-J** [**-F**] | **-P** | **-R**] \
      [**-S***dir*] [**-X**] \
      [**-W***debug-*...] [**-f***option*] [**-gen-***option*] 
      [**-pdu**={**all**|**auto**|*Type*}] \
      [**-print-***option*] \
      *input-filenames*...

# DESCRIPTION

asn1c compiles ASN.1 specifications into a set of
target language (C/C++) encoders and decoders for BER, DER, PER, XER, OER
and other encoding rules.

The input files are normally ASN.1 specifications. Input files whose name
ends in `.json` (in any letter case) are instead read as the JSON
representation of a parsed ASN.1 specification, as produced by the **-J**
option. Both kinds of files may be given together.

# OPTIONS

## Stage Selection Options

-E
:   Run the parsing stage only. Print the reconstructed ASN.1 text.

-F
:   Used together with **-E**,
    instructs the compiler to stop after the ASN.1 syntax
    tree fixing stage and dump the reconstructed ASN.1 specification
    to the standard output.

-J
:   Run the parsing stage only (like **-E**), but print the parsed ASN.1
    syntax tree as JSON to the standard output instead of ASN.1 text.
    Together with **-F** the tree is fixed first, as with **-E -F**.
    The output can be read back by giving the file, named with a `.json`
    extension, as an input file. Cannot be combined with **-X** or
    **-print-**... options.

-P
:   Dump the compiled output to the standard output instead of creating the
    target language files on disk.

-R
:   Restrict the compiler to generate only the ASN.1 tables,
    omitting the usual support code.

-S *directory*
:   Use the specified directory with ASN.1 skeleton files.

-X
:   Generate an XML DTD schema for the specified ASN.1 files.

## Warning Options

-Werror
:   Treat warnings as errors; abort if any warning is produced.

-Wdebug-lexer
:   Enable lexer debugging during the ASN.1 lexing stage.

-Wdebug-parser
:   Enable lexer debugging during the ASN.1 parsing stage.

-Wdebug-fixer
:   Enable ASN.1 syntax tree fixer debugging during the fixing stage.

-Wdebug-compiler
:   Enable debugging during the actual compile time.

## Language Options

-fbless-SIZE
:   Allow `SIZE()` constraint for `INTEGER`, `ENUMERATED`,
    and other types for which this constraint is normally prohibited
    by the standard.
    This is a violation of ASN.1 standard, and the compiler may
    fail to produce a meaningful code.

-fcompound-names
:   Using this option prevents name collisions in the target source code
    by using complex names for target language structures.
    (Name collisions may occur if the ASN.1 module reuses the same identifiers
    in multiple contexts).

-findirect-choice
:   When generating code for a `CHOICE` type, compile the `CHOICE` members
    as indirect pointers instead of declaring them inline.
    Consider using this option together with **-fno-include-deps**
    to prevent circular references.

-fincludes-quoted
:   Refer to header files in `#include`s using **"**double**"** instead of **\<**angle**>** quotes.

-fknown-extern-type=*name*
:   Pretend the specified type is known.
    The compiler will assume the target language source files
    for the given type have been provided manually.

-fline-refs
:   Include ASN.1 module's line numbers in generated code comments.

-fno-constraints
:   Do not generate ASN.1 subtype constraint checking code.
    This may make a shorter executable.

-fno-include-deps
:   Do not generate courtesy #include lines for non-critical type dependencies.
    Helps prevent namespace collisions.

-funnamed-unions
:   Enable unnamed unions in the definitions of target language's structures.

-fwide-types
:   Use the unbounded size data types (`INTEGER_t`, `ENUMERATED_t`, `REAL_t`)
    by default, instead of using the native machine's data types (long, double).

## Codecs Generation Options

-gen-OER
:   Generate the Octet Encoding Rules (OER) support code.

-gen-PER
:   Generate the Packed Encoding Rules (PER) support code.

-pdu={all|auto|*Type*}
:   Create a PDU table for specified types, or discover Protocol Data Units
    automatically. In case of **-pdu=all**,
    all ASN.1 types defined in all modules will form a PDU table.
    In case of **-pdu=auto**, all types not referenced by any other type will
    form a PDU table.
    If *Type* is an ASN.1 type identifier, the identifier is added to
    the generated PDU table.
    The last form may be specified multiple times to add any number of PDUs.

## Output Options

-print-constraints
:   When **-EF** options are also specified,
    this option forces the compiler to explain its internal understanding
    of subtype constraints.

-print-lines
:   Generate "`-- #line`" comments in **-E** output.

# TRANSFER SYNTAXES

The ASN.1 family of standards define a number of ways to encode data,
including byte-oriented (e.g., BER), bit-oriented (e.g., PER),
and textual (e.g., XER). Some encoding variants (e.g., DER) are just stricter
variants of the more general encodings (e.g., BER).

The interoperability table below specifies which API functions can be used
to exchange data in a compatible manner. If you need to _produce_ data
conforming to the standard specified in the column 1,
use the API function in the column 2.
If you need to _process_ data conforming to the standard(s) specified in the
column 3, use the API function specified in column 4.
See the `asn1c-usage.pdf` for details.

-------------------------------------------------------------
Encoding       API function       Understood by API function
-------------- ------------------ ------------- -------------
BER            der_encode()       BER           ber_decode()

DER            der_encode()       DER, BER      ber_decode()

CER            _not supported_    CER, BER      ber_decode()

BASIC-OER      oer_encode()       *-OER         oer_decode()

CANONICAL-OER  oer_encode()       *-OER         oer_decode()

BASIC-UPER     uper_encode()      *-UPER        uper_decode()

CANONICAL-UPER uper_encode()      *-UPER        uper_decode()

*-APER         _not supported_    *-APER        _not supported_

BASIC-XER      xer_encode(...)    *-XER         xer_decode()

CANONICAL-XER  xer_encode         *-XER         xer_decode()
               (XER_F_CANONICAL)
-------------------------------------------------------------

*) Asterisk means both BASIC and CANONICAL variants.


# JSON INPUT AND OUTPUT

The **-J** option serializes the parsed ASN.1 tree as a JSON document with
a `generator` string and a `modules` array. Each module lists its name, OID,
tagging mode, imports, exports and definitions; each definition carries its
type, tags, constraints, values and nested members. Reading such a document
(any input file named `*.json`) rebuilds the same tree, so that **-E**, **-F**
and the compiler behave as if the original ASN.1 file had been given.

    asn1c -J spec.asn1 > spec.json       # ASN.1 to JSON
    asn1c -E spec.json                   # JSON back to ASN.1 text
    asn1c -D out/ spec.json              # compile the JSON directly
    asn1c -E base.asn1 extra.json        # mix ASN.1 and JSON inputs

Malformed JSON is reported on the standard error and asn1c exits with
status 65. See the file `ASN1_JSON.md` for a description of the format.

# SEE ALSO

`unber`(1), `enber`(1).

