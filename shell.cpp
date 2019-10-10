#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define MAX_LINE 80 /* 80 chars per line, per command */

//Mảng lưu lịch sử biến needToWait
	//của tiến trình cha với mỗi command line. Ví dụ:
	//history[0] = "cat prog.c &" thì history_wait[0] = 0
	int history_wait[10];

//Mảng lưu lịch sử command line
	char* history[10][MAX_LINE / 2 + 1];

//Biến lưu vị trí con trỏ trỏ vào history
	//default = 0;
	int buffHead = 0;
//Biến lưu đối số trỏ về màn hình console
	int stdoutBack = dup(1);
//Biến lưu vị trí chuỗi ">" hoặc "<" 
	//trong chức năng redirecting i/o
	//default = -1 (không thuộc TH trên)
	int ioIdx = -1;
//Mảng lưu tạm Buffer phục vụ chức năng redirecting i/o
	char** ioBuff = (char**)malloc((MAX_LINE / 2 + 1) * sizeof(char*));
//Cờ phục vụ chức năng communication via a pipe
	int pipeFlag;

//Hàm khởi tạo mảng history
void initHistory(void);
//Hàm hủy mảng history
void freeHistory(void);
//Hàm in mảng history
void printHistory(void);
//Hàm tính toán xử lý chức năng History Feature
char** history_computation(char** args, int* needWait, int* emptyHistory);
//Hàm xuất output ra file
void output(char** args, int i);
//Hàm truyền input vào shell
void input(char** args, int i); 
//Hàm khởi tạo mảng ioBuff
void initIOBuf(char** args, int i);
//Hàm tính toán xử lý chức năng Communication via a Pipe
void pipe(char** args, int i);

int main(void)
{
	char* args[MAX_LINE / 2 + 1]; /* command line (of 80) has max of 40 arguments */
	int shouldRun = 1;
	initHistory();
	while (shouldRun)
	{
	//Kiểm tra có rơi vào trường hợp command line
		//gần nhất được sử dụng thuộc về chức năng
		//redirecting i/o hay không? Nếu có,
		//tiến hành xử lý history
		if (ioIdx != -1)
		{
			dup2(stdoutBack, 1);
			if (ioIdx != -1)
			{
				int j;
				for (j = 0; ioBuff[j] != NULL; j++)
					history[(buffHead - 1) % 10][ioIdx + j] = ioBuff[j];
				history[(buffHead - 1) % 10][ioIdx + j] = ioBuff[j];
			}
		}

		ioIdx = -1;
		printf("osh>");
		fflush(stdout);
		pid_t pid;
		char cmd_line[MAX_LINE + 1];
		char* sptr = cmd_line;
		int av = 0;

	//Đọc command line của user vào biến cmd_line
		if (scanf("%[^\n]%*1[\n]", cmd_line) < 1)
		{
			if (scanf("%1[\n]", cmd_line) < 1)
			{
				printf("STDIN FAILED\n");
				return 1;
			}
			continue;
		}

	//Phân tích command line
		//Xóa kí tự không hợp lệ, trỏ tới kí tự tiếp theo
		while (*sptr == ' ' || *sptr == '\t')
		{
			sptr++;
		}
		//Xử lý lấy từng chuỗi đối số cách nhau bởi
			//dấu cách " ". Ví dụ: "ls -l", input:
			//"ls" và "-l"
		while (*sptr != '\0') 
		{
			//Khởi tạo mảng lưu tạm dấu cách " "
			char* tempBuff = (char*)malloc((MAX_LINE + 1) * sizeof(char));
			//Khởi tạo mảng lưu các đối số (chuỗi) của command line.
			args[av] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
			int ret = sscanf(sptr, "%[^ \t]", args[av]);
			sptr += strlen(args[av]);
			if (ret < 1)
			{
				printf("INVALID COMMAND\n");
				return 1;
			}
			ret = sscanf(sptr, "%[ \t]", tempBuff);
			if (ret > 0)
			{
				sptr += strlen(tempBuff);
			}
			av++;
			free(tempBuff);
		}
		//Xử lý việc command line user nhập vào có dấu "&" ở phía sau.
			//Cờ kiểm tra liệu tiến trình cha:
				//1 là đợi, 0 là không đợi - tiến trình con để thoát.
			int needToWait = 1;
			//Kiểm tra nếu có dấu "&"
		if (strlen(args[av - 1]) == 1 && args[av - 1][0] == '&')
		{
			needToWait = 0; /*Set cờ = 0.*/
			free(args[av - 1]);
			args[av - 1] = NULL;
		}
			//không có dấu "&"
		else
		{
			args[av] = NULL;
		}
		//Xử lý thoát chương trình khi người dùng nhập vào "exit"
		if (strcmp(args[0], "exit") == 0)
		{
			freeHistory();
			if (ioBuff)
			{
				free(ioBuff);
			}
			return 0;
		}
		//Redirecting Input/Output
			//input
		for (int i = 0; args[i] != NULL; i++)
		{
			if (strcmp(args[i], "<") == 0 && args[i + 1] != NULL)
			{
				ioIdx = i;
				input(args, i);
				break;
			}
		}
			//output
		for (int i = 0; args[i] != NULL; i++)
		{
			if (strcmp(args[i], ">") == 0 && args[i + 1] != NULL)
			{
				ioIdx = i;
				output(args, i);
				break;
			}
		}
		//Communication via a Pipe
			//Đặt cờ pipeFlag (0 = run (1), 1 = no need)
				//Vì bản thân trong hàm pipe đã có
				//hàm thực thi lệnh execvp()
				//nên ở phía dưới ta không cần chạy
				//code trong phần "Fork child to Execute args"
		pipeFlag = 0;
		for (int i = 0; args[i] != NULL; i++)
		{
			if (strcmp(args[i], "|") == 0 && args[i + 1] != NULL)
			{
				pipe(args, i);
				pipeFlag = 1; /*Set cờ = 1*/
				break;
			}
		}

	//History Computation - tính toán xử lý liên quan đến
		//History Feature
		//In ra lịch sử thực thi lệnh (10 command line gần nhất)
			//nếu user nhập vào "history"
		if (args[1] == NULL && strcmp(args[0], "history") == 0)
		{
			printHistory();
			continue;
		}
		//Cờ nếu người dùng muốn thực hiện !! khi chưa có dòng lệnh nao`
		int* emptyHistory = (int*)malloc(sizeof(int));
		*emptyHistory = 0;
		//Gọi hàm tính toán xử lý chức năng History Feature
		char** argsPtr = history_computation(args, &needToWait,emptyHistory);
		//Sau khi thoát hàm, ta có được con trỏ argsPtr
			//trỏ vào vùng nhớ lưu lệnh cần thực thi
			//trước khi được bỏ vào hàm execvp() của Linux
		if (*emptyHistory == 1)
		{
			continue;
		}
		//Xử lý ngoại lệ.
			//Xét trong trường hợp user nhập vào "!!" hoặc "![%d]"
				//mà lệnh đó là lệnh yêu cầu xử lý chức năng
				//redirecting output
		for (int i = 0; argsPtr[i] != NULL; i++)
		{
			if (strcmp(argsPtr[i], ">") == 0 && argsPtr[i + 1] != NULL)
			{
				ioIdx = i;
				output(argsPtr, i);
				break;
			}
		}
			//Xét trong trường hợp user nhập vào "!!" hoặc "![%d]"
				//mà lệnh đó là lệnh yêu cầu xử lý chức năng
				//redirecting input
		for (int i = 0; argsPtr[i] != NULL; i++)
		{
			if (strcmp(argsPtr[i], "<") == 0 && argsPtr[i + 1] != NULL)
			{
				ioIdx = i;
				input(argsPtr, i);
				break;
			}
		}
			//Xét trong trường hợp user nhập vào "!!" hoặc "![%d]"
				//mà lệnh đó là lệnh yêu cầu xử lý chức năng
				//communication via a pipe
		for (int i = 0; argsPtr[i] != NULL; i++)
		{
			if (strcmp(argsPtr[i], "|") == 0 && pipeFlag == 0 && argsPtr[i + 1] != NULL)
			{
				pipe(argsPtr, i);
				pipeFlag = 1;
				break;
			}
		}

	//Fork child to Execute args (1)
		//Tạo tiến trình con để thực thi lệnh
		if (pipeFlag == 0) /*Kiểm tra cờ pipeFlag*/
		{
			pid = fork();
			if (pid < 0) /*không thể phân thân*/
			{
				printf("FORK FAILED\n");
				return 1;
			}
			else if (pid == 0) /*code chạy trong tiến trình con*/
			{
				if (execvp(argsPtr[0], argsPtr)) /*execvp() trả về 0/1 (đúng/sai)*/
				{
					printf("INVALID COMMAND\n");
					return 1;
				}
			}
			else /*pid > 0*/
			{
				if (needToWait)
				{
					while (wait(NULL) != pid);
				}
				else
				{
					printf("[1]%d\n", pid);
				}
			}
		}
	}
	return 0;
}

char** history_computation(char** args, int* needWait, int* emptyHistory)
{
	int i;
	//Kiểm tra lệnh nhập vào là "!!"
	if (args[1] == NULL && strcmp(args[0], "!!") == 0)
	{
		//Kiểm tra biến buffHead xem đây có phải
			//lệnh đầu tiên không
		if (buffHead > 0)  
		{
			strcpy(args[0], history[(buffHead - 1) % 10][0]);
			for (i = 1; history[(buffHead - 1) % 10][i] != NULL; i++)
			{
				args[i] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
				strcpy(args[i], history[(buffHead - 1) % 10][i]);
			}
			args[i] = NULL;
			*needWait = history_wait[(buffHead - 1) % 10];
		}
		else /*buffHead <= 0*/
		{
			printf("NO COMMANDS IN HISTORY\n");
			*emptyHistory = 1;
			return args;
		}
	}
	//Kiểm tra lệnh nhập vào là lệnh "![%d]"
	else if (args[1] == NULL && args[0][0] == '!')
	{
		int idx;
		char* sptr = &(args[0][1]);
		if (sscanf(sptr, "%d", &idx) == 1)
		{
			if (idx > 0 && buffHead > idx - 1 && idx > buffHead - 9)
			{
				strcpy(args[0], history[(idx - 1) % 10][0]);
				for (i = 1; history[(idx - 1) % 10][i] != NULL; i++)
				{
					args[i] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
					strcpy(args[i], history[(idx - 1) % 10][i]);
				}
				args[i] = NULL;
				*needWait = history_wait[(idx - 1) % 10];
			}
		//Xử lý việc user truy cập sai cách:
			//vượt quá giới hạn index
			else
			{
				printf("NO SUCH COMMAND IN HISTORY(index out of range)\n");
				return args;
			}
		}
			//nhập sai index
		else
		{
			printf("NO SUCH COMMAND IN HISTORY(invalid index)\n");
			return args;
		}
	}
	//Lưu lại lệnh vừa nhập vào lịch sử
	for (i = 0; i < (MAX_LINE / 2 + 1) && history[buffHead % 10][i] != NULL; i++)
	{
		free(history[buffHead % 10][i]);
	}
	for (i = 0; args[i] != NULL; i++)
	{
		history[buffHead % 10][i] = args[i];
	}
	history[buffHead % 10][i] = args[i];
	history_wait[buffHead % 10] = *needWait;
	return history[(buffHead++) % 10];
}

void initHistory(void)
{
	int i, j;
	for (i = 0; i < 10; i++)
	{
		for (j = 0; j < (MAX_LINE / 2 + 1); j++)
		{
			history[i][j] = NULL;
		}
		history_wait[i] = 0;
	}
}

void freeHistory(void)
{
	int i, j;
	for (i = 0; i < 10 && i < buffHead; i++)
	{
		for (j = 0; history[i][j] != NULL; j++)
		{
			if (history[i][j])
				free(history[i][j]);
		}
	}
}

void printHistory(void)
{
	int i, j;
	for (i = 0; i < 10 && i < buffHead; i++)
	{
		int index;
		if (buffHead > 10)
		{
			index = buffHead - 9 + i;
		}
		else /*buffHead <= 10*/
		{
			index = i + 1;
		}
		printf("[%d] ", index);
		for (j = 0; history[(index - 1) % 10][j] != NULL; j++)
		{
			printf("%s ", history[(index - 1) % 10][j]);
		}
		if (history_wait[(index - 1) % 10] == 0)
		{
			printf("&");
		}
		printf("\n");
	}
}

void initIOBuf(char** args, int i)
{
	int j;
	for (j = 0; args[j + i] != NULL; j++)
	{
		ioBuff[j] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
		strcpy(ioBuff[j], args[j + i]);
	}
	ioBuff[j] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
	ioBuff[2] = NULL;
}

void output(char** args, int i)
{
	int f = fileno(fopen(args[i + 1], "w"));
	dup2(f, 1);
	initIOBuf(args, i);
	args[i] = NULL;
	args[i + 1] = NULL;
	close(f);
}

void input(char** args, int i)
{
	int f = fileno(fopen(args[i + 1], "r"));
	initIOBuf(args, i);
	for (int j = i; args[j] != NULL; j++)
	{
		args[j] = args[j + 1];
	}
	close(f);
}

void pipe(char** args, int i)
{
	//mảng chứa 2 phần tử file descriptor
		//(pipefd[0] read end, pipefd[1] write end)
	int pipefd[2]; 
	char* argv1[MAX_LINE / 2 + 1];
	char* argv2[MAX_LINE / 2 + 1];
	int k = 0;
	pid_t pid1, pid2;
	for (; k < i; k++)
	{
		argv1[k] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
		argv1[k] = args[k];
	}
	argv1[k] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
	argv1[k] = NULL;
	int j = i;
	for (; args[j] != NULL; j++)
	{
		argv2[j] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
		argv2[j] = args[j];
	}
	argv2[j] = (char*)malloc((MAX_LINE + 1) * sizeof(char));
	argv2[j] = NULL;
	if (pipe(pipefd) < 0)
	{
		pipeFlag = 1;
		printf("PIPE FAILED\n");
		return;
	}
	pid1 = fork();
	if (pid1 < 0) /*không thể phân thân*/
	{
		printf("FORK 1 FAILED\n");
		return;
	}
	else if (pid1 == 0) /*code chạy trong tiến trình con pid1*/
	{
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[0]);
		execvp(argv1[0], argv1);
	}
	else /*pid1 > 0 - code chạy trong tiến trình cha*/
	{
		pid2 = fork();
		if (pid2 < 0)
		{
			printf("FORK 2 FAILED\n");
			return;
		}
		else if (pid2 == 0) /*code chạy trong tiến trình con pid2*/
		{
			dup2(pipefd[0], STDIN_FILENO);
			close(pipefd[1]);
			execvp(argv1[0], argv1);
		}
		else
		{
			while (wait(NULL) != pid2); /*đợi tiến trình con chạy xong mới thoát*/
		}
	}
	close(pipefd[0]);
	close(pipefd[1]);
}
