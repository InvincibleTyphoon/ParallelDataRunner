#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#define MAX_SINGLE_INPUT 8192
#define GENERAL_INTEGER 100
#define WREND 1
#define RDEND 0
#define CHILD 0

pthread_mutex_t mutex;
char*** threadArgvs;
int threadArgcs[GENERAL_INTEGER];
int numOfThreadArgments;

int numOfWorkerThreads;
char ** input;
int run = 1;

typedef struct _Queue{
	char ** que;
	int size;
	int front;
	int tail;
}Que;

Que outputQue;
Que inputQue;

void que_init(Que * que){
	que->size = 10000;
	que->tail = -1;
	que->que = (char**)calloc(1,sizeof(char*)*que->size);
	que->front = 0;
}

void que_push(Que * que, char * string){
//	printf("que push entered\n");
	pthread_mutex_lock(&mutex);
	if(que->tail >= que->size){
		que->size*=2;
		que->que = (char**)realloc(que->que,sizeof(char*)*que->size);
	}

	que->tail++;
	que->que[que->tail] = string;
	pthread_mutex_unlock(&mutex);
//	printf("que[%d] pushed: %s",que->tail , string);

}

char * que_pop(Que * que){
	char* ptr;
	pthread_mutex_lock(&mutex);
//	printf("que_pop entered\n");
//	printf("que_pop mutex locked\n");
	if(que->front > que->tail){
		pthread_mutex_unlock(&mutex);
		return NULL;
	}
	ptr = que->que[que->front];

	que->front++;
	pthread_mutex_unlock(&mutex);
//	printf("que_pop done\n");
	return ptr;
}

void error_handling(char message[]){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

int isthreadCreated = 0;
int receiverRun = 1;
void* receiverFunc(void * a){
	char * ptr;
	isthreadCreated = 1;

	while(1){
		ptr = que_pop(&outputQue);
		if(receiverRun == 0 && (ptr == NULL)){
			if(outputQue.front > outputQue.tail)
				break;
		}
		if(ptr == NULL)
			continue;
		printf("%s",ptr);
//		fflush(stdout);
		free(ptr);
	}
	return NULL;
}

void createProcessesForWorker(int argvIdx, int toThisProcess,int pipeToWorker){
	int pfd[2];
	int pid;

	if(argvIdx >= numOfThreadArgments - 1){
		dup2(toThisProcess, STDIN_FILENO);
		close(toThisProcess);
		dup2(pipeToWorker, STDOUT_FILENO);
		close(pipeToWorker);
		execvp(threadArgvs[argvIdx][0], threadArgvs[argvIdx]);
		error_handling("exec() Error!_createProcessesForWorker()_argvIdx >= numOfThreadArgments");		
	}
	pipe(pfd);


	pid = fork();

	if(pid < 0)
		error_handling("pipe() Error!_createProcessesForWorker()");
	else if(pid == CHILD){
		close(pfd[WREND]);
		createProcessesForWorker(argvIdx+1, pfd[RDEND] ,pipeToWorker);
	}
	else{
		close(pfd[RDEND]);
		dup2(toThisProcess, STDIN_FILENO);
		close(toThisProcess);
		close(pipeToWorker);
		dup2(pfd[WREND], STDOUT_FILENO);
		close(pfd[WREND]);
		execvp(threadArgvs[argvIdx][0], threadArgvs[argvIdx]);
		error_handling("exec() Error!_createProcessesForWorker()");
	}
}

void* workerFunc(void * a){
//	int thisIdx = *((int*)a);
	int pid;
	int parentToChild[2];
	int childToParent[2];
	char * ptr;
	char* buf;
	char staticBuf[MAX_SINGLE_INPUT];
	int readLen;

	FILE* writer;
	FILE* reader;
	isthreadCreated = 1;
	
	pipe(parentToChild);
	pipe(childToParent);

	pid = fork();
	if(pid < 0)
		error_handling("pipe() Error!workerFunc()");
	else if(pid == CHILD){
		close(childToParent[RDEND]);
		close(parentToChild[WREND]);
		createProcessesForWorker(0, parentToChild[RDEND] ,childToParent[WREND]);
		error_handling("createProcessesForWorker errror! child process not end.");
	}
//	waitpid(pid, &temp, 0);
	close(parentToChild[RDEND]);
	close(childToParent[WREND]);
	fcntl(childToParent[RDEND], F_SETFL, O_NONBLOCK);
	if((reader = fdopen(childToParent[RDEND], "r")) == NULL)
		error_handling("fdopen error!");
	if((writer = fdopen(parentToChild[WREND], "w")) == NULL)
		error_handling("fdopen error!");

	while(1){
		ptr = que_pop(&inputQue);
		if(ptr != NULL){
			write(parentToChild[WREND], ptr, strlen(ptr));
//			fsync(parentToChild[WREND]);
//			printf("ptr = %s\n",ptr);
		}
		else if(ptr == NULL && run == 0){
			int status;
			if(inputQue.front <= inputQue.tail)
				continue;
			close(parentToChild[WREND]);
			wait(&status);
//			printf("%d %d wait done!\n", inputQue.front, inputQue.tail);
			break;
		}
		if((readLen = read(childToParent[RDEND], staticBuf, MAX_SINGLE_INPUT)) > 0){
			buf = (char*)calloc(1, sizeof(char) * MAX_SINGLE_INPUT);
			strcpy(buf, staticBuf);
			que_push(&outputQue, buf);

//			printf("buf = %s\n",buf);
//			fflush(stdout);
		}

	}

	fcntl(childToParent[RDEND], F_SETFL, !O_NONBLOCK);
	while(read(childToParent[RDEND], staticBuf, MAX_SINGLE_INPUT) > 0){
		buf = (char*)calloc(1, sizeof(char) * MAX_SINGLE_INPUT);
		strcpy(buf, staticBuf);
//		printf("last : %s\n",buf);
		que_push(&outputQue, buf);
	}
	
//	printf("workerFunc Done!\n");
	return NULL;
}

void parseWorkerThreadProcessArgments(char * srcArgment){
	int i,j,k;
	int srcIdx;
	int isSingleQuoted = 0;
	numOfThreadArgments = 1;

	threadArgvs = (char***)calloc(1, sizeof(char**)*GENERAL_INTEGER);

	for(i = 0;i < GENERAL_INTEGER; i++){
		threadArgvs[i] = (char**)calloc(1, sizeof(char*)*GENERAL_INTEGER);
		for(j = 0; j < GENERAL_INTEGER; j++){
			threadArgvs[i][j] = (char*)calloc(1, sizeof(char)*GENERAL_INTEGER);
		}
	}

	threadArgcs[0] = 1;	
	for(srcIdx = 0, i =0 ,j = 0, k = 0; ; srcIdx++){
//		printf("%c\n",srcArgment[srcIdx]);
		if(srcArgment[srcIdx] == '\0'){
//			printf("end argv : %s",threadArgvs[i][0]);
			threadArgvs[i][j][k] = 0;
			threadArgvs[i][j + 1]= NULL;
			break;
		}
		else if(srcArgment[srcIdx] == '-' && srcArgment[srcIdx + 1] == '>'){
			srcIdx++;
			numOfThreadArgments++;
			threadArgvs[i][j][k] = 0;
			threadArgvs[i][j] = NULL;
			i++; j = 0; k = 0;
			threadArgcs[i] = 1;
			if(srcArgment[srcIdx+1] == ' ')
				srcIdx++;	
		}else if(srcArgment[srcIdx] == ' ' && isSingleQuoted == 0){
			j++; k =0;
			threadArgcs[i]++;
		}else if(srcArgment[srcIdx] == ' ' && isSingleQuoted == 1){
			threadArgvs[i][j][k++] = srcArgment[srcIdx];
		}else if(srcArgment[srcIdx] == '\'' && isSingleQuoted == 0){
//			threadArgvs[i][j][k++] = srcArgment[srcIdx];
			isSingleQuoted = 1;
		}else if(srcArgment[srcIdx] == '\'' && isSingleQuoted == 1){
//			threadArgvs[i][j][k++] = srcArgment[srcIdx];
			isSingleQuoted = 0;
		}else{
			threadArgvs[i][j][k++] = srcArgment[srcIdx];
		}
	}


/*	for(i = 0; i <numOfThreadArgments; i++){
		if(strcmp(threadArgvs[i][0], "grep") == 0){
//			printf("let's strcpy i : %d threadArgcs[i] : %d\n", i, threadArgcs[i]);
			threadArgvs[i][threadArgcs[i]] = (char*)calloc(1, sizeof(char)* GENERAL_INTEGER);
			strcpy(threadArgvs[i][threadArgcs[i]++], "--line-buffered");
			threadArgvs[i][threadArgcs[i]] = NULL;
		}
	}
*/
}

void testFunction_showThreadArgments(){
/*	int i, j;
	for(i = 0;i < numOfThreadArgments;i++){
		printf("%dth argc : %d\n",i,threadArgcs[i]);
		for(j = 0; j<= threadArgcs[i]; j++){
			printf("%d: %s\n",j,threadArgvs[i][j]);
		}
	}*/
}

void argmentErrorHandle(int argc, char * argv[]){
	if(strcmp(argv[1], "-n") != 0)
		error_handling("-n missing!");
	if(atoi(argv[2]) <= 0)
		error_handling("argment n must be positive integer");
}

void notifyNumOfWorkerThreads(char * argv[]){
	numOfWorkerThreads = atoi(argv[2]);
}

void waitForThreadCreation(){
	while(!isthreadCreated) 
		;

	isthreadCreated = 0;
}

void mainLoop_pushToQue(char * string){
	char * ptr;
	char * pusher;
	ptr = strtok(string, "\n");
	pusher = (char*)calloc(1, sizeof(char) * (strlen(ptr) + 1));
	strcpy(pusher, ptr);
	pusher[strlen(pusher)] = '\n';
	pusher[strlen(pusher)] = 0;
/*	sleep(1);
	printf("pusher: %s ",ptr);
	fflush(stdout);*/

//	printf("%s",ptr);
//	printf("asd\n");
	que_push(&inputQue, pusher);

	while((ptr = strtok(NULL, "\n"))){
		pusher = (char*)calloc(1, sizeof(char) * (strlen(ptr) + 1));
		strcpy(pusher, ptr);
//		printf("pusher: %s",pusher);
		pusher[strlen(pusher)] = '\n';
		pusher[strlen(pusher)] = 0;
		que_push(&inputQue, pusher);
		//printf("%s",ptr);
	}
}

void mainLoop(){
	char* buf;
//	int readSize;

	buf = (char*)calloc(1,sizeof(char) * MAX_SINGLE_INPUT);

	while((read(STDIN_FILENO, buf, MAX_SINGLE_INPUT)) > 0){
//		printf("%s",buf);
//		printf("main read Something\n");
		mainLoop_pushToQue(buf);
//		que_push(&inputQue, buf);
		//alarm(0);
		buf = (char*)calloc(1,sizeof(char) * MAX_SINGLE_INPUT);
	}
}

void waitUntilAllThreadsAreEnd(pthread_t receiver, pthread_t workers[]){
	int status;
	int wi, ti;
	
	run  = 0;
//	signal(sigterm);

	for(wi=0, ti =0;wi<numOfWorkerThreads;wi++){
		ti = wi;
		pthread_join(workers[wi], (void**)&status);
		wi=ti;
	}
	receiverRun = 0;

	pthread_join(receiver, (void**)&status);
}

int main(int argc, char* argv[]){
	pthread_t receiver;
	pthread_t * workers;
	int i;

	pthread_mutex_init(&mutex, NULL);
	que_init(&outputQue);
	que_init(&inputQue);
	parseWorkerThreadProcessArgments(argv[argc -1]);
	testFunction_showThreadArgments();
	argmentErrorHandle(argc, argv);
	notifyNumOfWorkerThreads(argv);

	workers = (pthread_t *)calloc(1, sizeof(pthread_t) * numOfWorkerThreads);
	
	if(pthread_create(&receiver, NULL, receiverFunc, (void*)&i) < 0)
		error_handling("Receiver Thread Creating Error");
	waitForThreadCreation();

	for(i = 0; i < numOfWorkerThreads; i++){
		if(pthread_create(&(workers[i]), NULL, workerFunc, (void*)&i) < 0)
			error_handling("Worker Thread Creating Error");

		waitForThreadCreation();
	}
	mainLoop();

	waitUntilAllThreadsAreEnd(receiver, workers);
}
