%expect 0
%{
extern char *yytext;
int yylex();
void yyerror(const char *str);

void pattern_push(const char *str);
void pattern_alt();
void pattern_seq();
void pattern_opt();

%}

%token FRAGMENT
%token LPAREN RPAREN LSQRBR RSQRBR SLASH
%token ERROR

%%

pattern         : alternatives;

alternatives    : alternatives SLASH sequence { pattern_alt(); }
                | sequence;

sequence        : sequence basepattern { pattern_seq(); }
                | basepattern;

basepattern     : FRAGMENT { pattern_push(yytext); }
                | LSQRBR pattern RSQRBR { pattern_opt(); }
                | LPAREN pattern RPAREN;
