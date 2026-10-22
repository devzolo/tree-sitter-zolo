// Direct lexical-boundary regressions; the corpus additionally exercises the
// same selectors through the compiled parser and checks their public children.
#include <assert.h>
#include <stdio.h>
#include "../../src/scanner.c"

typedef struct { TSLexer lexer; const unsigned char *input; size_t position,mark,token_start; unsigned column; } Probe;
static int32_t scalar_at(const unsigned char *text) {
  if(text[0]<128)return text[0];
  if((text[0]&0xe0)==0xc0)return ((text[0]&31)<<6)|(text[1]&63);
  if((text[0]&0xf0)==0xe0)return ((text[0]&15)<<12)|((text[1]&63)<<6)|(text[2]&63);
  return ((text[0]&7)<<18)|((text[1]&63)<<12)|((text[2]&63)<<6)|(text[3]&63);
}
static void advance(TSLexer *lexer,bool skip) {
  Probe *p=(Probe *)lexer;const unsigned char c=p->input[p->position];
  p->position+=c<128?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;
  if(skip)p->token_start=p->position;
  p->column=c=='\n'?0:p->column+1;lexer->lookahead=scalar_at(p->input+p->position);
}
static void mark(TSLexer *lexer){Probe *p=(Probe *)lexer;p->mark=p->position;}
static uint32_t column(TSLexer *lexer){return ((Probe *)lexer)->column;}
static bool eof(const TSLexer *lexer){return !lexer->lookahead;}
static bool included(const TSLexer *lexer){(void)lexer;return false;}
static Probe make_probe(const char *text) {
  Probe p={0};p.input=(const unsigned char *)text;p.lexer.lookahead=scalar_at(p.input);
  p.lexer.advance=advance;p.lexer.mark_end=mark;p.lexer.get_column=column;
  p.lexer.eof=eof;p.lexer.is_at_included_range_start=included;p.lexer.result_symbol=ERROR_SENTINEL;return p;
}
static void check(const char *text,bool accepted) {
  Scanner scanner={0};scanner.expr_flags=9;
  bool valid[ERROR_SENTINEL+1]={0};valid[SIXTH_LITERAL_TYPE_START]=true;
  Probe p=make_probe(text);
  const bool actual=tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid);
  if(actual!=accepted)fprintf(stderr,"scalar guard: %s; expected %d, got %d\n",text,(int)accepted,(int)actual);
  assert(actual==accepted);assert(p.mark==p.token_start);assert(scanner.expr_flags==9);
  if(actual)assert(p.lexer.result_symbol==SIXTH_LITERAL_TYPE_START);
}
int main(void) {
  const char *positive[]={
    "\"GET\"", "\"\"", "\"\\{literal\\}\"", "\"\\u{7b}\\u{7d}\"", "b\"\\x7B\"",
    "\"\\\\\\{literal\"", "\"\"\"{{literal}}\"\"\"", "\"\"\"\n  {{literal}}\n  \"\"\"",
    "r\"{raw}\\n\"", "r#\"quoted \" {raw}\"#", "r###\"\"\"quoted\"\"\"###",
    "r\"\"\"\n  {raw}\\n\n  \"\"\"", "r#\"\"\"\r\n  {raw}\r\n  \"\"\"#",
    "b\"bytes\"", "```\n{verbatim}\n```", "```\r\n  `\r\n  ``\r\n  text ``` inside\r\n  ```",
    "42", "- /* note */ 42", "0xFF", "0b1010", "0o77", "1_000", "1.25", "1e+3", "-0.0",
    "true", "false", "true\"next token\"", " /* kept */ \"GET\"", "1.{field}", "42..100"
  };
  const char *negative[]={
    "\"{value}\"", "\"\"\"{value}\"\"\"", "\"\"\"\n  {value}\n  \"\"\"", "\"unterminated",
    "\"\\u{110000}\"", "\"\\u{d800}\"", "\"\\u{}\"", "\"\\xQ1\"", "\"\\q\"", "\"\\x7B\"", "\"\\a\"", "\"\\1\"",
    "b\"{value}\"", "b\"\\{\"", "b\"\\u{7b}\"", "b\"\\a\"",
    "```sql\nSELECT 1\n```", "```inline```", "```\nunterminated", "r#\"unterminated\"",
    "42u8", "42f32", "1.25f64", "500ms", "1m", "1\xc2\xb5s", "3widgets", "1n", "1d", "1bd",
    "0x", "0b2", "1e+", "trueName", "nil", "`true`", "json\"{x}\"", "-true"
  };
  unsigned count=0;
  for(unsigned i=0;i<sizeof(positive)/sizeof(positive[0]);i++){check(positive[i],true);count++;}
  for(unsigned i=0;i<sizeof(negative)/sizeof(negative[0]);i++){check(negative[i],false);count++;}
  // A generic integer retains the previous const node; positive floats and
  // strings enter the new type branch. The old const expression stays intact.
  const struct { const char *text; enum TokenType selected; } generic[]={
    {"42",SIXTH_CONST_ARGUMENT_START},{"-42",SIXTH_CONST_ARGUMENT_START},
    {"0xFF",SIXTH_CONST_ARGUMENT_START},{"42u8",SIXTH_CONST_ARGUMENT_START},
    {"1.25",SIXTH_LITERAL_TYPE_START},{"\"GET\"",SIXTH_LITERAL_TYPE_START}
  };
  for(unsigned i=0;i<sizeof(generic)/sizeof(generic[0]);i++) {
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
    valid[SIXTH_LITERAL_TYPE_START]=valid[SIXTH_CONST_ARGUMENT_START]=true;
    Probe p=make_probe(generic[i].text);
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==generic[i].selected && p.mark==0);count++;
  }
  const struct {const char *text;bool triple;size_t end;} raw[]={
    {"r\"\"",false,3},{"r\"\"\"inline",false,3},
    {"r###\"\"\"quoted\"\"\"###",false,0},
    {"r\"\\\"",false,0},
    {"r\"\"\"\n  {raw}\n  \"\"\"",true,0},
    {"r#\"\"\"\n  text \"\"\" inside\n  \"\"\"#",true,0},
    {"r#\"\"\"\r# ) \"#",false,0}
  };
  for(unsigned i=0;i<sizeof(raw)/sizeof(raw[0]);i++) {
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
    valid[SIXTH_RAW_STRING_LITERAL]=valid[SIXTH_RAW_TRIPLE_STRING_LITERAL]=true;
    Probe p=make_probe(raw[i].text);
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==(raw[i].triple?SIXTH_RAW_TRIPLE_STRING_LITERAL:SIXTH_RAW_STRING_LITERAL));
    assert(p.mark==(raw[i].end?raw[i].end:strlen(raw[i].text)));count++;
  }
  const unsigned hashes[]={0,3,4,255,256};
  for(unsigned i=0;i<sizeof(hashes)/sizeof(hashes[0]);i++)for(unsigned triple=0;triple<2;triple++) {
    char text[1024];size_t n=0;text[n++]='r';
    for(unsigned j=0;j<hashes[i];j++)text[n++]='#';
    text[n++]='"';if(triple){text[n++]='"';text[n++]='"';text[n++]='\n';}
    memcpy(text+n,"{raw}\\n",7);n+=7;
    if(triple)text[n++]='\n';
    text[n++]='"';if(triple){text[n++]='"';text[n++]='"';}
    const unsigned close=hashes[i]>255?255:hashes[i];
    for(unsigned j=0;j<close;j++)text[n++]='#';text[n]=0;
    check(text,true);count++;
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
    valid[SIXTH_RAW_STRING_LITERAL]=valid[SIXTH_RAW_TRIPLE_STRING_LITERAL]=true;
    Probe p=make_probe(text);
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==(triple?SIXTH_RAW_TRIPLE_STRING_LITERAL:SIXTH_RAW_STRING_LITERAL));
    assert(p.mark==n);count++;
  }
  {
    const struct {const char *text;enum TokenType valid;enum TokenType selected;} combined[]={
      {"<card />",MARKUP_BOUNDARY,MARKUP_BOUNDARY},
      {"partial send(1)",PARTIAL_KEYWORD,PARTIAL_KEYWORD},
      {"recover {}",RECOVER_KEYWORD,RECOVER_KEYWORD},
      {"relative 0.1",APPROX_MODE_START,APPROX_MODE_START},
      {"r\"raw\"",RECOVER_KEYWORD,SIXTH_RAW_STRING_LITERAL}
    };
    for(unsigned i=0;i<sizeof(combined)/sizeof(combined[0]);i++) {
      Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
      valid[SIXTH_RAW_STRING_LITERAL]=valid[SIXTH_RAW_TRIPLE_STRING_LITERAL]=true;
      valid[combined[i].valid]=true;Probe p=make_probe(combined[i].text);
      assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
      assert(p.lexer.result_symbol==combined[i].selected);count++;
    }
  }
  {
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
    valid[SIXTH_DEFER_OUTCOME]=valid[IF_ELSE_KEYWORD]=true;
    Probe p=make_probe("else {}");
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==IF_ELSE_KEYWORD);count++;
  }
  {
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
    valid[SIXTH_LITERAL_TYPE_START]=valid[BLOCK_COMMENT]=true;
    Probe p=make_probe(" /* owner */ .Running");
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==BLOCK_COMMENT && p.token_start==1 && p.mark==12);count++;
  }
  {
    const struct {const char *text;enum TokenType selected;} headers[]={
      {"<signal> count",SIXTH_STORAGE_START},{":outer 42",SIXTH_LOOP_LABEL_START},
      {"/* label */ :outer 42",SIXTH_LOOP_LABEL_START},
      {"// label\n:outer 42",SIXTH_LOOP_LABEL_START}
    };
    for(unsigned i=0;i<sizeof(headers)/sizeof(headers[0]);i++) {
      Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
      valid[headers[i].selected]=valid[SIXTH_EXIT_VALUE]=true;
      Probe p=make_probe(headers[i].text);
      assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
      assert(p.lexer.result_symbol==headers[i].selected && p.mark==0);count++;
    }
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1]={0};
    valid[SIXTH_STORAGE_START]=valid[SIXTH_ASSERT_KEYWORD]=true;
    Probe p=make_probe("assert let .Ok(value) = input");
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==SIXTH_ASSERT_KEYWORD);count++;
    memset(valid,0,sizeof(valid));
    valid[SIXTH_LOOP_LABEL_START]=valid[SIXTH_EXIT_EMPTY]=valid[SIXTH_EXIT_VALUE]=true;
    p=make_probe("\n  print(1)");
    assert(tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));
    assert(p.lexer.result_symbol==SIXTH_EXIT_EMPTY && p.mark==p.token_start);count++;
  }
  {
    Scanner scanner={0};bool valid[ERROR_SENTINEL+1];memset(valid,1,sizeof(valid));
    Probe p=make_probe("r\"\"\"\n  {raw}\n  \"\"\"");
    assert(!tree_sitter_zolo_external_scanner_scan(&scanner,&p.lexer,valid));count++;
  }
  printf("%u scalar type scanner regressions passed.\n",count);return 0;
}
