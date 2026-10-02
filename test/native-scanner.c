// Standalone scanner boundary gate; requires no generated parser.
#include <assert.h>
#include <stdio.h>
#include "../src/scanner.c"
typedef struct {TSLexer lexer;const char *text;size_t position,end;} TestLexer;
static void advance_test(TSLexer *base,bool skip){(void)skip;TestLexer *l=(TestLexer*)base;if(l->text[l->position])l->position++;base->lookahead=(unsigned char)l->text[l->position];}
static void mark_test(TSLexer *base){TestLexer*l=(TestLexer*)base;l->end=l->position;}
static bool eof_test(const TSLexer *base){return !base->lookahead;}
static TestLexer input(const char*text){TestLexer l={0};l.text=text;l.lexer.lookahead=(unsigned char)text[0];l.lexer.advance=advance_test;l.lexer.mark_end=mark_test;l.lexer.eof=eof_test;return l;}
static void boundary(const char*import,const char*name,const char*body,unsigned token){Scanner s={0};bool valid[32]={0};TestLexer l=input(import);valid[FOREIGN_IMPORT]=true;assert(tree_sitter_zolo_external_scanner_scan(&s,&l.lexer,valid));memset(valid,0,sizeof valid);valid[FOREIGN_PROVIDER]=true;l=input(name);assert(tree_sitter_zolo_external_scanner_scan(&s,&l.lexer,valid));memset(valid,0,sizeof valid);valid[FOREIGN_BODY]=true;l=input(body);assert(tree_sitter_zolo_external_scanner_scan(&s,&l.lexer,valid));if(l.lexer.result_symbol!=token||strcmp(body+l.end,"} print(42)")!=0){fprintf(stderr,"boundary token %u expected %u: %s\n",l.lexer.result_symbol,token,body+l.end);assert(0);}}
int main(void){
  boundary("use plugin jvm::{kotlin as kt}","kt"," val s = \"${run { \"}\" }}\"; /* { /* } */ } */ return 42; } print(42)",15);
  boundary("use plugin jvm::{kotlin}","kotlin"," val s = $$\"\"\" } ${literal} $${run { \"}\" }} \"\"\"; } print(42)",15);
  boundary("use plugin native::{c as small}","small","\n#define CLOSE } \\\n { }\n return 42; } print(42)",16);
  boundary("use plugin native::{cpp as cc}","cc"," auto s=u8R\"close( } \" )near )close\"; return 42; } print(42)",17);
  boundary("use plugin native::{cpp}","cpp"," // ignored \\\n } {\n return 42; } print(42)",17);
  boundary("use plugin rust::{rust as rs}","rs"," let s=br###\" } \\\" \"## still } \"###; /* { /* } */ } */ 'outer: loop { break 'outer; } } print(42)",18);
  boundary("use plugin rust::rust", "rust"," let x: &'a str = \"}\"; let y='}'; let z='x'; } print(42)",18);
  boundary("use plugin go::{go as golang}","golang"," s := `${ } \\`; x := '}'; return 42 } print(42)",19);
  boundary("use plugin node::{javascript}","javascript"," const r=/}/; const t=`${{x:'}'}.x}`; } print(42)",12);
  boundary("use plugin jvm::{java}","java"," String s=\"}\"; // }\n return 42; } print(42)",14);
  puts("10 scanner dialect boundaries passed");return 0;
}
