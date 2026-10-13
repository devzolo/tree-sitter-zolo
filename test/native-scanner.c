// Standalone scanner boundary gate; requires no generated parser.
#include <assert.h>
#include <stdio.h>
#include "../src/scanner.c"
typedef struct {TSLexer lexer;const char *text;size_t position,end;uint32_t initial_column;} TestLexer;
static void advance_test(TSLexer *base,bool skip){(void)skip;TestLexer *l=(TestLexer*)base;if(l->text[l->position])l->position++;base->lookahead=(unsigned char)l->text[l->position];}
static void mark_test(TSLexer *base){TestLexer*l=(TestLexer*)base;l->end=l->position;}
static bool eof_test(const TSLexer *base){return !base->lookahead;}
static uint32_t column_test(TSLexer *base){TestLexer*l=(TestLexer*)base;size_t start=l->position;while(start && l->text[start-1]!='\n')start--;return (uint32_t)(l->position-start)+(start==0?l->initial_column:0);}
static TestLexer input(const char*text){TestLexer l={0};l.text=text;l.lexer.lookahead=(unsigned char)text[0];l.lexer.advance=advance_test;l.lexer.mark_end=mark_test;l.lexer.eof=eof_test;l.lexer.get_column=column_test;return l;}
static void boundary(const char*import,const char*name,const char*body,unsigned token){Scanner s={0};bool valid[ERROR_SENTINEL + 1]={0};TestLexer l=input(import);valid[FOREIGN_IMPORT]=true;assert(tree_sitter_zolo_external_scanner_scan(&s,&l.lexer,valid));memset(valid,0,sizeof valid);valid[FOREIGN_PROVIDER]=true;l=input(name);assert(tree_sitter_zolo_external_scanner_scan(&s,&l.lexer,valid));memset(valid,0,sizeof valid);valid[FOREIGN_BODY]=true;l=input(body);assert(tree_sitter_zolo_external_scanner_scan(&s,&l.lexer,valid));if(l.lexer.result_symbol!=token||strcmp(body+l.end,"} print(42)")!=0){fprintf(stderr,"boundary token %u expected %u: %s\n",l.lexer.result_symbol,token,body+l.end);assert(0);}}
static void dependency_header(const char *text, bool expected) {
  Scanner scanner = {0};
  bool valid[ERROR_SENTINEL + 1] = {0};
  valid[DEPENDENCIES_KEYWORD] = true;
  TestLexer lexer = input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner, &lexer.lexer, valid) == expected);
  if (expected) {
    assert(lexer.lexer.result_symbol == DEPENDENCIES_KEYWORD);
    assert(lexer.end == 4); // Lookahead must leave the entire block to the grammar.
  }
}

static void with_boundary(const char *text, bool update, bool handle, bool recovery, bool expected, unsigned token) {
  Scanner scanner = {0};
  bool valid[ERROR_SENTINEL + 1] = {0};
  valid[RECORD_UPDATE_WITH] = update;
  valid[HANDLE_SEPARATOR] = handle;
  valid[ERROR_SENTINEL] = recovery;
  TestLexer lexer = input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner, &lexer.lexer, valid) == expected);
  if (expected) {
    assert(lexer.lexer.result_symbol == token);
    const char *word = strstr(text, "with");
    assert(word != NULL);
    assert(lexer.end == (size_t)(word - text) + 4);
  }
  assert(scanner.count == 0 && scanner.scope == 0);
}

static void callback_boundary(const char *text, bool expected, size_t marker_end) {
  Scanner scanner = {0};
  bool valid[ERROR_SENTINEL + 1] = {0};
  valid[NAMED_CALLBACK_START] = true;
  valid[RECORD_UPDATE_WITH] = true;
  valid[DEPENDENCIES_KEYWORD] = true; // Callback lookahead must not lose d-labels.
  valid[CONVENTION] = true; // sink stays an identifier before a callback body.
  TestLexer lexer = input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner, &lexer.lexer, valid) == expected);
  if (expected) {
    assert(lexer.lexer.result_symbol == NAMED_CALLBACK_START);
    assert(lexer.end == marker_end);
  }
  assert(scanner.count == 0 && scanner.scope == 0);
}

static void callback_finish(const char *text, unsigned token) {
  Scanner scanner = {0};
  bool valid[ERROR_SENTINEL + 1] = {0};
  valid[NAMED_CALLBACK_START] = true;
  valid[TRAILING_CALLBACK_PIPE] = true;
  valid[CALLBACK_END] = true;
  valid[FOREIGN_IMPORT] = true; // Marker must run before any import whitespace scan.
  TestLexer lexer = input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner, &lexer.lexer, valid));
  assert(lexer.lexer.result_symbol == token);
  assert(lexer.end == 0);
  assert(scanner.count == 0 && scanner.scope == 0);
}

static void fourth_prefix(const char *text, uint8_t flags, bool expected, unsigned token) {
  Scanner scanner = {0};
  scanner.expr_flags = flags;
  bool valid[ERROR_SENTINEL + 1] = {0};
  valid[PARTIAL_KEYWORD] = true;
  valid[CAPTURE_KEYWORD] = true;
  valid[PARALLEL_COLLECTION_KEYWORD] = true;
  valid[FAIL_KEYWORD] = true;
  valid[ITER_BUILDER_KEYWORD] = true;
  TestLexer lexer = input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner, &lexer.lexer, valid) == expected);
  if (expected) {
    assert(lexer.lexer.result_symbol == token);
    assert(lexer.end == 4); // Only the word belongs to this contextual token.
  }
  assert(scanner.expr_flags == flags && scanner.count == 0 && scanner.scope == 0);
}

typedef struct {TSLexer lexer;const int32_t *text;size_t position,end;} CodepointLexer;
static void advance_codepoint(TSLexer *base,bool skip){(void)skip;CodepointLexer*l=(CodepointLexer*)base;if(l->text[l->position])l->position++;base->lookahead=l->text[l->position];}
static void mark_codepoint(TSLexer *base){CodepointLexer*l=(CodepointLexer*)base;l->end=l->position;}
static uint32_t column_codepoint(TSLexer *base){CodepointLexer*l=(CodepointLexer*)base;size_t start=l->position;while(start && l->text[start-1]!='\n')start--;return (uint32_t)(l->position-start);}
static void fourth_prefix_codepoints(const int32_t *text,uint8_t flags,bool expected,unsigned token) {
  Scanner scanner={0};scanner.expr_flags=flags;
  bool valid[ERROR_SENTINEL+1]={0};
  valid[PARTIAL_KEYWORD]=true;valid[CAPTURE_KEYWORD]=true;valid[PARALLEL_COLLECTION_KEYWORD]=true;valid[FAIL_KEYWORD]=true;valid[ITER_BUILDER_KEYWORD]=true;
  CodepointLexer lexer={0};lexer.text=text;lexer.lexer.lookahead=text[0];lexer.lexer.advance=advance_codepoint;lexer.lexer.mark_end=mark_codepoint;lexer.lexer.eof=eof_test;lexer.lexer.get_column=column_codepoint;
  assert(tree_sitter_zolo_external_scanner_scan(&scanner,&lexer.lexer,valid)==expected);
  if(expected){assert(lexer.lexer.result_symbol==token);assert(lexer.end==4);}
  assert(scanner.expr_flags==flags&&scanner.count==0&&scanner.scope==0);
}

static void timeout_operand(const char *text, bool expected) {
  Scanner scanner = {0};
  scanner.expr_flags = 3;
  bool valid[ERROR_SENTINEL + 1] = {0};
  valid[TIMEOUT_OPERAND_START] = true;
  TestLexer lexer = input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner, &lexer.lexer, valid) == expected);
  if (expected) {
    assert(lexer.lexer.result_symbol == TIMEOUT_OPERAND_START);
    assert(lexer.end == 0); // The proof never owns the operand or its trivia.
  }
  assert(scanner.expr_flags == 3 && scanner.count == 0 && scanner.scope == 0);
}

static void view_pattern_start(const char *text,bool expected) {
  Scanner scanner={0};scanner.expr_flags=3;
  bool valid[ERROR_SENTINEL+1]={0};valid[VIEW_PATTERN_START]=true;
  TestLexer lexer=input(text);
  assert(tree_sitter_zolo_external_scanner_scan(&scanner,&lexer.lexer,valid)==expected);
  if(expected){assert(lexer.lexer.result_symbol==VIEW_PATTERN_START);assert(lexer.end==0);}
  assert(scanner.expr_flags==3&&scanner.count==0&&scanner.scope==0);
}

static void view_pattern_codepoints(const int32_t *text,bool expected) {
  Scanner scanner={0};scanner.expr_flags=3;
  bool valid[ERROR_SENTINEL+1]={0};valid[VIEW_PATTERN_START]=true;
  CodepointLexer lexer={0};lexer.text=text;lexer.lexer.lookahead=text[0];lexer.lexer.advance=advance_codepoint;lexer.lexer.mark_end=mark_codepoint;lexer.lexer.eof=eof_test;lexer.lexer.get_column=column_codepoint;
  assert(tree_sitter_zolo_external_scanner_scan(&scanner,&lexer.lexer,valid)==expected);
  if(expected){assert(lexer.lexer.result_symbol==VIEW_PATTERN_START);assert(lexer.end==0);}
  assert(scanner.expr_flags==3&&scanner.count==0&&scanner.scope==0);
}

static void block_triple(const char *text,uint32_t column,bool raw,unsigned hashes,unsigned token,size_t end) {
  Scanner scanner={0};scanner.expr_flags=3;
  bool valid[ERROR_SENTINEL+1]={0};
  valid[raw?RAW_BLOCK_TRIPLE_CONTENT0+hashes:BLOCK_TRIPLE_CONTENT]=true;
  valid[BLOCK_TRIPLE_END0+hashes]=true;
  TestLexer lexer=input(text);lexer.initial_column=column;
  const bool expected=token!=ERROR_SENTINEL;
  assert(tree_sitter_zolo_external_scanner_scan(&scanner,&lexer.lexer,valid)==expected);
  if(expected){assert(lexer.lexer.result_symbol==token);assert(lexer.end==end);}
  assert(scanner.expr_flags==3&&scanner.count==0&&scanner.scope==0);
}

int main(void){
  block_triple("text \"\"\" )\n  \"\"\" after",0,false,0,BLOCK_TRIPLE_CONTENT,strlen("text \"\"\" )\n"));
  block_triple("  \"\"\" after",0,false,0,BLOCK_TRIPLE_END0,5);
  block_triple("\"\"\" inline",5,false,0,BLOCK_TRIPLE_CONTENT,strlen("\"\"\" inline"));
  block_triple("\r\"\"\" )\n\"\"\"",0,false,0,BLOCK_TRIPLE_CONTENT,strlen("\r\"\"\" )\n"));
  block_triple("body\r\n\t\"\"\" after",0,false,0,BLOCK_TRIPLE_CONTENT,strlen("body\r\n"));
  block_triple("{value}",4,false,0,ERROR_SENTINEL,0);
  block_triple("{{value}}",4,false,0,ERROR_SENTINEL,0);
  block_triple("\\n",4,false,0,ERROR_SENTINEL,0);
  block_triple("}}",4,false,0,ERROR_SENTINEL,0);
  block_triple("a}b{value}",4,false,0,BLOCK_TRIPLE_CONTENT,strlen("a}b"));
  block_triple("a}}b",4,false,0,BLOCK_TRIPLE_CONTENT,1);
  block_triple("  {value}",0,false,0,BLOCK_TRIPLE_CONTENT,2);
  for(unsigned n=0;n<4;n++) {
    char hashes[4]={0},text[160];for(unsigned i=0;i<n;i++)hashes[i]='#';
    snprintf(text,sizeof(text),"body \"\"\"%s )\n  \"\"\"%s after",hashes,hashes);
    const size_t body_end=strlen("body \"\"\" )\n")+n;
    block_triple(text,0,true,n,RAW_BLOCK_TRIPLE_CONTENT0+n,body_end);
    snprintf(text,sizeof(text),"  \"\"\"%s after",hashes);
    block_triple(text,0,true,n,BLOCK_TRIPLE_END0+n,5+n);
    snprintf(text,sizeof(text),"\"\"\"%s inline",hashes);
    block_triple(text,3,true,n,RAW_BLOCK_TRIPLE_CONTENT0+n,strlen(text));
    if(n) {
      snprintf(text,sizeof(text),"\"\"\"\r%s )\n\"\"\"%s after",hashes,hashes);
      block_triple(text,0,true,n,RAW_BLOCK_TRIPLE_CONTENT0+n,strlen("\"\"\"\r )\n")+n);
    }
  }
  view_pattern_codepoints((const int32_t[]){0x00a0,'f',0x00a0,'-','>','x',')',0},true);
  view_pattern_codepoints((const int32_t[]){0x00a0,'a',',',0x00a0,'b',')',0},false);
  view_pattern_start("read_quantity -> .Valid(quantity))",true);
  view_pattern_start("factory() -> (head,...tail))",true);
  view_pattern_start("app::factory -> result)",true);
  view_pattern_start("(factory()) -> result)",true);
  view_pattern_start("factory(\"->\",[1],{ work() }) -> result)",true);
  view_pattern_start("factory(\"{nested(\"->\")}\") -> result)",true);
  view_pattern_start("factory(')') -> result)",true);
  view_pattern_start("factory / divisor -> result)",true);
  view_pattern_start("factory /* -> /* nested */ */ -> result)",true);
  view_pattern_start("factory // -> ignored\n -> result)",true);
  view_pattern_start("factory(```text\n -> ) inline ``` text\n```\n) -> result)",true);
  view_pattern_start("(a,b),c)",false);
  view_pattern_start("Factory(head),value)",false);
  view_pattern_start("factory(\"->\"))",false);
  view_pattern_start("factory(')'))",false);
  view_pattern_start("factory /* -> */)",false);
  view_pattern_start("factory // -> ignored\n)",false);
  view_pattern_start("factory([fn(x) -> x]))",false);
  view_pattern_start("factory({ fn(x) -> x }))",false);
  view_pattern_start("factory(```text\n -> ) inline ``` text\n```\n))",false);
  view_pattern_start("factory()) -> after_outer_close",false);
  view_pattern_start("factory(\"unfinished) -> result)",false);
  view_pattern_start("factory /* unfinished -> result)",false);
  view_pattern_start("",false);
  view_pattern_start("factory(r\"{ -> )\") -> result)",true);
  view_pattern_start("factory(r##\" ) \" -> still raw \"##) -> result)",true);
  view_pattern_start("factory(sql##\"{ ) \" -> still raw \"##) -> result)",true);
  view_pattern_start("factory(json\"{\\\"text\\\":\\\"->)\\\"}\") -> result)",true);
  view_pattern_start("factory(b\"{ -> )\") -> result)",true);
  view_pattern_start("factory(re\"{ -> )\") -> result)",true);
  view_pattern_start("factory(pat\"{text:str}\") -> result)",true);
  view_pattern_start("factory(\"\"\"a \" -> )\n text\"\"\") -> result)",true);
  view_pattern_start("factory(r##\"\"\"\n ) \" -> raw\n\"\"\"##) -> result)",true);
  view_pattern_start("factory(tw\"\"\"\n \" -> ) text\n\"\"\") -> result)",true);
  view_pattern_start("factory(r\"{ -> )\"))",false);
  view_pattern_start("factory(r##\" ) \" -> still raw \"##))",false);
  view_pattern_start("factory(sql##\"{ ) \" -> still raw \"##))",false);
  view_pattern_start("factory(\"\"\"a \" -> )\n text\"\"\"))",false);
  view_pattern_start("factory(\"{{ \")\" }}\") -> out)",true);
  view_pattern_start("factory(text\"{{ \")\" }}\") -> out)",true);
  view_pattern_start("factory(\"{{ \")\" }}\"))",false);
  view_pattern_start("factory(text\"{{ \")\" }}\"))",false);
  view_pattern_start("factory(\"\"\"\n    text \"\"\" )\n    body\n    \"\"\") -> out)",true);
  view_pattern_start("factory(tw\"\"\"\n    text \"\"\" )\n    body\n    \"\"\") -> out)",true);
  view_pattern_start("factory(r##\"\"\"\n    text \"\"\"## )\n    body\n    \"\"\"##) -> out)",true);
  view_pattern_start("factory(\"\"\"\n    text \"\"\" )\n    body\n    \"\"\"))",false);
  view_pattern_start("factory(tw\"\"\"\n    text \"\"\" )\n    body\n    \"\"\"))",false);
  view_pattern_start("factory(r##\"\"\"\n    text \"\"\"## )\n    body\n    \"\"\"##))",false);
  view_pattern_start("factory(\"\"\"{{ ) text\"\"\") -> out)",true);
  view_pattern_start("factory(\"\"\"{{ ) text\"\"\"))",false);
  view_pattern_start("factory(r#\"\"#) -> out)",true);
  view_pattern_start("factory(r#\"\"\"#) -> out)",true);
  view_pattern_start("factory(r#\"\"#))",false);
  view_pattern_start("factory(r#\"\"\"#))",false);
  view_pattern_start("factory(\"\"\"\r text \"\"\") -> out)",true);
  view_pattern_start("factory(tw\"\"\"\r text \"\"\") -> out)",true);
  view_pattern_start("factory(r#\"\"\"\r raw \"\"\"#) -> out)",true);
  view_pattern_start("factory(r#\"\"\"\r\n  text \"\"\"# )\r\n  \"\"\"#) -> out)",true);
  view_pattern_start("factory(\"\"\"\r text \"\"\"))",false);
  view_pattern_start("factory(tw\"\"\"\r text \"\"\"))",false);
  view_pattern_start("factory(r#\"\"\"\r raw \"\"\"#))",false);
  view_pattern_start("factory(r#\"\"\"\r\n  text \"\"\"# )\r\n  \"\"\"#))",false);
  view_pattern_start("factory(```text\ntext\r``` )\nbody\n```\n) -> out)",true);
  view_pattern_start("factory(```text\r\ntext\r``` )\r\nbody\r\n```\r\n) -> out)",true);
  view_pattern_start("factory(```text\ntext\r``` )\nbody\n```\n))",false);
  view_pattern_start("factory(```text\r\ntext\r``` )\r\nbody\r\n```\r\n))",false);
  view_pattern_start("factory(r#\"\"\"\r# ) \"#) -> out)",true);
  view_pattern_start("factory(r##\"\"\"\r## ) \"##) -> out)",true);
  view_pattern_start("factory(r#\"\"\"\r# ) \"#))",false);
  view_pattern_start("factory(r##\"\"\"\r## ) \"##))",false);
  view_pattern_start("factory(\"\"\"\n\r\"\"\" )\n\"\"\") -> out)",true);
  view_pattern_start("factory(\"\"\"\n\r\"\"\" )\n\"\"\"))",false);
  fourth_prefix_codepoints((const int32_t[]){'f','a','i','l',0x00a0,'-','1',0},0,true,FAIL_KEYWORD);
  fourth_prefix_codepoints((const int32_t[]){'f','a','i','l',0x00a0,'[','1',']',0},0,true,FAIL_KEYWORD);
  fourth_prefix_codepoints((const int32_t[]){'f','a','i','l',0x00a0,')',0},0,false,0);
  fourth_prefix_codepoints((const int32_t[]){'f','a','i','l',0x00a0,'{','1','}',0},1,false,0);
  fourth_prefix_codepoints((const int32_t[]){'i','t','e','r',0x00a0,'[','f','o','r',' ','x',' ','i','n',' ','x','s',']',0},0,true,ITER_BUILDER_KEYWORD);
  fourth_prefix_codepoints((const int32_t[]){'i','t','e','r','[',0x00a0,'i','f',' ','x',' ','=','>',' ','x',']',0},0,true,ITER_BUILDER_KEYWORD);
  fourth_prefix_codepoints((const int32_t[]){'i','t','e','r',0x00a0,'[','0',']',0},0,false,0);
  fourth_prefix_codepoints((const int32_t[]){'i','t','e','r',0x00a0,'(',')',0},0,false,0);
  timeout_operand(" 2ms { work() }", true);
  timeout_operand(" .Duration(2) { work() }", true);
  timeout_operand(" .5s { work() }", true);
  timeout_operand(" duration() { work() }", true);
  timeout_operand(" /* outer /* inner */ */ .Duration(2) { work() }", true);
  timeout_operand(" // duration follows\n 2ms { work() }", true);
  timeout_operand(":retry loop { break :retry 2ms } { work() }", true);
  timeout_operand(":retry /* loop */ for item in values {} { work() }", true);
  timeout_operand(":retry while ready {} { work() }", true);
  timeout_operand(":retry loop_more", false);
  timeout_operand(": 5", false);
  timeout_operand(":value", false);
  timeout_operand(" /* label */ :value", false);
  timeout_operand(" /* unfinished", false);
  timeout_operand("", false);
  fourth_prefix("fail -1", 0, true, FAIL_KEYWORD);
  fourth_prefix("fail [1, 2]", 0, true, FAIL_KEYWORD);
  fourth_prefix("fail (1, \"error\")", 0, true, FAIL_KEYWORD);
  fourth_prefix("fail #{code: 1}", 0, true, FAIL_KEYWORD);
  fourth_prefix("fail /* payload */ -1", 0, true, FAIL_KEYWORD);
  fourth_prefix("fail(value)", 0, false, 0);
  fourth_prefix("fail[0]", 0, false, 0);
  fourth_prefix("fail-1", 0, false, 0);
  fourth_prefix("fail.value", 0, false, 0);
  fourth_prefix("fail { 1 }", 1, false, 0); // An if/match scrutinee named fail.
  fourth_prefix("fail_more value", 0, false, 0);
  fourth_prefix("iter[for value in values => value]", 0, true, ITER_BUILDER_KEYWORD);
  fourth_prefix("iter[if ready => value]", 0, true, ITER_BUILDER_KEYWORD);
  fourth_prefix("iter /* source */ [for value in values => value]", 0, true, ITER_BUILDER_KEYWORD);
  fourth_prefix("iter[0]", 0, false, 0);
  fourth_prefix("iter(value)", 0, false, 0);
  fourth_prefix("iter[format(value)]", 0, false, 0);
  callback_finish(" content { 1 }", NAMED_CALLBACK_START);
  callback_finish(" /* note */ done { 1 }", NAMED_CALLBACK_START);
  callback_finish(" { |value| value }", TRAILING_CALLBACK_PIPE);
  callback_finish(" {/* note */ || value }", TRAILING_CALLBACK_PIPE);
  callback_finish(" { work() }", CALLBACK_END);
  callback_finish("\n Point { x: 1 }", CALLBACK_END);
  callback_finish(" /* newline\n */ Point { x: 1 }", CALLBACK_END);
  callback_finish(" |> next()", CALLBACK_END);
  callback_finish(" with { value: 1 }", CALLBACK_END);
  callback_finish("", CALLBACK_END);
  callback_boundary(" content { 1 }", true, 0);
  callback_boundary("done { 1 }", true, 0);
  callback_boundary("sink { 1 }", true, 0);
  callback_boundary("when { 1 }", true, 0);
  callback_boundary(" /* note */ content { 1 }", true, 0);
  callback_boundary(" /* outer /* nested */ */ content { 1 }", true, 0);
  callback_boundary("content\n { 1 }", true, 0); // Only the label must share the closer line.
  callback_boundary("\n content { 1 }", false, 0);
  callback_boundary(" /* newline\n */ content { 1 }", false, 0);
  callback_boundary(" // line comment\n content { 1 }", false, 0);
  callback_boundary(" read()", false, 0);
  callback_boundary(" }", false, 0);
  callback_boundary(" |> next()", false, 0);
  callback_boundary(" scope { 1 }", false, 0);
  callback_boundary(" with chosen { 1 }", false, 0);
  with_boundary("with { city: 1 }", true, false, false, true, RECORD_UPDATE_WITH);
  with_boundary("\r\n  with /* nested /* block */ */ { city: 1 }", true, false, false, true, RECORD_UPDATE_WITH);
  with_boundary("with // brace in trivia }\n { city: 1 }", true, false, false, true, RECORD_UPDATE_WITH);
  with_boundary("with chosen { body }", true, false, false, false, 0);
  with_boundary("with_more { city: 1 }", true, false, false, false, 0);
  with_boundary("with /* incomplete", true, false, false, false, 0);
  with_boundary("with chosen", false, true, false, true, HANDLE_SEPARATOR);
  with_boundary("with { Eff::op() => 1 }", true, true, false, true, HANDLE_SEPARATOR);
  with_boundary("with_more chosen", false, true, false, false, 0);
  with_boundary("with { city: 1 }", true, true, true, false, 0);
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
  dependency_header("deps { rust::serde_json = \"1.0.145\" }", true);
  dependency_header("deps /* header */ {\r\n // }\r\n alias /* comment */ :: \"@scope/pkg\" = \"1\" }", true);
  dependency_header("deps { deps::serde_json = \"1\" }", true);
  dependency_header("deps {}", false);
  dependency_header("deps { field: 1 }", false);
  dependency_header("deps {\n // }\n field: 1 }", false);
  dependency_header("deps()", false);
  dependency_header("deps_more { rust::serde_json = \"1\" }", false);
  dependency_header("deps { rust { serde_json = \"1\" } }", true);
  dependency_header("deps { /* unfinished", false);
  dependency_header("deps { ; , /* first */ rust::serde_json = \"1.0.145\"; }", true);
  dependency_header("deps { rust {} }", true);
  dependency_header("deps { ; , /* first */ rs /* group */ { } cpp::fmt = \"11\" }", true);
  dependency_header("deps /* header */ {\r\n // first group }\r\n kt\r\n { \"g:a\" = \"1\" } }", true);
  dependency_header("deps { deps {} }", true);
  dependency_header("deps { field: rust {} }", false);
  dependency_header("deps {\n field: rust { x: 1 } }", false);
  dependency_header("deps { rust // unfinished group header", false);
  dependency_header("deps_more { rust {} }", false);
  dependency_header("deps {\n field\n}", false);
  dependency_header("deps {\n field // ordinary field }\n}", false);
  puts("10 scanner dialect boundaries, 21 dependency headers, 10 with, 15 callback, 10 call-end, 17 fourth-prefix, 15 timeout-operand, 8 Unicode-prefix, 74 View and 27 BLOCK triple boundaries passed");return 0;
}
