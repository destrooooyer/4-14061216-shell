//��ֻ�Ƿ�����������Ҫ�����һ��������C����
//����Σ����������ղ�����c�ļ���
%{
    #include "global.h"

   // int yylex ();
    void yyerror ();
    #define YYSTYPE char*
    int offset, len, commandDone;
%}
//�������
%token STRING 
%token END

//������
%%

line            :   /* empty */
                    |command END                   {  inputBuff = yylval; execute();  commandDone = 1;  return 0; }
;
//�������� & �����̨����
command         :   fgCommand
                    |fgCommand '&'
;

fgCommand       :   simpleCmd
;
// ...<.
// ....  
// ..>.
// ..<.>.    
simpleCmd       :   progInvocation inputRedirect outputRedirect
;

progInvocation  :   STRING args
;

inputRedirect   :   /* empty */
                    |'<' STRING
;

outputRedirect  :   /* empty */
                    |'>' STRING
;

args            :   /* empty */
                    |args STRING
;

%%


/****************************************************************
                  ������Ϣִ�к���
****************************************************************/
//OR
void yyerror()
{
    printf(" is a wrong input\n");
}

/****************************************************************
                  main������
****************************************************************/
int main(int argc, char** argv) {
    int i;
    char c;

    init(); //��ʼ������
    commandDone = 0;
    
    printf("4_14061216_Shell:%s$ ", get_current_dir_name()); //��ӡ��ʾ����Ϣ

    while(1){
        //yacc�﷨�����������ڵ㣬���ÿ�ʼ���������������ɹ�����0�����򷵻ط���ֵ
        yyparse(); //�����﷨�����������ú�����yylex()�ṩ��ǰ����ĵ��ʷ�
        if(commandDone == 1){ //�����Ѿ�ִ����ɺ������ʷ��¼��Ϣ
            commandDone = 0;
            addHistory(inputBuff);
        }
        printf("4_14061216_Shell:%s$ ",get_current_dir_name()); //��ӡ��ʾ����Ϣ
     }

    return (EXIT_SUCCESS);
}
