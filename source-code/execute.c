/*
1.添加了几个不支持参数的简单内部命令
2.修改了CTRL+C 没有彻底删除进程，还残留在jobs中的错误。jobs只保存没有执行完的作业
3.修改了fg命令重启的进程无法再被Ctrl Z/C中断的错误
*/
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

#include "global.h"

#define DEBUG
int goon = 0, ingnore = 0;       //用于设置signal信号量
char *envPath[10], cmdBuff[40];  //外部命令的存放路径及读取外部命令的缓冲空间
History history;                 //历史命令
Job *head = NULL;                //作业头指针
pid_t fgPid;                     //当前前台作业的进程号

int ifCtrl = 0;

/*******************************************************
				  工具以及辅助方法
				  ********************************************************/
/*判断命令是否存在*/
int exists(char *cmdFile){
	int i = 0;
	if ((cmdFile[0] == '/' || cmdFile[0] == '.') && access(cmdFile, F_OK) == 0/*【文件是否存在，返回0存在，-1不存在】*/){ //命令在当前目录
		strcpy(cmdBuff, cmdFile);
		return 1;
	}
	else{  //查找ysh.conf文件中指定的目录，确定命令是否存在
		while (envPath[i] != NULL){ //查找路径已在初始化时设置在envPath[i]中
			strcpy(cmdBuff, envPath[i]);
			strcat(cmdBuff, cmdFile);///【连接两个字符串】

			if (access(cmdBuff, F_OK) == 0){ //命令文件被找到
				return 1;
			}

			i++;
		}
	}

	return 0;
}

///【str[start...end]转化为int，如果有不属于0-9的字符则返回-1】
/*将字符串转换为整型的Pid*/
int str2Pid(char *str, int start, int end){
	int i, j;
	char chs[20];

	for (i = start, j = 0; i < end; i++, j++){
		if (str[i] < '0' || str[i] > '9'){
			return -1;
		}
		else{
			chs[j] = str[i];
		}
	}
	chs[j] = '\0';

	return atoi(chs);
}

///【删除str这个字符串第一个'/'及之前的字符】
/*调整部分外部命令的格式*/
void justArgs(char *str){
	int i, j, len;
	len = strlen(str);

	for (i = 0, j = -1; i < len; i++){
		if (str[i] == '/'){
			j = i;
		}
	}

	if (j != -1){ //找到符号'/'
		for (i = 0, j++; j < len; i++, j++){
			str[i] = str[j];
		}
		str[i] = '\0';
	}
}

/*设置goon*/
void setGoon(){
	goon = 1;
}

/*释放环境变量空间*/
void release(){
	int i;
	for (i = 0; strlen(envPath[i]) > 0; i++){
		free(envPath[i]);   //【释放envPath[i]指向的内存空间】
	}
}

/*******************************************************
				  信号以及jobs相关
				  ********************************************************/
/*添加新的作业*/
Job* addJob(pid_t pid){ //【pid_t就是int】
	Job *now = NULL, *last = NULL, *job = (Job*)malloc(sizeof(Job));

	//初始化新的job
	job->pid = pid;
	strcpy(job->cmd, inputBuff);
	strcpy(job->state, RUNNING);
	job->next = NULL;

	if (head == NULL){ //若是第一个job，则设置为头指针
		head = job;
	}
	else{ //否则，根据pid将新的job插入到链表的合适位置
		now = head;
		while (now != NULL && now->pid < pid){
			last = now;
			now = now->next;
		}
		last->next = job;
		job->next = now;
	}

	return job;
}

/*移除一个作业*/
void rmJob(int sig, siginfo_t *sip, void* noused){
	pid_t pid;
	Job *now = NULL, *last = NULL;

	if (ingnore == 1){
		ingnore = 0;
		return;
	}

	pid = sip->si_pid;

	now = head;
	while (now != NULL && now->pid < pid){
		last = now;
		now = now->next;
	}

	if (now == NULL){ //作业不存在，则不进行处理直接返回
		return;
	}

	//开始移除该作业
	if (now == head){
		head = now->next;
	}
	else{
		last->next = now->next;
	}

	free(now);
}

/*组合键命令ctrl+z*/
void ctrl_Z(){
	Job *now = NULL;

	ifCtrl = 1;

	printf("zfgPid=%d\n", fgPid);

	if (fgPid == 0){ //前台没有作业则直接返回
		ingnore = 1;
		return;   //return 产生的信号会被init()里面声明的signal()捕捉，所以产生了错误
	}

	//SIGCHLD信号产生自ctrl+z
	ingnore = 1;

	now = head;
	while (now != NULL && now->pid != fgPid)
		now = now->next;

	if (now == NULL){ //未找到前台作业，则根据fgPid添加前台作业
		now = addJob(fgPid);
	}

	//修改前台作业的状态及相应的命令格式，并打印提示信息
	strcpy(now->state, STOPPED);
	now->cmd[strlen(now->cmd)] = '&';
	now->cmd[strlen(now->cmd) + 1] = '\0';
	printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);

	//发送SIGSTOP信号给正在前台运作的工作，将其停止
	printf("z\n");
	kill(fgPid, SIGSTOP);
	fgPid = 0;
}

//++++++++++++++++++++++++++++++++++++++++++++
/*组合键命令ctrl+c*/
void ctrl_C(){
	Job *now = NULL;

	ifCtrl = 1;

	if (fgPid == 0){ //前台没有作业则直接返回
		return;
	}

	//SIGCHLD信号产生自ctrl+c
	ingnore = 1;

	now = head;
	printf("%d\n", fgPid);

	/*****************************************************/
	if (now != NULL && now->pid == fgPid){
		now = now->next;
	}
	while (now != NULL && now->next != NULL && now->next->pid != fgPid)
		now = now->next;
	if (now != NULL && now->next != NULL && now->next->pid == fgPid){
		now->next = now->next->next;
	}
	printf("[%d]\t%s\t\t%s\n", fgPid, KILLED, inputBuff);
	/*****************************************************/

	//发送SIGSTOP信号给正在前台运作的工作，将其停止
	kill(fgPid, SIGKILL);
	fgPid = 0;
}

//++++++++++++++++++++++++++++++++++++++++++++





/*fg命令   fg %pid  不能有空格*/
void fg_exec(int pid){
	Job *now = NULL;
	int i;

	//SIGCHLD信号产生自此函数
	ingnore = 1;

	//根据pid查找作业，从作业链表头部开始寻找
	now = head;
	while (now != NULL && now->pid != pid)
		now = now->next;

	if (now == NULL){ //未找到作业
		printf("pid为7%d 的作业不存在！\n", pid);
		return;
	}

	//记录前台作业的pid，修改对应作业状态
	fgPid = now->pid;
	strcpy(now->state, RUNNING);

	signal(SIGTSTP, ctrl_Z); //设置signal信号，为下一次按下组合键Ctrl+Z做准备
	ifCtrl = 0;

	i = strlen(now->cmd) - 1;
	while (i >= 0 && now->cmd[i] != '&')
		i--;
	now->cmd[i] = '\0';

	printf("%s\n", now->cmd);
	kill(now->pid, SIGCONT); //向对象作业发送SIGCONT信号，使其运行
	printf("%d\n", fgPid);
	//waitpid(fgPid, NULL, 0); //父进程等待前台进程的运行
	/***************************************************/
	while (waitpid(fgPid, NULL, 0) != fgPid && ifCtrl == 0);
	/***************************************************/
}

/*bg命令*/
void bg_exec(int pid){
	Job *now = NULL;

	//SIGCHLD信号产生自此函数
	ingnore = 1;

	//根据pid查找作业
	now = head;
	while (now != NULL && now->pid != pid)
		now = now->next;

	if (now == NULL){ //未找到作业
		printf("pid为7%d 的作业不存在！\n", pid);
		return;
	}
	/************************************/
	signal(SIGTSTP, ctrl_Z);
	/************************************/

	strcpy(now->state, RUNNING); //修改对象作业的状态
	printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);

	kill(now->pid, SIGCONT); //向对象作业发送SIGCONT信号，使其运行
}

/*******************************************************
					命令历史记录
					********************************************************/
void addHistory(char *cmd){
	if (history.end == -1){ //第一次使用history命令
		history.end = 0;
		strcpy(history.cmds[history.end], cmd);
		return;
	}

	history.end = (history.end + 1) % HISTORY_LEN; //end前移一位
	strcpy(history.cmds[history.end], cmd); //将命令拷贝到end指向的数组中

	if (history.end == history.start){ //end和start指向同一位置
		history.start = (history.start + 1) % HISTORY_LEN; //start前移一位
	}
}

/*******************************************************
					 初始化环境
					 ********************************************************/
/*通过路径文件获取环境路径*/
void getEnvPath(int len, char *buf){
	int i, j, last = 0, pathIndex = 0, temp;
	char path[40];

	for (i = 0, j = 0; i < len; i++){
		if (buf[i] == ':'){ //将以冒号(:)分隔的查找路径分别设置到envPath[]中
			if (path[j - 1] != '/'){
				path[j++] = '/';
			}
			path[j] = '\0';
			j = 0;

			temp = strlen(path);
			envPath[pathIndex] = (char*)malloc(sizeof(char)* (temp + 1));
			strcpy(envPath[pathIndex], path);

			pathIndex++;
		}
		else{
			path[j++] = buf[i];
		}
	}

	envPath[pathIndex] = NULL;
}

/*初始化操作*/
void init(){
	int fd, n, len;
	char c, buf[80];

	//打开查找路径文件ysh.conf
	if ((fd = open("ysh.conf", O_RDONLY, 660)) == -1){
		perror("init environment failed\n");
		exit(1);
	}

	//初始化history链表
	history.end = -1;
	history.start = 0;

	len = 0;
	//将路径文件内容依次读入到buf[]中
	while (read(fd, &c, 1) != 0){
		buf[len++] = c;
	}
	buf[len] = '\0';

	//将环境路径存入envPath[]
	getEnvPath(len, buf);

	//注册信号
	struct sigaction action;
	action.sa_sigaction = rmJob;
	sigfillset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGCHLD, &action, NULL);
	signal(SIGTSTP, ctrl_Z);

	//++++++
	signal(SIGINT, ctrl_C);
	//++++++

	/*******************************************************/
	signal(SIGUSR1, setGoon); //不注册的话无法在进程间通信，会卡死在进程里
	/*******************************************************/
}

/*******************************************************
					  命令解析
					  ********************************************************/
SimpleCmd* handleSimpleCmdStr(char *_inputBuff, int begin, int end){
	int i, j, k;
	int fileFinished; //记录命令是否解析完毕
	char c, buff[10][40], inputFile[30], outputFile[30], *temp = NULL;
	SimpleCmd *cmd = (SimpleCmd*)malloc(sizeof(SimpleCmd));

	//printf("%s|||",_inputBuff);

	//默认为非后台命令，输入输出重定向为null
	cmd->isBack = 0;
	cmd->input = cmd->output = NULL;

	//初始化相应变量
	for (i = begin; i < 10; i++){
		buff[i][0] = '\0';
	}
	inputFile[0] = '\0';
	outputFile[0] = '\0';

	i = begin;
	//跳过空格等无用信息
	while (i < end && (_inputBuff[i] == ' ' || _inputBuff[i] == '\t')){
		i++;
	}

	k = 0;
	j = 0;
	fileFinished = 0;
	temp = buff[k]; //以下通过temp指针的移动实现对buff[i]的顺次赋值过程
	while (i < end){
		/*根据命令字符的不同情况进行不同的处理*/
		switch (_inputBuff[i]){
			//遇到空格开始读参数
		case ' ':
		case '\t': //命令名及参数的结束标志
			temp[j] = '\0';
			j = 0;
			if (!fileFinished){
				k++;
				temp = buff[k];
			}
			break;

		case '<': //输入重定向标志
			if (j != 0){
				//此判断为防止命令直接挨着<符号导致判断为同一个参数，如果ls<sth
				temp[j] = '\0';
				j = 0;
				if (!fileFinished){
					k++;
					temp = buff[k];
				}
			}
			temp = inputFile;
			fileFinished = 1;
			i++;
			break;

		case '>': //输出重定向标志
			if (j != 0){
				temp[j] = '\0';
				j = 0;
				if (!fileFinished){
					k++;
					temp = buff[k];
				}
			}
			temp = outputFile;
			fileFinished = 1;
			i++;
			break;

		case '&': //后台运行标志
			if (j != 0){
				temp[j] = '\0';
				j = 0;
				if (!fileFinished){
					k++;
					temp = buff[k];
				}
			}
			cmd->isBack = 1;
			fileFinished = 1;
			i++;
			break;

		default: //默认则读入到temp指定的空间
			temp[j++] = _inputBuff[i++];
			continue;
		}

		//跳过空格等无用信息
		while (i < end && (_inputBuff[i] == ' ' || _inputBuff[i] == '\t')){
			i++;
		}
	}

	if (_inputBuff[end - 1] != ' ' && _inputBuff[end - 1] != '\t' && _inputBuff[end - 1] != '&'){
		temp[j] = '\0';
		if (!fileFinished){
			k++;
		}
	}

	//依次为命令名及其各个参数赋值
	cmd->args = (char**)malloc(sizeof(char*)* (k + 1));
	cmd->args[k] = NULL;
	for (i = 0; i < k; i++){
		j = strlen(buff[i]);
		cmd->args[i] = (char*)malloc(sizeof(char)* (j + 1));
		strcpy(cmd->args[i], buff[i]);
	}

	//如果有输入重定向文件，则为命令的输入重定向变量赋值
	if (strlen(inputFile) != 0){
		j = strlen(inputFile);
		cmd->input = (char*)malloc(sizeof(char)* (j + 1));
		strcpy(cmd->input, inputFile);
	}

	//如果有输出重定向文件，则为命令的输出重定向变量赋值
	if (strlen(outputFile) != 0){
		j = strlen(outputFile);
		cmd->output = (char*)malloc(sizeof(char)* (j + 1));
		strcpy(cmd->output, outputFile);
	}
#ifdef DEBUG
	printf("****\n");
	printf("isBack: %d\n", cmd->isBack);
	for (i = 0; cmd->args[i] != NULL; i++){
		printf("args[%d]: %s\n", i, cmd->args[i]);
	}
	printf("input: %s\n", cmd->input);
	printf("output: %s\n", cmd->output);
	printf("****\n");
#endif
	return cmd;
}

/*******************************************************
					  命令执行
					  ********************************************************/
/*执行外部命令*/
void execOuterCmd(SimpleCmd *cmd){


	pid_t pid;
	int pipeIn, pipeOut;

	if (exists(cmd->args[0])){ //命令存在
		if ((pid = fork()) < 0){
			perror("fork failed");
			return;
		}

		if (pid == 0){ //子进程
			if (cmd->input != NULL){ //存在输入重定向
				if ((pipeIn = open(cmd->input, O_RDONLY, S_IRUSR | S_IWUSR)) == -1){
					printf("不能打开文件 %s！\n", cmd->input);
					return;
				}
				if (dup2(pipeIn, 0) == -1){
					printf("重定向标准输入错误！\n");
					return;
				}
			}

			if (cmd->output != NULL){ //存在输出重定向
				if ((pipeOut = open(cmd->output, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) == -1){
					printf("不能打开文件 %s！\n", cmd->output);
					return;
				}
				if (dup2(pipeOut, 1) == -1){
					printf("重定向标准输出错误！\n");
					return;
				}
			}

			if (cmd->isBack){ //若是后台运行命令，等待父进程增加作业
				signal(SIGUSR1, setGoon); //收到信号，setGoon函数将goon置1，以跳出下面的循环
				while (goon == 0); //等待父进程SIGUSR1信号，表示作业已加到链表中
				goon = 0; //置0，为下一命令做准备

				printf("[%d]\t%s\t\t%s\n", getpid(), RUNNING, inputBuff);
				kill(getppid(), SIGUSR1);
			}

			///如果路径是"./"sh会傻了吧唧的把它当成/bin(我猜的)，然后就扯了，所以不调用sh+++++++++++++++++
			int flag_is_usrprog = 0;
			if (cmd->args[0][0] == '.'&&cmd->args[0][1] == '/')
				flag_is_usrprog = 1;
			//++++++++++++++++++++++++++++++

			justArgs(cmd->args[0]);

			//++++++++++++++++++++++++++

			int k;
			int len = 0;
			for (k = 0; cmd->args[k] != NULL; k++)
				len += strlen(cmd->args[k]) + 1;
			char **inst = (char**)malloc(sizeof(char*)* (4));

			inst[3] = NULL;
			inst[0] = (char*)malloc(sizeof(char)* 3);
			strcpy(inst[0], "sh");
			inst[1] = (char*)malloc(sizeof(char)* 3);
			strcpy(inst[1], "-c");
			inst[2] = (char*)malloc(sizeof(char)*len);
			int i;
			strcpy(inst[2], cmd->args[0]);
			strcat(inst[2], "  ");
			for (i = 1; i < k; i++)
			{
				strcat(inst[2], cmd->args[i]);
				strcat(inst[2], "  ");
			}
			//printf("%s\n",inst[2]);


			if (flag_is_usrprog == 1)
			{
				if (execv(cmdBuff, cmd->args) < 0)
				{ //执行命令
					printf("execv failed!\n");
					return;
				}
			}
			else
			{
				if (execv("/bin/sh", inst) < 0)
				{ //执行命令
					printf("execv failed!\n");
					return;
				}
			}

			//++++++++++++++++++++++++++

			// 			if (execv(cmdBuff, cmd->args) < 0){ //执行命令
			// 				printf("execv failed!\n");
			// 				return;
			// 			}
		}
		else{ //父进程
			if (cmd->isBack){ //后台命令             
				fgPid = 0; //pid置0，为下一命令做准备
				addJob(pid); //增加新的作业
				kill(pid, SIGUSR1); //子进程发信号，表示作业已加入

				//等待子进程输出
				signal(SIGUSR1, setGoon);
				while (goon == 0);
				goon = 0;

				/************************/
				waitpid(pid, NULL, 0);
				/***********************/
			}
			else{ //非后台命令
				fgPid = pid;
				waitpid(pid, NULL, 0);
			}
		}
	}
	else{ //命令不存在
		printf("找不到命令 15%s\n", inputBuff);
	}
}

/*执行命令*/
void execSimpleCmd(SimpleCmd *cmd){
	int i, pid, len, j, k = 0;
	char *temp;
	char address[100], c;
	Job *now = NULL;
	char *internalcommand[] = { "cd", "exit", "fg", "bg", "history", "jobs", "kill", "echo", "pwd", "type", "read", "break", "continue", "export", "return", "shift", "test", "exec", "eval", "hash", "readonly", "trap", "unset", "umask", "wait" };


	if (strcmp(cmd->args[0], "exit") == 0) { //exit命令
		exit(0);
	}
	else if (strcmp(cmd->args[0], "history") == 0) { //history命令
		if (history.end == -1){
			printf("尚未执行任何命令\n");
			return;
		}
		i = history.start;
		do {
			printf("%s\n", history.cmds[i]);
			i = (i + 1) % HISTORY_LEN;
		} while (i != (history.end + 1) % HISTORY_LEN);
	}
	else if (strcmp(cmd->args[0], "jobs") == 0) { //jobs命令
		if (head == NULL){
			printf("尚无任何作业\n");
		}
		else {
			printf("index\tpid\tstate\t\tcommand\n");
			for (i = 1, now = head; now != NULL; now = now->next, i++){
				printf("%d\t%d\t%s\t\t%s\n", i, now->pid, now->state, now->cmd);
			}
		}
	}
	else if (strcmp(cmd->args[0], "cd") == 0) { //cd命令
		temp = cmd->args[1];
		if (temp != NULL){
			//chdir()函数，改变当前目录
			if (chdir(temp) < 0){
				printf("cd; %s Incorrect file name or folder name！\n", temp);
			}
		}
	}
	else if (strcmp(cmd->args[0], "fg") == 0) { //fg命令
		temp = cmd->args[1];
		if (temp != NULL && temp[0] == '%'){
			pid = str2Pid(temp, 1, strlen(temp));
			if (pid != -1){
				fg_exec(pid);
			}
		}
		else{
			printf("fg; Parameters not lawful,the correct format is：fg %<int>\n");
		}
	}
	else if (strcmp(cmd->args[0], "bg") == 0) { //bg命令
		temp = cmd->args[1];
		if (temp != NULL && temp[0] == '%'){
			pid = str2Pid(temp, 1, strlen(temp));

			if (pid != -1){
				bg_exec(pid);
			}
		}
		else{
			printf("bg; Parameters not lawful,the correct format is：bg %<int>\n");
		}
	}
	/*********************************************************/
	else if (strcmp(cmd->args[0], "echo") == 0) { //echo命令
		temp = cmd->args[1];
		len = strlen(temp);
		if (temp != NULL && temp[0] == '"' && temp[len - 1] == '"'){
			for (i = 1; i < len - 1; i++){
				printf("%c", temp[i]);
			}
			printf("\n");
		}
		else{
			printf("echo; Parameters not lawful,the correct format is：echo \"...\"\n");
		}
	}
	else if (strcmp(cmd->args[0], "pwd") == 0){ //pwd命令
		if (getcwd(address, sizeof(address)) != NULL){
			printf("current working directory: %s\n", address);
		}
		else{
			printf("fales to get current working directory\n");
		}
	}
	else if (strcmp(cmd->args[0], "kill") == 0){ //kill命令
		temp = cmd->args[1];
		pid = atoi(temp);
		if (kill(pid, SIGKILL) < 0){
			printf("false to kill %d\n", pid);
		}

	}
	else if (strcmp(cmd->args[0], "type") == 0){
		for (i = 1; cmd->args[i] != NULL; i++){
			temp = cmd->args[i];
			for (j = 0; internalcommand[j] != NULL; j++){
				if ((strcmp(temp, internalcommand[j])) == 0){
					k = 1;
					printf("%s is a shell builtin\n", cmd->args[i]);
					break;
				}
			}
			if (k == 0){
				printf("%s is in /bin/%s\n", cmd->args[i], cmd->args[i]);
			}
			k = 0;
		}
	}
	else{ //外部命令
		execOuterCmd(cmd);
	}

	//释放结构体空间
	for (i = 0; cmd->args[i] != NULL; i++){
		free(cmd->args[i]);
		free(cmd->input);
		free(cmd->output);
	}
}


int exec_pipe(char *str)
{
	int i = 0, j = 0, n = 0;
	int flag = 0;
	char temp_chr_1st[100][100] = { { 0 } };

	for (i = 0; i < strlen(str); i++)
	{
		if (str[i] == '|')
		{
			flag = 1;
			temp_chr_1st[n][j] = '\0';
			j = 0;
			n++;
		}
		else
		{
			temp_chr_1st[n][j++] = str[i];
		}
	}
	if (flag == 0)
		return 0;

	for (i = 0; i <= n; i++)
	{
		if (i == 0)
		{
			strcat(temp_chr_1st[i], " > temp1.txt");
			SimpleCmd *cmd = handleSimpleCmdStr(temp_chr_1st[i], 0, strlen(temp_chr_1st[i]));
			execSimpleCmd(cmd);
		}
		else if (i == n)
		{
			if (i % 2 == 1)
			{
				strcat(temp_chr_1st[i], " < temp1.txt");
				SimpleCmd *cmd = handleSimpleCmdStr(temp_chr_1st[i], 0, strlen(temp_chr_1st[i]));
				execSimpleCmd(cmd);
			}
			else
			{
				strcat(temp_chr_1st[i], " < temp2.txt");
				SimpleCmd *cmd = handleSimpleCmdStr(temp_chr_1st[i], 0, strlen(temp_chr_1st[i]));
				execSimpleCmd(cmd);
			}
		}
		else
		{
			if (i % 2 == 1)
			{
				strcat(temp_chr_1st[i], " < temp1.txt");
				strcat(temp_chr_1st[i], " > temp2.txt");
				SimpleCmd *cmd = handleSimpleCmdStr(temp_chr_1st[i], 0, strlen(temp_chr_1st[i]));
				execSimpleCmd(cmd);
			}
			else
			{
				strcat(temp_chr_1st[i], " < temp2.txt");
				strcat(temp_chr_1st[i], " > temp1.txt");
				SimpleCmd *cmd = handleSimpleCmdStr(temp_chr_1st[i], 0, strlen(temp_chr_1st[i]));
				execSimpleCmd(cmd);
			}
		}
	}
	return 1;

}


/*******************************************************
					 命令执行接口
					 ********************************************************/
void execute(){
	if (exec_pipe(inputBuff) == 0)
	{
		SimpleCmd *cmd = handleSimpleCmdStr(inputBuff, 0, strlen(inputBuff));
		execSimpleCmd(cmd);
	}
}
