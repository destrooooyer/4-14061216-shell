%{
#include "bison.tab.h"
#include <stdio.h>
#include <string.h>
#include "global.h"
 #define YYSTYPE char*
%}

CHAR [^" "\t\n"<"">""<""|""&"]
NUM [0-9]
SYMBOL [">""<""|"]
SPACETAB [" "\t]

OP  {SPACETAB}*{CHAR}+{SPACETAB}*
  
STRING  ({OP}{SYMBOL}?{OP})+



%%

{STRING}  {yylval = strdup(yytext);return STRING;}

{SPACETAB} 
\n       return END;


%%
int yywrap(){
return 1;
}
