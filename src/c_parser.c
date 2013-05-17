/* c_parser.c - a C 99 parser */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*  Copyright 2000, 2001 Dibyendu Majumdar.
 *  Author : Dibyendu Majumdar
 *  Email  : dibyendu@mazumdar.demon.co.uk  
 *  Website: www.mazumdar.demon.co.uk
 *
 */

/*
 * 12 Jan 2000 - Started
 * 13 Jan 2000 - Completed 1st version
 * 14-15 Jan 2000 - Fixed many bugs, added TRACE()
 * 16 Jan 2001 - Streamlined TokMap, and token test macros
 * 16 Jan 2001 - Documented FIRST sets, ambiguities
 * 17 Jan 2001 - Started using CVS
 * 17 Jan 2001 - Imported typedef handling from UPS sources
 * 18 Jan 2001 - First version that can parse typedefs
 *               BUG: a typedef name can be hidden by declaring a function,
 *                    variable, or enumerator with the same name. Currently
 *                    cannot handle this.
 * 19 Jan 2001 - Started work on C99 grammer.
 * 19 Jan 2001 - Added support for designators in initializers
 * 19 Jan 2001 - Modified external declaration so that these can no longer
 *               begin without a declaration specifier.
 * 19 Jan 2001 - Selection (if, switch) and iteration (for, do, while)
 *               statements now enclosed in blocks, sub statements also 
 *               enclosed in blocks.
 * 19 Jan 2001 - First expression of for can now be a declaration.
 * 19 Jan 2001 - Compound statements can now have declartions and statements
 *               interspersed.
 * 20 Jan 2001 - Tentative code for parsing compound literals.
 * 20 Jan 2001 - Structured trace messages to show parser in action.
 * 16 Nov 2001 - Tidied up for release. As you can see I have not worked on this since Jan, 2001.
 *               It is unlikely I will be able to spend time on this in the immediate future. Hence,
 *               I am releasing it "as is".
 */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "c_lex.h"
#include "list.h"

/***
* Various FIRST SETS
* ------------------
*   declarator
*        IDENTIFIER * ( 
*   declaration_specifier
*        AUTO REGISTER STATIC EXTERN TYPEDEF VOID CHAR
*        SHORT INT LONG FLOAT DOUBLE SIGNED UNSIGNED STRUCT UNION
*        ENUM TYPEDEF_NAME CONST VOLATILE
*   declaration
*        declaration_specifier
*   external_declaration
*        declaration declarator
*   expression
*        ( ++ -- & * + - ~ ! SIZEOF IDENTIFIER FLOATING_CONSTANT
*        INTEGER_CONSTANT CHARACTER_CONSTANT STRING_CONSTANT
*   statement
*        expression ; { IF SWITCH WHILE DO FOR GOTO CONTINUE BREAK
*        RETURN CASE DEFAULT IDENTIFIER
*/

/***
* Ambiguity
*    1) A declaration and a function definition look alike.
*    2) An expression and a statement look alike.
*    3) A type cast and an expression look alike.
*    4) A labeled statment and an expression statement look alike.
*    5) An assignment and a conditional expression look alike.
*/

#define is_assign_operator(tok) \
	(TokMap[tok] & TOK_ASSGNOP)
#define is_type_name(tok) \
	(TokMap[tok] & TOK_DECL_SPEC || tok == IDENTIFIER && Cursym != 0 && Cursym->object_type == OBJ_TYPEDEF_NAME)
#define is_declaration(tok) \
	is_type_name(tok)
#ifdef C89
#define is_external_declaration(tok) \
	(is_declaration(tok) || tok == STAR || tok == LPAREN || tok == IDENTIFIER)
#else
#define is_external_declaration(tok) \
	is_declaration(tok)
#endif
#define is_expression(tok) \
	(TokMap[tok] & TOK_EXPR)
#define is_statement(tok) \
	(TokMap[tok] & TOK_STMT)
#define is_function_body(tok) \
	(tok == LBRACE || (is_declaration(tok) && tok != TYPEDEF))

#if 1
#define TRACEIN(s) do { if (DebugLevel == 2) printf("%*s%s {\n", TraceLevel, "", s); fflush(stdout); TraceLevel++; } while(0);
#define TRACEOUT(s) do { --TraceLevel; if (DebugLevel == 2) printf("%*s} (%s)\n", TraceLevel, "", s); fflush(stdout); } while(0);
#else
#define TRACEIN(s) 
#define TRACEOUT(s)
#endif

enum {
	TOK_UNKNOWN = 0,
	TOK_EXPR = 1,
	TOK_CONSTANT = 2,
	TOK_TYPE_SPECIFIER_QUALIFIER = 4,
	TOK_TYPE_SPECIFIER = 8,
	TOK_TYPE_QUALIFIER = 16,
	TOK_TAG = 32,
	TOK_STRUCT = 64,
	TOK_STORAGE_CLASS = 256,
	TOK_STMT = 1024,
	TOK_DECL_SPEC = 2048,
	TOK_ASSGNOP = 4096,
};

enum {
	LEVEL_GLOBAL = 0,
	LEVEL_FUNCTION = 1,
	LEVEL_STATEMENT = 2
};

enum {
	OBJ_TYPEDEF_NAME = 1,
	OBJ_FUNCTION_DECL = 2,
	OBJ_FUNCTION_DEFN = 4,
	OBJ_ENUMERATOR = 8,
	OBJ_VARIABLE = 16,
	OBJ_PARAMETER = 32,
	OBJ_IDENTIFIER = 2+4+8+16+32
};

typedef struct symbol_t {
	link_t link;
	const char *name;
	int storage_class;
	int object_type;	
} symbol_t;

typedef struct {
	link_t link;
	int level;		/* nesting level */
	list_t symbols;		/* list of symbols */
} symtab_t;

static unsigned long TokMap[BADTOK];

static int DebugLevel = 0;
static int TraceLevel = 0;

static	int Level = 0;
static	int Saw_ident = 0;
static	int Is_func = 0;
static	token_t	tok;
static	int Parsing_struct = 0;
static	int Parsing_oldstyle_parmdecl = 0;
static	int Storage_class[100];
static	int stack_ptr = -1;

static list_t identifiers;		/* head of the identifiers list */
static list_t labels;
static list_t types;
static symtab_t *Cursymtab;		/* current symbol table */
static symtab_t *Curlabels;		
static symtab_t *Curtypes;		

static symbol_t *Cursym = 0;

static void
init_tokmap(void);

static const char *
object_name(int object_type);

static symtab_t *
new_symbol_table(list_t *owner);

static void
init_symbol_table(void);

static symbol_t *
find_symbol(symtab_t *tab, const char *name, int all_scope);

static void
enter_scope(void);

static void
exit_scope(void);

static void
install_symbol(const char *name, int storage_class, int object_type);

static void
match(token_t expected_tok);

static bool
check_not_typedef(void);

static void
constant_expression(void);

static void
expression(void);

static void
primary_expression(void);

static void
postfix_operator(void);

static void
postfix_operators(void);

static void
sizeof_expression(void);

static void
unary_expression(void);

static void
multiplicative_expression(void);

static void
additive_expression(void);

static void
shift_expression(void);

static void
relational_expression(void);

static void
equality_expression(void);

static void
and_expression(void);

static void
exclusive_or_expression(void);

static void
inclusive_or_expression(void);

static void
logical_and_expression(void);

static void
logical_or_expression(void);

static void
conditional_expression(void);

static void
assignment_expression(void);

static void
labeled_statement(void);

static void
case_statement(void);

static void
default_statement(void);

static void
if_statement(void);

static void
switch_statement(void);

static void
while_statement(void);

static void
do_while_statement(void);

static void
for_statement(void);

static void
break_statement(void);

static void
continue_statement(void);

static void
goto_statement(void);

static void
return_statement(void);

static void
empty_statement(void);

static void
expression_statement(void);

static void
statement(void);

static void
compound_statement(void);

static void
enumerator(void);

static void
enum_specifier(void);

static void
member(void);

static void
members(void);

static void
struct_or_union_specifier(void);

static void
type_name(void);

static void
declaration_specifiers(int no_storage_class);

static void
pointer(void);

static void
direct_declarator(int abstract);

static void 
parameter_list(int *new_style);

static void
suffix_declarator(void);

static void
declarator(int abstract);

static void
designator(void);

static void
initializer(int recurse);

static void
function_definition(void);

static int
init_declarator(int check_if_function);

static void
declaration(void);

static void
translation_unit(void);

static const char *
mygetline(char *arg);

static void
init_tokmap(void)
{
	TokMap[ FLOATING_CONSTANT ] = TOK_CONSTANT|TOK_EXPR|TOK_STMT ;
	TokMap[ INTEGER_CONSTANT ] = TOK_CONSTANT|TOK_EXPR|TOK_STMT ;
	TokMap[ STRING_CONSTANT ] = TOK_CONSTANT|TOK_EXPR|TOK_STMT ;
	TokMap[ CHARACTER_CONSTANT ] = TOK_CONSTANT|TOK_EXPR|TOK_STMT ;
	TokMap[ IDENTIFIER ] = TOK_EXPR|TOK_STMT ;
	TokMap[ SIZEOF ] = TOK_EXPR|TOK_STMT ;
	TokMap[ AND ] = TOK_EXPR|TOK_STMT ;
	TokMap[ PLUSPLUS ] = TOK_EXPR|TOK_STMT ;
	TokMap[ MINUSMINUS ] = TOK_EXPR|TOK_STMT ;
	TokMap[ STAR ] = TOK_EXPR|TOK_STMT ;
	TokMap[ PLUS ] = TOK_EXPR|TOK_STMT ;
	TokMap[ MINUS ] = TOK_EXPR|TOK_STMT ;
	TokMap[ TILDE ] = TOK_EXPR|TOK_STMT ;
	TokMap[ LPAREN ] = TOK_EXPR|TOK_STMT ;
	TokMap[ NOT ] = TOK_EXPR|TOK_STMT ;
	TokMap[ TYPEDEF_NAME ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC;
	TokMap[ CHAR ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ FLOAT ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ DOUBLE ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ SHORT ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ INT ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ UNSIGNED ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ SIGNED ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ VOID ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ STRUCT ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_STRUCT|TOK_TAG|TOK_DECL_SPEC ;
	TokMap[ UNION ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_STRUCT|TOK_TAG|TOK_DECL_SPEC ;
	TokMap[ ENUM ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TAG|TOK_DECL_SPEC ;
	TokMap[ LONG ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_SPECIFIER|TOK_DECL_SPEC ;
	TokMap[ CONST ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_QUALIFIER|TOK_DECL_SPEC ;
	TokMap[ VOLATILE ] = TOK_TYPE_SPECIFIER_QUALIFIER|TOK_TYPE_QUALIFIER|TOK_DECL_SPEC ;
	TokMap[ STATIC ] = TOK_STORAGE_CLASS|TOK_DECL_SPEC ;
	TokMap[ EXTERN ] = TOK_STORAGE_CLASS|TOK_DECL_SPEC ;
	TokMap[ AUTO ] = TOK_STORAGE_CLASS|TOK_DECL_SPEC ;
	TokMap[ REGISTER ] = TOK_STORAGE_CLASS|TOK_DECL_SPEC ;
	TokMap[ TYPEDEF ] = TOK_STORAGE_CLASS|TOK_DECL_SPEC ;
	TokMap[ IF ] = TOK_STMT ;
	TokMap[ BREAK ] = TOK_STMT ;
	TokMap[ CASE ] = TOK_STMT ;
	TokMap[ CONTINUE ] = TOK_STMT ;
	TokMap[ DEFAULT ] = TOK_STMT ;
	TokMap[ DO ] = TOK_STMT ;
	TokMap[ ELSE ] = TOK_STMT ;
	TokMap[ FOR ] = TOK_STMT ;
	TokMap[ GOTO ] = TOK_STMT ;
	TokMap[ RETURN ] = TOK_STMT ;
	TokMap[ SWITCH ] = TOK_STMT ;
	TokMap[ WHILE ] = TOK_STMT ;
	TokMap[ LBRACE ] = TOK_STMT;
	TokMap[ SEMI ] = TOK_STMT; 
	TokMap[ EQUALS ] = TOK_ASSGNOP ;
	TokMap[ PLUS_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ MINUS_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ STAR_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ SLASH_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ PERCENT_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ LSHIFT_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ RSHIFT_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ AND_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ XOR_EQUALS ] = TOK_ASSGNOP ;
	TokMap[ OR_EQUALS ] = TOK_ASSGNOP ;

}

static const char *
object_name(int object_type)
{
	const char *name;

	switch(object_type) {
	case OBJ_TYPEDEF_NAME: name = "typedef"; break;
	case OBJ_FUNCTION_DECL: name = "function_decl"; break;
	case OBJ_FUNCTION_DEFN: name = "function_defn"; break;
	case OBJ_ENUMERATOR: name = "enumerator"; break;
	case OBJ_VARIABLE: name = "variable"; break;
	case OBJ_PARAMETER: name = "parameter"; break;
	case OBJ_IDENTIFIER: name = "identifier"; break;
	default: name = "invalid"; break;
	}
	return name;
}

static symtab_t *
new_symbol_table(list_t *owner)
{
	symtab_t *tab;

	tab = calloc(1, sizeof(symtab_t));
	list_init(&tab->symbols);
	list_append(owner, tab);
	return tab;
}

static void
init_symbol_table(void)
{
	list_init(&identifiers);
	Cursymtab = new_symbol_table(&identifiers);
	list_init(&labels);
	list_init(&types);
	Curtypes = new_symbol_table(&types);
}

static symbol_t *
find_symbol(symtab_t *tab, const char *name, int all_scope)
{
	symbol_t *sym;

	assert(tab != 0);
	for (sym = (symbol_t *)list_first(&tab->symbols);
		sym != 0;
		sym = (symbol_t *)list_next(&tab->symbols, sym)) {
		if (strcmp(sym->name, name) == 0) {
			return sym;
		}
	}
	if (!all_scope)
		return 0;
	tab = (symtab_t *)list_prev(&identifiers, tab);
	if (tab == 0)
		return 0;
	return find_symbol(tab, name, all_scope);
}

static void
enter_scope(void)
{
	symtab_t *tab;

	Level++;
	if (DebugLevel == 3) { 
		printf("%*sEntering scope %d\n", TraceLevel, "", Level); 
	}
	if (Level == LEVEL_STATEMENT) {
		/* Although we increment the level when we parse 
		 * function parameters, and again when we enter the
		 * function body, ANSI C requires these to be in the same
		 * scope.
		 */
		return;
	}
	/*
	tab = calloc(1, sizeof(symtab_t));
	list_init(&tab->symbols);
	list_append(&identifiers, tab);
	Cursymtab = tab;
	*/
	Cursymtab = new_symbol_table(&identifiers);
}

static void
exit_scope(void)
{
	symtab_t *tab;

	if (DebugLevel == 3) { 
		printf("%*sExiting scope %d\n", TraceLevel, "", Level); 
	}
	Level--;
	/* Although we increment the Level when we parse 
	 * function parameters, and again when we enter the
	 * function body, ANSI C requires these to be in the same
	 * scope.
	 */
	if (Level == LEVEL_FUNCTION)
		return;
	tab = Cursymtab;
	Cursymtab = (symtab_t *)list_prev(&identifiers, Cursymtab);
	assert(Cursymtab != 0);
	list_remove(&identifiers, tab);
}

static void
install_symbol(const char *name, int storage_class, int object_type)
{
	symbol_t *sym;

	sym = find_symbol(Cursymtab, name, 0);
	if (sym != 0) {
		fprintf(stderr, "Error: redeclaration of symbol %s as %s\n", name,
			object_name(object_type));
		fprintf(stderr, "Error: previously declared as %s\n", 
			object_name(sym->object_type));
		/* fprintf(stderr, "Level = %d\n", Level); */
		/* exit(1); */
	}
	if (DebugLevel == 3) {
		printf("%*sInstalling %s name %s\n", TraceLevel, "", object_name(object_type), name);
		if (sym) {
			printf("%*s\tOverriding %s name %s\n", TraceLevel, "", object_name(sym->object_type), sym->name);
		}
	}
	sym = safe_calloc(1, sizeof(symbol_t));
	sym->name = string_copy(name, strlen(name));
	sym->storage_class = storage_class;
	sym->object_type = object_type;
	list_append(&Cursymtab->symbols, sym);
}	
	
static void
match(token_t expected_tok)
{
	if (tok != expected_tok) {
		printf("Parse failed: Expected %s, ", tokname(expected_tok));
		printf("got %s\n", tokname(tok));
		/* printf("Level = %d\n", Level); */
		exit(1);
	}
	else {
		if (DebugLevel) {
			const char *cp = 0;
			if (TokMap[tok] & TOK_CONSTANT) {
				cp = Lexeme->constant->co_val;
			}
			else if (tok == IDENTIFIER) {
				cp = Lexeme->identifier->id_name;
			}
			if (cp == 0)
				printf("%*s[%s]\n", TraceLevel, "", tokname(tok)); 
			else
				printf("%*s[%s(%s)]\n", TraceLevel, "", tokname(tok), cp); 
		}
		tok = lex_get_token();
	}
}

static bool
check_not_typedef(void)
{
	return (Cursym == 0 || Cursym->object_type != OBJ_TYPEDEF_NAME);
}

static void
constant_expression(void)
{
	TRACEIN("constant_expression");
	conditional_expression();
	/* fold constant */
	TRACEOUT("constant_expression");
}

static void
expression(void)
{
	TRACEIN("expression");

	if (!is_expression(tok)) {
		TRACEOUT("expression");
		return;
	}

	assignment_expression();
	while (tok == COMMA) {
		match(COMMA);
		assignment_expression();
	}
	TRACEOUT("expression");
}

static void
primary_expression(void)
{
	TRACEIN("primary_expression");
	if (tok == IDENTIFIER) {
		check_not_typedef();
		match(IDENTIFIER);
	}
	else if (TokMap[tok] & TOK_CONSTANT) {
		match(tok);
	}
	/* parenthesized expression handled in unary_expression() */
	TRACEOUT("primary_expression");
}

static void
postfix_operator(void)
{
	TRACEIN("postfix_operator");
	if (tok == LBRAC) {
		match(LBRAC);
		expression();
		match(RBRAC);
	}
	else if (tok == LPAREN) {
		match(LPAREN);
		if (tok != RPAREN) {
			assignment_expression();
			while (tok == COMMA) {
				match(COMMA);
				assignment_expression();
			}
		}
		match(RPAREN);
	}
	else if (tok == DOT || tok == ARROW) {
		match(tok);
		match(IDENTIFIER);
	}
	else if (tok == PLUSPLUS || tok == MINUSMINUS) {
		match(tok);
	}
	TRACEOUT("postfix_operator");
}

static void
postfix_operators(void)
{
	TRACEIN("postfix_operators");
	while (tok == LBRAC || tok == LPAREN || tok == DOT ||
		tok == ARROW || tok == PLUSPLUS || tok == MINUSMINUS) {
		postfix_operator();
	}
	TRACEOUT("postfix_operators");
}

static void
sizeof_expression(void)
{
	TRACEIN("sizeof_expression");
	
	match(SIZEOF);
	if (tok == LPAREN) {
		int found_typename = 0;
		match(LPAREN);
		if (is_type_name(tok)) {
			type_name();
			found_typename = 1;
		}
		else {
			expression();
		}
		match(RPAREN);
#if 1 /* as per comp.std.c */
		if (found_typename && tok == LBRACE) {
			initializer(0);
			postfix_operators();
		}
#endif
		else if (!found_typename) {
			postfix_operators();
		}
	}
	else {
		unary_expression();
	}
	TRACEOUT("sizeof_expression");
}

static void
unary_expression(void)
{
	TRACEIN("unary_expression");
	if (tok == SIZEOF) {
		sizeof_expression();
	}
	else if (tok == LPAREN) {
		int found_typename = 0;
		match(LPAREN);
		if (is_type_name(tok)) {
			type_name();
			found_typename = 1;
		}
		else {
			expression();
		}
		match(RPAREN);
		if (found_typename && tok == LBRACE) {
			initializer(0);
			postfix_operators();
		}
		else if (!found_typename) {
			postfix_operators();
		}
		else {
			unary_expression();
		}
	}
	else if (tok == PLUSPLUS || tok == MINUSMINUS || tok == AND
		|| tok == STAR || tok == PLUS || tok == MINUS
		|| tok == TILDE || tok == NOT) {
		match(tok);
		unary_expression();
	}
	else {
		primary_expression();
		postfix_operators();
	}
	TRACEOUT("unary_expression");
}

static void
multiplicative_expression(void)
{
	TRACEIN("multiplicative_expression");
	unary_expression();
	while (tok == STAR || tok == SLASH || tok == PERCENT) {
		match(tok);
		unary_expression();
	}
	TRACEOUT("multiplicative_expression");
}

static void
additive_expression(void)
{
	TRACEIN("additive_expression");
	multiplicative_expression();
	while (tok == PLUS || tok == MINUS) {
		match(tok);
		multiplicative_expression();
	}
	TRACEOUT("additive_expression");
}

static void
shift_expression(void)
{
	TRACEIN("shift_expression");
	additive_expression();
	while (tok == LSHIFT || tok == RSHIFT) {
		match(tok);
		additive_expression();
	}
	TRACEOUT("shift_expression");
}

static void
relational_expression(void)
{
	TRACEIN("relational_expression");
	shift_expression();
	while (tok == GREATERTHAN || tok == LESSTHAN || tok == GTEQ || tok == LESSEQ) {
		match(tok);
		shift_expression();
	}
	TRACEOUT("relational_expression");
}

static void
equality_expression(void)
{
	TRACEIN("equality_expression");
	relational_expression();
	while (tok == EQEQ || tok == NOTEQ) {
		match(tok);
		relational_expression();
	}
	TRACEOUT("equality_expression");
}

static void
and_expression(void)
{
	TRACEIN("and_expression");
	equality_expression();
	while (tok == AND) {
		match(AND);
		equality_expression();
	}
	TRACEOUT("and_expression");
}

static void
exclusive_or_expression(void)
{
	TRACEIN("exclusive_or_expression");
	and_expression();
	while (tok == XOR) {
		match(XOR);
		and_expression();
	}
	TRACEOUT("exclusive_or_expression");
}

static void
inclusive_or_expression(void)
{
	TRACEIN("inclusive_or_expression");
	exclusive_or_expression();
	while (tok == OR) {
		match(OR);
		exclusive_or_expression();
	}
	TRACEOUT("inclusive_or_expression");
}

static void
logical_and_expression(void)
{
	TRACEIN("logical_and_expression");
	inclusive_or_expression();
	while (tok == ANDAND) {
		match(ANDAND);
		inclusive_or_expression();
	}
	TRACEOUT("logical_and_expression");
}


static void
logical_or_expression(void)
{
	TRACEIN("logical_or_expression");
	logical_and_expression();
	while (tok == OROR) {
		match(OROR);
		logical_and_expression();
	}
	TRACEOUT("logical_or_expression");
}

static void
conditional_expression(void)
{
	TRACEIN("conditional_expression");
	logical_or_expression();
	if (tok == QUERY) {
		match(QUERY);
		expression();
		match(COLON);
		conditional_expression();
	}
	TRACEOUT("conditional_expression");
}

static void
assignment_expression(void)
{
	TRACEIN("assignment_expression");
	conditional_expression();
	if (is_assign_operator(tok)) {
		/* TODO: check that previous expression was unary */
		match(tok);
		assignment_expression();
	}
	TRACEOUT("assignment_expression");
}

static void
labeled_statement(void)
{
	TRACEIN("labeled_statement");
	match(IDENTIFIER);
	match(COLON);
	statement();
	TRACEOUT("labeled_statement");
}

static void
case_statement(void)
{
	TRACEIN("case_statement");
	match(CASE);
	constant_expression();
	match(COLON);
	statement();
	TRACEOUT("case_statement");
}

static void
default_statement(void)
{
	TRACEIN("default_statement");
	match(DEFAULT);
	match(COLON);
	statement();
	TRACEOUT("default_statement");
}

static void
if_statement(void)
{
	TRACEIN("if_statement");
	enter_scope();
	match(IF);
	match(LPAREN);
	expression();
	match(RPAREN);
	enter_scope();
	statement();
	exit_scope();
	if (tok == ELSE) {
		enter_scope();
		match(ELSE);
		statement();
		exit_scope();
	}
	exit_scope();
	TRACEOUT("if_statement");
}

static void
switch_statement(void)
{
	TRACEIN("switch_statement");
	enter_scope();
	match(SWITCH);
	match(LPAREN);
	expression();
	match(RPAREN);
	enter_scope();
	statement();
	exit_scope();
	exit_scope();
	TRACEOUT("switch_statement");
}

static void
while_statement(void)
{
	TRACEIN("while_statement");
	enter_scope();
	match(WHILE);
	match(LPAREN);
	expression();
	match(RPAREN);
	enter_scope();
	statement();
	exit_scope();
	exit_scope();
	TRACEOUT("while_statement");
}

static void
do_while_statement(void)
{
	TRACEIN("do_while_statement");
	enter_scope();
	match(DO);
	enter_scope();
	statement();
	exit_scope();
	match(WHILE);
	match(LPAREN);
	expression();
	match(RPAREN);
	exit_scope();
	match(SEMI);
	TRACEOUT("do_while_statement");
}

static void
for_statement(void)
{
	TRACEIN("for_statement");
	enter_scope();
	match(FOR);
	match(LPAREN);
	if (tok != SEMI) {
		if (is_declaration(tok)) {
			declaration();
		}
		else {	
			expression();
			match(SEMI);
		}
	}
	else {
		match(SEMI);
	}
	if (tok != SEMI)
		expression();
	match(SEMI);
	if (tok != RPAREN)
		expression();
	match(RPAREN);
	enter_scope();
	statement();
	exit_scope();
	exit_scope();
	TRACEOUT("for_statement");
}

static void
break_statement(void)
{
	TRACEIN("break_statement");
	match(BREAK);
	match(SEMI);
	TRACEOUT("break_statement");
}

static void
continue_statement(void)
{
	TRACEIN("continue_statement");
	match(CONTINUE);
	match(SEMI);
	TRACEOUT("continue_statement");
}

static void
goto_statement(void)
{
	TRACEIN("goto_statement");
	match(GOTO);
	match(IDENTIFIER);
	match(SEMI);
	TRACEOUT("goto_statement");
}

static void
return_statement(void)
{
	TRACEIN("return_statement");
	match(RETURN);
	if (tok != SEMI)
		expression();
	match(SEMI);
	TRACEOUT("return_statement");
}

static void
empty_statement(void)
{
	TRACEIN("empty_statement");
	match(SEMI);
	TRACEOUT("empty_statement");
}

static void
expression_statement(void)
{
	TRACEIN("expression_statement");

	if (tok == IDENTIFIER && lex_colon_follows()) {
		labeled_statement();
	}
	else {
		expression();
		match(SEMI);
	}
	TRACEOUT("expression_statement");
}

static void
statement(void)
{
	TRACEIN("statement");
	switch (tok) {
	case IDENTIFIER: expression_statement(); break;
	case CASE: case_statement(); break;
	case DEFAULT: default_statement(); break;
	case IF: if_statement(); break;
	case SWITCH: switch_statement(); break;
	case WHILE: while_statement(); break;
	case DO: do_while_statement(); break;
	case FOR: for_statement(); break;
	case BREAK: break_statement(); break;
	case CONTINUE: continue_statement(); break;
	case GOTO: goto_statement(); break;
	case RETURN: return_statement(); break;
	case LBRACE: compound_statement(); break;
	case SEMI: empty_statement(); break;
	default: 
		if (is_expression(tok))
			expression_statement(); 
		break;
	}
	TRACEOUT("statement");
}


static void
compound_statement(void)
{
	TRACEIN("compound_statement");
	enter_scope();
	match(LBRACE);

	while (tok != RBRACE) {
		if (is_declaration(tok)) {
			if (tok == IDENTIFIER && lex_colon_follows())
				statement();
			else
				declaration();
		}
		else {
			statement();
		}
	}
	exit_scope();
	match(RBRACE);
	TRACEOUT("compound_statement");
}

static void
enumerator(void)
{
	TRACEIN("enumerator");
	if (tok == IDENTIFIER) {
		check_not_typedef();
		install_symbol(Lexeme->identifier->id_name, 
			Storage_class[stack_ptr], OBJ_ENUMERATOR);
		match(IDENTIFIER);
	}
	else {
		TRACEOUT("enumerator");
		return;
	}
	if (tok == EQUALS) {
		match(EQUALS);
		constant_expression();
	}
	TRACEOUT("enumerator");
}

static void
enum_specifier(void)
{
	TRACEIN("enum_specifier");
	if (tok == ENUM) {
		match(ENUM);
	}
	else {
		TRACEOUT("enum_specifier");
		return;
	}
	if (tok == IDENTIFIER) {
		match(IDENTIFIER);
	}
	if (tok == LBRACE) {
		match(LBRACE);
		enumerator();
		while (tok == COMMA) {
			match(COMMA);
			enumerator();
		}
		match(RBRACE);
	}
	TRACEOUT("enum_specifier");
}

static void
member(void)
{
	TRACEIN("member");
	if (tok != COLON)
		declarator(0);
	if (tok == COLON) {
		match(COLON);
		constant_expression();
	}
	TRACEOUT("member");
}

static void
members(void)
{
	TRACEIN("members");
	do {
		stack_ptr++;
		declaration_specifiers(1);
		member();
		while (tok == COMMA) {
			match(COMMA);
			member();
		}
		match(SEMI);
		stack_ptr--;
	} while (tok != RBRACE);
	TRACEOUT("members");
}

static void
struct_or_union_specifier(void)
{
	TRACEIN("struct_or_union_specifier");
	Parsing_struct++;
	match(tok);
	if (tok == IDENTIFIER)
		match(IDENTIFIER);
	if (tok == LBRACE) {
		match(LBRACE);
		members();
		match(RBRACE);
	}
	Parsing_struct--;
	TRACEOUT("struct_or_union_specifier");
}

static void
type_name(void)
{
	TRACEIN("type_name");
	stack_ptr++;
	declaration_specifiers(1);
	declarator(1);
	stack_ptr--;
	TRACEOUT("type_name");
}

static void
declaration_specifiers(int no_storage_class)
{
	bool type_found = FALSE;
	TRACEIN("declaration_specifiers");
	assert(stack_ptr >= 0 && stack_ptr < 100);
	Storage_class[stack_ptr] = 0;
	while (is_declaration(tok)) {
		if (no_storage_class && (TokMap[tok] & TOK_STORAGE_CLASS)) {
			fprintf(stderr, "Parse failed: unexpected storage class %s\n", tokname(tok));
			exit(1);
		}
		if (tok == IDENTIFIER && type_found)
			break;
		if ((TokMap[tok] & TOK_TYPE_SPECIFIER) || tok == IDENTIFIER) {
			type_found = TRUE;
		}
		if (TokMap[tok] & TOK_STRUCT) {
			struct_or_union_specifier();
			break;
		}
		else if (tok == ENUM) {
			enum_specifier();
			break;
		}
		else {
			bool savedtok = 0;
			if (TokMap[tok] & TOK_STORAGE_CLASS) {
				Storage_class[stack_ptr] = tok;
			}
			else if (tok == IDENTIFIER) {
				savedtok = tok;
			}
			match(tok);
			if (savedtok == IDENTIFIER)
				break;
		}
	}
	TRACEOUT("declaration_specifiers");
}

static void
pointer(void)
{
	TRACEIN("pointer");
	while (tok == STAR) {
		match(STAR);
		while (TokMap[tok] & TOK_TYPE_QUALIFIER) {
			match(tok);
		}
	}
	TRACEOUT("pointer");
}

static void
direct_declarator(int abstract)
{
	TRACEIN("direct_declarator");
	if (tok == LPAREN) {
		match(LPAREN);
		declarator(abstract);
		match(RPAREN);
	}
	else {
		if (!abstract) {
			if (tok == IDENTIFIER) {
				Saw_ident = 1;
				if (Storage_class[stack_ptr] == TYPEDEF) {
					install_symbol(Lexeme->identifier->id_name, 
						TYPEDEF, OBJ_TYPEDEF_NAME);
				}
				else if (!Parsing_struct && !Parsing_oldstyle_parmdecl) {
					install_symbol(Lexeme->identifier->id_name, 
						Storage_class[stack_ptr], OBJ_IDENTIFIER);
				}
				match(IDENTIFIER);
			}
		}
	}
	TRACEOUT("direct_declarator");
}

static void 
parameter_list(int *new_style)
{
	TRACEIN("parameter_list");
	if (tok == IDENTIFIER && (Cursym == 0 || Cursym->object_type != OBJ_TYPEDEF_NAME)) {
		*new_style = 0;
		install_symbol(Lexeme->identifier->id_name, 
			AUTO, OBJ_PARAMETER);
		match(IDENTIFIER);
		while (tok == COMMA) {
			match(COMMA);
			if (tok == IDENTIFIER) {
				check_not_typedef();
				install_symbol(Lexeme->identifier->id_name, 
					AUTO, OBJ_PARAMETER);
				match(tok);
			}
			else
				match(IDENTIFIER);
		}
	}
	else {

		/*
		 * CHECK: When defining a function, each declarator in a
		 * parameter list must contain an identifier.
		 */

		*new_style = 1;
		stack_ptr++;
		declaration_specifiers(0);
		declarator(0);
		stack_ptr--;
		while (tok == COMMA) {
			match(COMMA);
			if (tok == ELLIPSIS) {
				match(ELLIPSIS);
				break;
			}
			stack_ptr++;
			declaration_specifiers(0);
			declarator(0);
			stack_ptr--;
		}
	}
	TRACEOUT("parameter_list");
}

static void
suffix_declarator(void)
{
	TRACEIN("suffix_declarator");
	if (tok == LBRAC) {
		match(LBRAC);
		constant_expression();
		match(RBRAC);
	}
	else if (tok == LPAREN) {
		int new_style = 0;
		enter_scope();
		match(LPAREN);
		parameter_list(&new_style);
		match(RPAREN);
		if (new_style && tok != LBRACE)
			exit_scope();
		Is_func = 1;
	}
	TRACEOUT("suffix_declarator");
}


static void
declarator(int abstract)
{
	TRACEIN("declarator");
	if (tok == STAR) {
		pointer();
	}
	direct_declarator(abstract);
	while (tok == LBRAC || tok == LPAREN) {
		suffix_declarator();
	}
	TRACEOUT("declarator");
}

static void
designator(void)
{
	TRACEIN("designator");
	if (tok == LBRAC) {
		match(LBRAC);
		constant_expression();
		match(RBRAC);
	}
	else if (tok == DOT) {
		match(DOT);
		if (tok == IDENTIFIER) {
			check_not_typedef();
			match(tok);
		}
	}
	TRACEOUT("designator");
}
	

static void
initializer(int recurse)
{
	TRACEIN("initializer");
	if (tok == LBRACE) {
		match(LBRACE);
		initializer(recurse+1);
		while (tok == COMMA) {
			match(COMMA);
			initializer(recurse+1);
		}
		match(RBRACE);
	}
	else if (recurse && (tok == LBRAC || tok == DOT)) {
		while (tok == LBRAC || tok == DOT) {
			designator();
		}
		match(EQUALS);
		initializer(0);
	}
	else {
		assignment_expression();
	}
	TRACEOUT("initializer");
}

static void
function_definition(void)
{
	TRACEIN("function_definition");

	if (tok == LBRACE) {
		compound_statement();
	}
	else {
		Parsing_oldstyle_parmdecl++;
		while (is_declaration(tok)) {
			/*
			* CHECK: The only storage class permitted is
			* register and initialization is not permitted.
			* if no declaration is given for a parameter,
			* its type is taken to be int.
			*/
	
		 	declaration();
	 	}
		Parsing_oldstyle_parmdecl--;
	 	compound_statement();
 	}
	exit_scope();
	TRACEOUT("function_definition");
}

static int
init_declarator(int check_if_function)
{
	int old_Is_func, old_Saw_ident;
	int func_defn = 0;
	TRACEIN("init_declarator");

	old_Saw_ident = Saw_ident;
	old_Is_func = Is_func;

	Saw_ident = 0;
	Is_func = 0;
	declarator(0);

	func_defn = check_if_function &&
		Level == LEVEL_FUNCTION &&
		Is_func && 
		Saw_ident &&
		is_function_body(tok);

	if (Is_func) {
		/*
		 * CHECK: The only storage class specifiers allowed among the
		 * declaration specifiers are extern or static.
		 */

		/*
		 * CHECK: A function may not return a function or an array.
		 */
	}

	Is_func = old_Is_func;
	Saw_ident = old_Saw_ident;
	if (func_defn) {
		function_definition();
		TRACEOUT("init_declarator");
		return 1;
	}
	else {
		if (tok == EQUALS) {
			/*
		 	* CHECK: not allowed when parsing old style function parameters
		 	* or a prototype.
		 	*/
			match(EQUALS);
			initializer(0);
		}
	}
	TRACEOUT("init_declarator");
	return 0;
}

/*
 * 3) a declaration must have at least one declarator, or its type specifier
 * must declare a structure tag, a union tag, or the members of an enumeration.
 * 4) empty declarations are not permitted.
 */
static void
declaration(void)
{
	TRACEIN("declaration");
	
	stack_ptr++;
	declaration_specifiers(0);
	if ( tok == SEMI ) {
		match(SEMI);
		goto success;
	}
	/* 2) the first declarator at global level may start a function
	 * definition.
	 */
	if (init_declarator(Level == LEVEL_GLOBAL) == 1) {
		goto success;
	}
	while ( tok == COMMA ) {
		match(COMMA);
		init_declarator(0);
	}
	match(SEMI);
success:
	stack_ptr--;
	TRACEOUT("declaration");
}

/*
 * 1) translation unit consists of a sequence of external declarations.
 * which are either declarations or function definitions.
 * 2) only at this level can functions be defined.
 */
static void
translation_unit(void)
{
	TRACEIN("translation_unit");
	Level = LEVEL_GLOBAL;
	tok = lex_get_token();
	while (tok != 0) {

		if (is_external_declaration(tok)) {
			/* 
			 * a function definition looks like a declaration,
			 * hence is initially parsed as one.
			 * the check for 2) is in init_declarator().
			 */
			declaration();
		}
		else if (tok == SEMI) {
			/*ERROR(4): empty declarations are not permitted */
			match(tok);
		}
		else {
			fprintf(stderr, "Parse failed: unexpected input %s\n",
				tokname(tok));
			exit(1);
		}
		assert(Level == LEVEL_GLOBAL);
	}
	TRACEOUT("translation_unit");
}

token_t
name_type(const char *name)
{
	Cursym = find_symbol(Cursymtab, name, 1);
	return IDENTIFIER;
}

static const char *
mygetline(char *arg)
{
	static char line[512];
	line[0] = 0;

	return fgets(line, sizeof line, (FILE *)arg);
}

void parser_main(int argc, char *argv[])
{
	lex_env_t mylex = {0};
        FILE *fp;
	const char *cp = getenv("DEBUG");

	if (cp != 0) {
		DebugLevel = atoi(cp);
	}

        if (argc != 2)
                exit(1);
        fp = fopen(argv[1], "r");
        if (fp == 0)
                exit(1);

 	Lex_env = &mylex;
	Lex_env->le_getline = mygetline;
	Lex_env->le_getline_arg = (char *)fp;
	Lexeme = &Lex_env->le_lexeme;
        init_tokmap();
	init_symbol_table();

#if 0
        putenv("LEX_DEBUG=1");
#endif
	translation_unit();

        return;
}

