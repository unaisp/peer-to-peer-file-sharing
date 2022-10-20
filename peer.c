/*
	Program : peer.c
	This program is to run on client side.
	It has a main thread to interact with user. 
	It will create seperate thread for downloading, uploading.
	Also it have another thread for accepting uploadd requests from other peers.
	
	It will create a folder 'p2p-files' and we have to copy the file to this folder.
	It also create a download folder with this folder.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>

#include <termios.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>


#define ERROR -1
#define FILE_BUF_SIZE 1500			//	100KB
#define SHARE_FOLDER "p2p-files"
#define DOWNLOAD_FOLDER "p2p-files/download"
#define LEN_HEAD_MSG 10
#define LEN_JOIN_MSG 15
#define LEN_PUBLISH_MSG 10

#define RED   "\x1B[31m"
#define GREEN   "\x1B[32m"
#define YELLOW   "\x1B[33m"
#define BLUE   "\x1B[34m"
#define MAGENTA   "\x1B[35m"
#define CYAN   "\x1B[36m"
#define WHITE   "\x1B[37m"
#define RESET "\x1B[0m"

char PRINT[200];
char COMMENT[200];
int Lines=0;
int ShowComment;

long long int UpCounter;
long long int DownCounter;

pthread_mutex_t download_mutex;
pthread_mutex_t upload_mutex;


struct  Object
{
	char *name;
	char type[5];
	long long int size;
	char *path;
	int level;
	struct Object *nextObject;
}*objectHead;

struct SearchResult
{
	char IP[20];
	int port;
	struct Object object;
}*SearchResults[50];

struct UpDown
{
	char IP[20];
	int port;
	struct Object object;

	int status;
	long long int transmitted;
	pthread_t threadID;

	struct UpDown *next;
} *downloadHead,*uploadHead;

struct ClientConnection
{
	int sockID;
	struct sockaddr_in sockAddr;
	pthread_t threadID;
};

//Mains

int ReconnectToServer(struct sockaddr_in *, int,int );
void PublishFiles(int ,int);

void* UploadListner(void* argv);
void* DownloadThread(void *);
void* UploadThread(void * argv);
long long int SendFolder(struct UpDown *,int ,char *,char *, char *,char *, int *, int );
long long int SendFile(struct UpDown *, int , char *, char *, char *, char *, int *);

//Supportings
char * ReadFromBuf(int ,int ,char *, int *);
void ReadFromBuferTo(char *, int ,int ,char *, int *);

void ClearSearchResults();
char* CreateFolder(char *,char *);
int SendHeaderMessage(int , int );
int SendData(int ,char *, int );
int ReadData(int ,char *,int );

FILE* Fopen(char *, char *);

char* MakeHeaderMsg(int lenNextMsg);
char* MakeJoinMsg(int lenNextMsg,int port);
char* MakePublishMsg(int lenNextMsg);

long long int ReadObjects(char *, int );
long long int SendObjects(int serverSockID,struct Object *,int);
char *GetFileExtention(char *filename);
void ShowDownload();
void ShowUpload();
void* EventThread(void *);

//Pritnts Strings
char* BytesToString(char * , long long int );
char* Merge3(char* ,char* ,char* );
char* Merge5(char* ,char* ,char* ,char *,char* );
void MyPrintf(int lines);
void MyComment(char,int);

void handler(int signum, siginfo_t *info, void *context)
{
  struct sigaction action = {
    .sa_handler = SIG_DFL,
    .sa_sigaction = NULL,
    .sa_mask = 0,
    .sa_flags = 0,
    .sa_restorer = NULL
  };

  fprintf(stderr, "Fault address: %p\n", info->si_addr);
  switch (info->si_code) {
  case SEGV_MAPERR:
    fprintf(stderr, "Address not mapped.\n");
    break;

  case SEGV_ACCERR:
    fprintf(stderr, "Access to this address is not allowed.\n");
    break;

  default:
    fprintf(stderr, "Unknown reason.\n");
    break;
  }

  /* unregister and let the default action occur */
  sigaction(SIGSEGV, &action, NULL);
}

struct sigaction action = {
    .sa_handler = NULL,
    .sa_sigaction = handler,
    .sa_mask = 0,
    .sa_flags = SA_SIGINFO,
    .sa_restorer = NULL
  };

void main(int argc, char *argv[] )
{
	int lenSocket,retVal,lenMsg,lenNextMsg,lenName,lenPath;
	int option,count,i,no;
	int serverSocket,uplListeningSockID;
	char *buf, query[100],type[5],temp[50];
	struct sockaddr_in serverAddr,clientSockAddr;
	struct stat st;
	struct SearchResult *sResult;
	char newOption[10],sizeString[10];
	struct UpDown *download;
	pthread_t uploadListenThreadID;
	char serverIP[20];
	int serverPort;

	system("clear");

	if(argc<2)
	{
		printf(RED"\nProvide server ip and port\n"RESET);
		printf("\n\tIP : ");
		scanf("%s",serverIP);
		printf("\tPort : ");
		scanf("%d",&serverPort);
	}
	else
	{
		strcpy(serverIP,argv[1]);
		serverPort=atoi(argv[2]);
	}


	char Key;

	if (sigaction(SIGSEGV, &action, NULL) < 0)
	{
    	perror("sigaction");
  	}

	lenSocket=sizeof(struct sockaddr_in);

	serverAddr.sin_family=AF_INET;
	serverAddr.sin_port=htons(serverPort);
	serverAddr.sin_addr.s_addr=inet_addr(serverIP);
	memset(&serverAddr.sin_zero,0,sizeof(serverAddr.sin_zero));

	clientSockAddr.sin_family=AF_INET;
	clientSockAddr.sin_port=0;
	clientSockAddr.sin_addr.s_addr=INADDR_ANY;
	memset(&clientSockAddr.sin_zero,0,sizeof(clientSockAddr.sin_zero));

	uplListeningSockID=socket(AF_INET,SOCK_STREAM,0);

	if(bind(uplListeningSockID,(struct sockaddr *)&clientSockAddr,lenSocket)==ERROR)
		printf("Binding failed");

	if (getsockname(uplListeningSockID, (struct sockaddr *)&clientSockAddr, &lenSocket) == -1)
	{
    	perror("getsockname");
    	return;
	}

	//Socket Creations
	if((serverSocket=socket(AF_INET,SOCK_STREAM,0))==ERROR)
	{
		perror(RED"Socket creation failed"RESET);
		//exit(-1);
	}	

	//Connecting to server
	if(connect(serverSocket,(struct sockaddr *)&serverAddr,lenSocket)==ERROR)
	{
		perror(RED"Connection to server failed"RESET);
		//exit(-1);
	}
	printf(GREEN"\nConnected to server\n"RESET);

	if(stat(SHARE_FOLDER,&st)==-1)
	{
		mkdir(SHARE_FOLDER,0700);
	}

	//Client listetning
	//uplListeningSockID=socket(AF_INET,SOCK)

	//Sending Head Message
	buf=MakeHeaderMsg(LEN_JOIN_MSG);	//Next msg=join
	retVal=SendData(serverSocket,buf,LEN_HEAD_MSG);
	if(retVal==ERROR)
	{
		perror("Sending header message failed\n");
		//exit(-1);
	}
	
	//Sending Join Msg
	buf=MakeJoinMsg(LEN_PUBLISH_MSG,ntohs(clientSockAddr.sin_port));				//Next Msg=Publish
	retVal=SendData(serverSocket,buf,LEN_JOIN_MSG);
	if(retVal==ERROR)
	{
		perror("Sending header message failed\n");
		//exit(-1);
	}
	free(buf);
	
	//Publishing Files
	PublishFiles(serverSocket,ntohs(clientSockAddr.sin_port));

	close(serverSocket);
	serverSocket=0;
	printf(GREEN"Connection to server closed\n"RESET);

	fflush(stdout);

	//Starting listner for file upload request
	if(pthread_create(&uploadListenThreadID,NULL,UploadListner,&uplListeningSockID)!=0)
		perror("Upload listener thread failed\n");

	printf(YELLOW"\n-------Peer Listening Port : %d--------"RESET,ntohs(clientSockAddr.sin_port));

	while(1)
	{
		Main:
		printf("\n[P] : Publish Again\t\t[S] : Search\t\t\n");
		printf("[D] : Show Downloads\t\t[U] : Show Uploads \t\t[Q] : Quit\n");

		strcpy(PRINT,YELLOW"Enter the option : "RESET);
		MyPrintf(1);
		scanf("%s",newOption);
		MyPrintf(0);

		//system("clear");

		if(strcmp(newOption,"P")==0 || strcmp(newOption,"p")==0)
		{
			serverSocket=ReconnectToServer(&serverAddr,LEN_PUBLISH_MSG,ntohs(clientSockAddr.sin_port));
			if(serverSocket==ERROR)
			{
				printf("Publishing cancelled\n");
				continue;
			}
			PublishFiles(serverSocket,45);
			close(serverSocket);
			serverSocket=0;
			fflush(stdout);
			printf(GREEN"Connection to server disconnected\n"RESET);
		}
		if(strcmp(newOption,"S")==0 || strcmp(newOption,"s")==0)
		{
			while(1)
			{
				Search:
				strcpy(PRINT,YELLOW"\nEnter the file/folder name : "RESET);
				MyPrintf(1);
				//fgets(query,100,stdin);
				scanf(" %[^\n]s",query);
				MyPrintf(0);

				lenMsg=5+5+strlen(query)+1;

				if(serverSocket==0)		//
				{
					//Reconnect to server
					serverSocket=ReconnectToServer(&serverAddr,lenMsg,ntohs(clientSockAddr.sin_port));//// Next msg is unknown now. So send header msg;
					if(serverSocket==ERROR)
					{
						printf("\nSearching cancelled");
						continue;
					}
				}
				else
				{
					//Send Header Msg
					if(SendHeaderMessage(serverSocket,lenMsg)==ERROR)
					{
						printf("\nSearching cancelled\n");
						continue;
					}
				}

				buf=(char *) malloc(lenMsg);
				strcpy(buf,"srch");
				sprintf(buf+5,"%d",LEN_HEAD_MSG);
				strcpy(buf+10,query);

				retVal=SendData(serverSocket,buf,lenMsg);
				if(retVal==ERROR)
				{
					printf("Searching cancelled\n");
					continue;
				}
				free(buf);

				//Search Result-
				count=0;
				lenNextMsg=LEN_HEAD_MSG;
				while(lenNextMsg>0 && count<100)
				{
					//printf("\nWaiting for %d bytes",lenNextMsg);
					buf=(char *)malloc(lenNextMsg);
					retVal=ReadData(serverSocket,buf,lenNextMsg);
					if(retVal==ERROR)
					{
						printf("\n Search Failed");
						break;
					}
					strcpy(type,buf);
					//printf("\nOne message arrived. len Msg : %d. Type : %s",lenNextMsg,type);

					if(strcmp(type,"head")==0)
					{
						lenNextMsg=atoi(buf+5);
						//printf("\nHead Msg arrived. Len Next Msg : %d",lenNextMsg);

					}
					else if(strcmp(type,"rslt")==0)
					{
						//		[0-4] head 		[5-9] Length of next Msg  	[10-29]   IP
						//		[30-34]	port 	[35-39] Obj type 			[40-54]   Size of file
						//		[55--] filename   // file path
						lenNextMsg=atoi(buf+5);
						lenName=strlen(buf+55)+1;
						lenPath=strlen(buf+55+lenName)+1;

						if(count==0)
						{
							printf("\n\033[4m%104s\033[24m","");
							printf("\n%5s %-30s %-6s %-10s %-25s %-30s","No","Name","Type","Size","Peer","Path");
							printf("\n\033[4m%104s\033[24m","");
						}

						sResult=(struct SearchResult*)malloc(sizeof(struct SearchResult));

						strcpy(sResult->IP,buf+10);
						sResult->port=atoi(buf+30);
						strcpy(sResult->object.type,buf+35);
						sResult->object.size=atoi(buf+40);

						(sResult->object).name=(char *)malloc(lenName);
						(sResult->object).path=(char *)malloc(lenPath);

						strcpy((sResult->object).name,buf+55);
						strcpy((sResult->object).path,buf+55+lenName);

						SearchResults[count]=sResult;

						sprintf(temp,"%s:%d",SearchResults[count]->IP,SearchResults[count]->port);
						printf("\n%5d %-30s %-6s %-10s %-25s %-30s",
							count+1,
							SearchResults[count]->object.name,
							SearchResults[count]->object.type,
							BytesToString(sizeString,SearchResults[count]->object.size),
							temp,
							SearchResults[count]->object.path);

						count++;

					}
					free(buf);
				}

				if(count==0)
				{
					printf(YELLOW"\n\n No result found"RESET);
					printf("\n[S] : Search Again\t\t[B] : Back");
				}
				else
				{
					printf("\n\n[#] : Enter file no to download\t\t[S] : Search Again\t\t[B] : Back");
				}

				while(1)
				{
					SelectDownloadNo:
					strcpy(PRINT,YELLOW"\nEnter the option : "RESET);
					MyPrintf(1);
					scanf("%s",newOption);
					MyPrintf(0);

					if(strcmp(newOption,"S")==0 || strcmp(newOption,"s")==0)
					{
						ClearSearchResults(count);
						goto Search;
					}
					else if(strcmp(newOption,"B")==0 || strcmp(newOption,"b")==0)
					{
						ClearSearchResults(count);
						close(serverSocket);
						serverSocket=0;
						printf(GREEN"\nConection to server disconnected"RESET);
						goto Main;
					}
					else
					{
						//Download
						no=atoi(newOption);
						if(no>count || no<=0)
						{
							printf("\n Invalid file no. Try Again.");
							goto SelectDownloadNo;
						}
						download=(struct UpDown *)malloc(sizeof(struct UpDown));
						memcpy(&(download->object),&(SearchResults[no-1]->object),sizeof(struct Object));
						download->object.name=(char *)malloc(strlen(SearchResults[no-1]->object.name)+1);
						download->object.path=(char *)malloc(strlen(SearchResults[no-1]->object.path)+1);

						strcpy(download->object.name,SearchResults[no-1]->object.name);
						strcpy(download->object.path,SearchResults[no-1]->object.path);

						strcpy(download->IP,SearchResults[no-1]->IP);
						download->port=SearchResults[no-1]->port;

						download->status=-1;		//Not Started

						pthread_mutex_lock(&download_mutex);
						download->next=downloadHead;
						downloadHead=download;
						pthread_mutex_unlock(&download_mutex);

						if(pthread_create(&(download->threadID),NULL,DownloadThread,download)!=0)
							perror("\nDownload thread creation failed");
					}
				}
				ClearSearchResults(count);

			}
		}
		else if(strcmp(newOption,"Q")==0 || strcmp(newOption,"q")==0)
		{
			exit(-1);
		}
		else if(strcmp(newOption,"D")==0|| strcmp(newOption,"d")==0)
		{
			ShowDownload();
		}
		else if(strcmp(newOption,"U")==0|| strcmp(newOption,"u")==0)
		{
			ShowUpload();
		}
	}
}

//This thread is used to listen the upload requests.
void* UploadListner(void* argv)
{
	int *mySockID,remoteSockID;
	int lenSock;
	struct ClientConnection *uploadCon;
	struct sockaddr_in remoteSockAddr;
	char msg[25]="Welcome to test";

	mySockID=(int *)argv;
	lenSock=sizeof(struct sockaddr_in);

	if(listen(*mySockID,5)==ERROR)
		perror("Listening failed");

	while(1)
	{
		uploadCon=(struct ClientConnection*)malloc(sizeof(struct ClientConnection));
		if((uploadCon->sockID=accept(*mySockID,(struct sockaddr *)&(uploadCon->sockAddr),&lenSock))==ERROR)
			perror("\nConnection accepting failed");
		else
			if(pthread_create(&(uploadCon->threadID),NULL,UploadThread,uploadCon)!=0)
				perror("\nUpload thread creation failed");
	}
	close(*mySockID);
}

//This thread is used to upload files/folders
void* UploadThread(void * argv)
{
	int i;
	char IP[20];
	struct ClientConnection *con;
	int remBuf,lenEnd,lenNextMsg,retVal;
	int lenName,lenPath,remaining;
	char buf[FILE_BUF_SIZE], *end,type[LEN_HEAD_MSG];
	struct UpDown *upload;
	long long int minValue;

	con=(struct ClientConnection *)argv;
	strcpy(IP,inet_ntoa(con->sockAddr.sin_addr));

	ShowComment=1;

	lenNextMsg=LEN_HEAD_MSG;
	while(lenNextMsg>0)
	{
		retVal=ReadData(con->sockID,buf,lenNextMsg);

		if(retVal==ERROR)
		{
			printf(RED"\n[Uploader:%s]Data reading erro"RESET,IP);
			close(con->sockID);
			free(con);
			break;
		}
		if(retVal==0) // client disconnected
		{
			close(con->sockID);
			free(con);
			break;
		}

		memcpy(type,buf,LEN_HEAD_MSG);

		if(strcmp(type,"prob")==0)
		{
			//Server Probing		
			close(con->sockID);
			free(con);			
			break;

		}
		else if(strcmp(type,"head")==0)			//	First Packet
		{
			lenNextMsg=atoi(buf+5);
		}
		else if(strcmp(type,"objt")==0)				//Upload Request
		{
			//Request=head+LenNextMsgSize+type+Name+Path
			lenNextMsg=atoi(buf+5);
			lenName=strlen(buf+30)+1;
			lenPath=strlen(buf+30+lenName)+1;

			upload = (struct UpDown *)malloc(sizeof(struct UpDown));
			upload->object.name=(char *)malloc(lenName);
			upload->object.path=(char *) malloc(lenPath);
			
			strcpy(upload->IP,IP);
			upload->port=ntohs(con->sockAddr.sin_port);
			upload->status=-1;			//	not started
			upload->transmitted=0;
			//upload->thread=con->threadID;

			strcpy(upload->object.type,buf+10);
			upload->object.size=atol(buf+15);
			strcpy(upload->object.name,buf+30);
			strcpy(upload->object.path,buf+30+lenName);

			if(ShowComment)
				printf(BLUE"\n[Upload:%s]Started\n"RESET,upload->object.name);

			pthread_mutex_lock(&upload_mutex);
			upload->next=uploadHead;
			uploadHead=upload;
			pthread_mutex_unlock(&upload_mutex);
		
			UpCounter=0;
			upload->status=0;
			remBuf=FILE_BUF_SIZE;
			if(strcmp(upload->object.type,"dir")==0)
			{
				upload->object.size=SendFolder(upload,con->sockID,upload->object.path,upload->object.name,"",buf, &remBuf,1);
			}
			else
			{
				upload->object.size=SendFile(upload, con->sockID, upload->object.path,upload->object.name,"",buf, &remBuf);
			}

			lenEnd=5;
			end=(char *)malloc(lenEnd);
			strcpy(end,"end");

			remaining=lenEnd;
			while(remaining>0)
			{
				minValue=remBuf<remaining?remBuf:remaining;
				if(minValue>0)
				{
					memcpy(buf+FILE_BUF_SIZE-remBuf,end+(lenEnd-remaining),minValue);
					remBuf-=minValue;
					remaining-=minValue;
				}

				if(remBuf==0)	//Buffer is full. Send to destination
				{
					retVal=SendData(con->sockID,buf,FILE_BUF_SIZE);
					if(retVal==ERROR)
						perror("File Send error");
					remBuf=FILE_BUF_SIZE;
				}
			}
			if(remBuf!=FILE_BUF_SIZE)
			{
				//Padd zero at the end. Send
				retVal=SendData(con->sockID,buf,FILE_BUF_SIZE);
				if(retVal==ERROR)
						perror("File Send error");
				remBuf=FILE_BUF_SIZE;

			}
			free(end);
			if(ShowComment)
				printf(BLUE"\n[Upload:%s]Completed\n"RESET,upload->object.name);
			break;
		}
	}
}

//It is the download thread.
void* DownloadThread(void * argv)
{
	int sockID,i,writedToFile;
	int retVal,lenSock,lenName,lenPath,lenObjtMsg,remBuf,lenFolderName,lenDestPath,lenFileName;
	long long int fileSize,remainingtToWrite,minValue;
	struct sockaddr_in remotePeerAddr;
	struct UpDown *download;
	char buf[FILE_BUF_SIZE],*tempBuf;
	char *downloadPath, *objtMsg, *parentFolder,*folderName,*destPath,*newFolderName,*fileName, *localPath;
	char temp[20],*URL;
	char head[5];
	struct stat st;
	FILE *fpr;
	float percentage;

	lenSock=sizeof(struct sockaddr_in);
	download=(struct UpDown *)argv;
	ShowComment=1;

	if((sockID=socket(AF_INET,SOCK_STREAM,0))==ERROR)
		perror("Downloader:Socket creation Failed");

	//Server Address initilization
	remotePeerAddr.sin_family=AF_INET;
	remotePeerAddr.sin_port=htons(download->port);
	remotePeerAddr.sin_addr.s_addr=inet_addr(download->IP);
	memset(remotePeerAddr.sin_zero,0,sizeof(remotePeerAddr.sin_zero));

	if(connect(sockID,(struct sockaddr *)&remotePeerAddr,lenSock)==ERROR)
		perror("Download:Connection to remote client failed");

	if(ShowComment)
		printf(CYAN"\n[Download]Connected to remote peer. Remote IP=%s. Port=%d.\n"RESET,download->IP,download->port);

	//Request=head+LenNextMsgSize+type+Name+Path
	lenName=strlen(download->object.name)+1;
	lenPath=strlen(download->object.path)+1;

	lenObjtMsg=5+5+5+15+lenName+lenPath;
	objtMsg=(char *)malloc(lenObjtMsg);

	strcpy(objtMsg,"objt");			//	head
	strcpy(objtMsg+5,"0");			//	Lenth of next message
	strcpy(objtMsg+10,download->object.type);			//Message Type
	sprintf(objtMsg+15,"%lld",download->object.size);			//File Size
	strcpy(objtMsg+30,download->object.name);			//file name
	strcpy(objtMsg+30+lenName,download->object.path);	//file path

	//Sending Header Message
	tempBuf=MakeHeaderMsg(lenObjtMsg);	//Next msg = Download Request message
	retVal=SendData(sockID,tempBuf,LEN_HEAD_MSG);
	if(retVal==ERROR)
	{
		perror("Sending header message failed\n");
		goto end;
	}
	free(tempBuf);

	//Sending file name path to peer
	retVal=SendData(sockID,objtMsg,lenObjtMsg);
	if(retVal==ERROR)
	{
		perror("Sending downloading request failed\n");
		goto end;
	}
	
	parentFolder=(char *)malloc(strlen(DOWNLOAD_FOLDER)+1);
	strcpy(parentFolder,DOWNLOAD_FOLDER);

	if(stat(DOWNLOAD_FOLDER,&st)==-1)
		mkdir(DOWNLOAD_FOLDER,0700);	


	//Downloading files
	printf("\n");
	download->status=0;				//Started
	download->transmitted=0;
	remBuf=0;
	percentage=0;
	while(1)
	{
		//Reading Head
		ReadFromBuferTo(head,sockID,5,buf,&remBuf);

		if(strcmp(head,"fldM")==0)		//	Main Folder
		{
			//Read Length of folder Name
			ReadFromBuferTo(temp,sockID,5,buf,&remBuf);		//length of folder name
			lenFolderName=atoi(temp);

			ReadFromBuferTo(temp,sockID,5,buf,&remBuf);		//length of folder path
			lenDestPath=atoi(temp);

			folderName=ReadFromBuf(sockID,lenFolderName,buf,&remBuf);  // name
			destPath=ReadFromBuf(sockID,lenDestPath,buf,&remBuf);	     //path

			free(parentFolder);
			newFolderName=CreateFolder(DOWNLOAD_FOLDER,folderName); //Eg : downlaod/A3
			parentFolder=Merge3(DOWNLOAD_FOLDER,"/",newFolderName);	//  napster/download/A3

			if(ShowComment)
				printf(CYAN"\n[Download:%s] %s\n "RESET,download->object.name,parentFolder);

			free(folderName);
			free(destPath);
			free(newFolderName);
		}
		else if(strcmp(head,"fldr")==0)		//Normal Folder
		{
			ReadFromBuferTo(temp,sockID,5,buf,&remBuf);		//length of folder name
			lenFolderName=atoi(temp);

			ReadFromBuferTo(temp,sockID,5,buf,&remBuf);		//length of folder path
			lenDestPath=atoi(temp);

			folderName=ReadFromBuf(sockID,lenFolderName,buf,&remBuf);  // name
			destPath=ReadFromBuf(sockID,lenDestPath,buf,&remBuf);	     //path

			//URL=parentFolder/destPath/folderName
			if(lenDestPath==1)	//No destination pth
				URL=Merge3(parentFolder,"/",folderName);
			else
				URL=Merge5(parentFolder,"/",destPath,"/",folderName);

			mkdir(URL,0700);
			if(ShowComment)
				printf(CYAN"\r[Download:%s] %5.2f%% %s "RESET,download->object.name,percentage,parentFolder);
			free(folderName);
			free(destPath);
			free(URL);
		}
		else if(strcmp(head,"file")==0)
		{
			ReadFromBuferTo(temp,sockID,5,buf,&remBuf);		//length of file  name
			lenFileName=atoi(temp);

			ReadFromBuferTo(temp,sockID,5,buf,&remBuf);		//length of destination  name
			lenDestPath=atoi(temp);

			ReadFromBuferTo(temp,sockID,15,buf,&remBuf);		//File Size
			fileSize=atol(temp);

			fileName=ReadFromBuf(sockID,lenFileName,buf,&remBuf);  // name
			destPath=ReadFromBuf(sockID,lenDestPath,buf,&remBuf);	     //path

			if(lenDestPath==1)	//No destination pth. Download to napster/download
				fpr=Fopen(parentFolder,fileName);
			else
			{
				localPath=Merge3(parentFolder,"/",destPath);//fileName
				fpr=Fopen(localPath,fileName);
			}
			
			if(fpr==NULL)
			{
				printf("[Downloader]%s can't open for write\n",fileName);
			}

			if(fileSize==0)
			{
				percentage=((float)(download->transmitted*100))/((float)download->object.size);
				if(ShowComment)
					printf(CYAN"\r[Download:%s] %5.2f%% %s/%s/%s  "RESET,download->object.name,percentage,parentFolder,destPath,fileName);
			}

			remainingtToWrite=fileSize;
			while(remainingtToWrite>0)
			{
				writedToFile=0;
				minValue=remainingtToWrite<remBuf?remainingtToWrite:remBuf;

				if(minValue>0)		//Buffer has excess data
				{
					writedToFile=fwrite(buf+FILE_BUF_SIZE-remBuf,1,minValue,fpr);
					if(writedToFile<0)
					{
						perror("\nWriting to file") ;
					}
					//remBuf-=writedToFile;
					//remainingtToWrite-=writedToFile;
					remBuf-=minValue;
					remainingtToWrite-=minValue;					
				}
				download->transmitted+=minValue;

				if(remBuf==0)
				{
					ReadData(sockID,buf,FILE_BUF_SIZE);
					remBuf=FILE_BUF_SIZE;
				}
				percentage=((float)(download->transmitted*100))/((float)download->object.size);
				if(ShowComment)
					printf(CYAN"\r[Download:%s] %5.2f%% %s/%s/%s"RESET,download->object.name,percentage,parentFolder,destPath,fileName);
			}
			fclose(fpr);
		}
		else if(strcmp(head,"end")==0)
		{
			printf(CYAN"\n[Download:%s]Downloading Completed\n"RESET,download->object.name);
			download->object.size=download->transmitted;
			download->status=1;
			break;
		}
	}
	end:
	free(parentFolder);
}

void ShowDownload()
{
	int i=0;
	struct UpDown *download;
	char size[10];
	char status[20];
	pthread_t threadID;	
	float fileSize;
	pthread_t eventThreadID;
	char exitKey[10];

	ShowComment=0;
	
	system("clear");
	pthread_create(&eventThreadID,NULL,EventThread,exitKey);

	while(1)
	{			
		pthread_mutex_lock(&download_mutex);
		download=downloadHead;
		pthread_mutex_unlock(&download_mutex);
		
		printf("\n\nDownloads\n----------");
		printf("\n%3s %-30s %-20s %-10s %-20s","No","Name","Source","Size","Status");	
		i=0;
		while(download)
		{			
			if(download->status==-1)
				strcpy(status,"Waiting");
			else if(download->status==0)
				sprintf(status,"%5.2f%%",((float)(download->transmitted*100))/((float)download->object.size));
			else if(download->status==1)
				strcpy(status,"Completed");
			
			printf("\n%3d %-30s %-20s %-10s %-20s",
				++i,
				download->object.name,
				download->IP,
				BytesToString(size,download->object.size),
				status);
				
			download=download->next;	
		}
		printf("\n\n\n\nEnter [B] to go back : \n");
		sleep(1);
		if(strcmp(exitKey,"b")==0 || strcmp(exitKey,"B")==0)
		{
			break;
		}
		system("clear");		
	}	

	ShowComment=1;			
}
void ShowUpload()
{
	int i=0;
	struct UpDown *upload;
	char size[10];
	char status[20];
	pthread_t threadID;	
	float fileSize;
	pthread_t eventThreadID;
	char exitKey[10];
	fd_set rfds;
    struct timeval tv;
    int retval, len;
    char buff[10] = {0};

	ShowComment=0;
	
	system("clear");
	pthread_create(&eventThreadID,NULL,EventThread,exitKey);

	while(1)
	{			
		pthread_mutex_lock(&upload_mutex);
		upload=uploadHead;
		pthread_mutex_unlock(&upload_mutex);
		
		printf("\nUploads\n----------");
		printf("\n%3s %-30s %-20s %-10s %-20s","No","Name","Destination","Size","Status");	
		i=0;
		while(upload)
		{			
			if(upload->status==-1)
				strcpy(status,"Waiting");
			else if(upload->status==0)
				sprintf(status,"%5.2f%%",((float)(upload->transmitted*100))/((float)upload->object.size));
			else if(upload->status==1)
				strcpy(status,"Completed");
			
			printf("\n%3d %-30s %-20s %-10s %-20s",
				++i,
				upload->object.name,
				upload->IP,
				BytesToString(size,upload->object.size),
				status);
				
			upload=upload->next;	
		}
		printf("\n\n\n\nEnter [B] to go back : \n");
		
		sleep(1);
		if(strcmp(exitKey,"b")==0 || strcmp(exitKey,"B")==0)
		{
			break;
		}
		
		/*FD_ZERO(&rfds);
    	FD_SET(0, &rfds);

    	tv.tv_sec = 2;
    	tv.tv_usec = 0;

    	retval = select(1, &rfds, NULL, NULL, &tv);

	    if (retval == -1){
	        perror("select()");
	  		return;
	    }
	    else if (retval)
	    {
	        fgets(buff, sizeof(buff), stdin);
	        if(strlen(buff)>0 && ( buff[0]=='B' || buff[0]=='b'))
	        {
	        	break;
	        }
	    }*/

		system("clear");		
	}	

	ShowComment=1;			
}

char * BytesToString(char * dest, long long int bytes)
{
	float size=(float)bytes;

	if(size<1024)
		sprintf(dest,"%-lldB",bytes);
	else if((size/=1024)<1024)
		sprintf(dest,"%-.2fKB",size);
	else if((size/=1024)<1024)
		sprintf(dest,"%-.2fMB",size);
	else if((size/=1024)<1024)
		sprintf(dest,"%-.2fGB",size);
	else 
		sprintf(dest,"%-.2fTB",size/1024);

	dest[9]='\0';
	return dest;
}

char * ReadFromBuf(int sockID,int toRead,char *buf, int *remBuf)
{
	int remaining;
	char* tempBuf;

	tempBuf=(char *)malloc(toRead);

	ReadFromBuferTo(tempBuf,sockID,toRead,buf,remBuf);

	return tempBuf;
}

void ReadFromBuferTo(char *tempBuf, int sockID,int toRead,char *buf, int *remBuf)
{
	int remaining;
	int min,i; 

	remaining=toRead;
	//tempBuf=(char *)malloc(toRead);

	while(remaining>0)
	{
		min=(*remBuf<remaining)?*remBuf:remaining;
		if(min>0)
		{
			memcpy(tempBuf+toRead-remaining,buf+FILE_BUF_SIZE-*remBuf,min);
			//strcpy(tempBuf+toRead-remaining,buf+FILE_BUF_SIZE-*remBuf);
			*remBuf-=min;
			remaining-=min;
		}

		if(*remBuf==0)
		{
			ReadData(sockID,buf,FILE_BUF_SIZE);
			*remBuf=FILE_BUF_SIZE;
		}
	}
}

int ReconnectToServer(struct sockaddr_in *serverAddr, int nextMessageSize,int port)
{
	int retVal;
	int serverSocket;
	char *buf;

	if((serverSocket=socket(AF_INET,SOCK_STREAM,0))==ERROR)
	{
		perror(RED"Socket creation failed"RESET);
		return ERROR;
	}

	if(connect(serverSocket,(struct sockaddr *)serverAddr,sizeof(struct sockaddr_in))==ERROR)
	{
		perror(RED"Connection to server failed"RESET);
		close(serverSocket);
		return ERROR;
	}

	buf=MakeHeaderMsg(LEN_JOIN_MSG);					//Next msg=join
	retVal=SendData(serverSocket,buf,LEN_HEAD_MSG);
	if(retVal==ERROR)
	{
		perror("Sending header message failed");
		close(serverSocket);
		return ERROR;
	}

	// Send reconnnect message
	buf=(char *)malloc(nextMessageSize);
	strcpy(buf,"recn");
	sprintf(buf+5,"%d",nextMessageSize);
	sprintf(buf+10,"%d",port);
	retVal=SendData(serverSocket,buf,LEN_JOIN_MSG);
	if(retVal==ERROR)
	{
		perror("Sending header message failed");
		close(serverSocket);
		return ERROR;
	}
	printf(GREEN"\nConnected to server"RESET);
	return serverSocket;
}

void PublishFiles(int serverSockID,int peerListeningPort)
{
	int retVal;
	char *buf;
	struct Object *napster;
	long long int count;

	objectHead=NULL;

	napster=(struct Object*)malloc(sizeof(struct Object));
	napster->name=(char *)malloc(strlen(SHARE_FOLDER)+1);
	napster->path=(char *)malloc(1);

	strcpy(napster->name,SHARE_FOLDER);
	strcpy(napster->path,"");			//Parent Folder
	strcpy(napster->type,"dir");
	napster->level=0;

	napster->nextObject=objectHead;
	objectHead=napster;

	printf(GREEN"\n[Publish]:Started"RESET);
	printf(GREEN"\n[Publish]:Reading files"RESET);
	napster->size= ReadObjects(SHARE_FOLDER,1);//----------------------------------READING
	//printf(GREEN"\n[Publish]:Reading completd"RESET);

	//Sending publish message
	buf=MakePublishMsg(LEN_HEAD_MSG);						//We don't What's the size of file file to publish
	retVal=SendData(serverSockID,buf,LEN_PUBLISH_MSG);
	if(retVal==ERROR)
	{
		perror("Sending publishing message failed");
	}
	free(buf);

	printf(GREEN"\n[Publish]:Sending files\n"RESET);
	count=SendObjects(serverSockID, objectHead,LEN_HEAD_MSG);//----------------------------------SENDING

	objectHead=NULL;
	printf(GREEN"\n[Publish]:Completed. Toltal %lld files."RESET,count);
}

//------------------------Supporting Fns--------------------------------------------------------
long long int SendFolder(struct UpDown *upload,int socket,char *folderPath,char *folderName, char *destPath,char *buf, int *remBuf, int topLevel)
{
	int lenFolderName, lenDestPath;
	int lenHead,remaining;
	char *URL;
	char *head;		//Head = type(5) + nameSize(V) + pathSize(V) +  name + destPath +;
	char *newDestPath;
	struct dirent *dir;
	long long int folderSize=0,minValue;
	DIR *dpr;


	lenFolderName=strlen(folderName)+1;
	lenDestPath=strlen(destPath)+1;

	if(strlen(folderPath)==0)			//file path is null
	{
		URL=(char *)malloc(lenFolderName);
		strcpy(URL,folderName);
	}
	else
	{	URL=Merge3(folderPath,"/",folderName);
	}

	dpr = opendir(URL);
	if(dpr)
	{
		lenFolderName=strlen(folderName)+1;
		lenDestPath=strlen(destPath)+1;
		lenHead=5+5+5+lenFolderName+lenDestPath;

		head=(char *)malloc(lenHead);

		strcpy(head,(topLevel==1)?"fldM":"fldr");		//	Head[0-4]
		sprintf(head+5,"%d",lenFolderName);				//	Head[5-9]
		sprintf(head+10,"%d",lenDestPath);				//	Head[10-14]
		strcpy(head+15,folderName);						//	Head[15-----]
		strcpy(head+15+lenFolderName,topLevel>=0?"":destPath);

		remaining=lenHead;
		while(remaining>0)
		{
			minValue=*remBuf<remaining?*remBuf:remaining;
			if(minValue>0)
			{
				memcpy(buf+FILE_BUF_SIZE-*remBuf,head+(lenHead-remaining),minValue);
				*remBuf-=minValue;
				remaining-=minValue;
			}			

			if(*remBuf==0)
			{
				if(SendData(socket,buf,FILE_BUF_SIZE)==ERROR)
					perror("File Send error");
				*remBuf=FILE_BUF_SIZE;
			}
		}
		free(head);
		topLevel--;

		newDestPath=(char *)malloc(lenDestPath+lenFolderName);

		if(topLevel>=0)
			strcpy(newDestPath,"");
		else
		{
			if(lenDestPath==1)
				strcpy(newDestPath,folderName);
			else
				newDestPath=Merge3(destPath,"/",folderName);
		}

		//sprintf(COMMENT,"[Uploader:%s-%s]%ld:%s",upload->IP,upload->object.name,++UpCounter,URL);
		//MyComment('U',1);

		while ((dir = readdir(dpr)) != NULL)
		{
			if(dir->d_type!=DT_DIR || (strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0))
			{
				if(dir->d_type==DT_DIR)
				{
					folderSize+=SendFolder(upload, socket, URL,dir->d_name, newDestPath,buf, remBuf, topLevel);
				}
				else
				{
					folderSize+=SendFile(upload, socket, URL, dir->d_name,newDestPath, buf, remBuf);
				}
			}
		}
		free(newDestPath);
	}
	closedir(dpr);
	free(URL);

	return folderSize;
}

long long int SendFile(struct UpDown *upload, int socket, char *filePath, char *fileName, char *destPath, char *buf, int *remBuf)
{
	int lenName, lenDestPath, lenHead,i;
	long long int remaining,readed,totalReaded;
	long long int fileSize=0;
	char *URL;
	char *head;			//Head = type(5) + nameSize(V) + pathSize(V) + fileSize(15) + name + destPath +;
	FILE *fpr;

	int LEN_TYPE=5;
	int LEN_FILE_SIZE=15;

	lenName=strlen(fileName)+1;
	lenDestPath=strlen(destPath)+1;


	if(strlen(filePath)==0)			//file path is null
	{
		URL=(char *)malloc(lenName);
		strcpy(URL,fileName);
	}
	else
	{	URL=Merge3(filePath,"/",fileName);
	}

	if((fpr=fopen(URL,"r"))!=NULL)
	{
		fseek(fpr,0,SEEK_END);
		fileSize=ftell(fpr);
		rewind(fpr);

		lenHead=LEN_TYPE+5+5+LEN_FILE_SIZE+lenName+lenDestPath;
		head = (char *)malloc(lenHead);

		strcpy(head,"file");				//	Head[0-4]
		sprintf(head+5,"%d",lenName);		//	Head[5-9]
		sprintf(head+10,"%d",lenDestPath);	//	Head[10-14]
		sprintf(head+15,"%lld",fileSize);	//	Head[15-29]
		strcpy(head+30,fileName);			//	Head[30-----]
		strcpy(head+30+lenName,destPath);

		i=0;

		remaining=lenHead;
		while(remaining>0)
		{
			if(*remBuf>=remaining)
			{
				memcpy(buf+FILE_BUF_SIZE-(*remBuf),head+(lenHead-remaining),remaining);
				*remBuf-=remaining;
				remaining=0;
			}
			else if(*remBuf>0)
			{
				memcpy(buf+FILE_BUF_SIZE-*remBuf,head+(lenHead-remaining),*remBuf);
				remaining-=*remBuf;
				*remBuf=0;
			}

			if(*remBuf==0)
			{
				if(SendData(socket,buf,FILE_BUF_SIZE)==ERROR)
					perror("File Send error");
				*remBuf=FILE_BUF_SIZE;
			}

		}

		while((readed=fread(buf+FILE_BUF_SIZE-*remBuf,sizeof(char),*remBuf,fpr))>0)
		{
			*remBuf-=readed;
			totalReaded+=readed;
			if(*remBuf<=0)	//	Now Buf is full
			{
				if(SendData(socket,buf,FILE_BUF_SIZE)==ERROR)
					perror("File Send error");

				*remBuf=FILE_BUF_SIZE;
			}
			upload->transmitted+=readed;

			//sprintf(COMMENT,"[Uploader:%s-%s]%s>>%-5.2f%%",upload->IP,upload->object.name,URL,(float)(totalReaded*100/fileSize));
			//sprintf(COMMENT,"[Uploader:%s-%s]%ld:%s",upload->IP,upload->object.name,++UpCounter,URL);
			//MyComment('U',1);
		}

		fclose(fpr);
		free(head);
	}
	else
	{
		printf("\n File can't open. URL %s",URL);
		perror("\nError");
	}
	free(URL);

	return fileSize;

}

FILE* Fopen(char *path, char *fileName)  // Eg : Path=download/xxx/
{
	FILE * fpr;
	int i=0;
	char *URL,temp[10];
	struct stat st;

	URL = (char *)malloc(strlen(path)+strlen(fileName)+10);
	sprintf(URL,"%s/%s",path,fileName);			// Eg : download/xxx/abc.txt


	while((fpr=fopen(URL,"r"))!=NULL)
	{
		fclose(fpr);
		sprintf(URL,"%s/%d_%s",path,++i,fileName);
		//printf("\nURL Excist : %s",URL);

	}
	fpr = fopen(URL,"a");
	free(URL);
	return fpr;
}

char* CreateFolder(char *localPath,char *name)
{
	int i=0;
	char *URL=(char *)malloc(strlen(localPath)+strlen(name)+10);
	char *newFolderName=(char *)malloc(strlen(name)+10);
	struct stat st;

	strcpy(newFolderName,name);
	sprintf(URL,"%s/%s",localPath,newFolderName);

	while(stat(URL,&st)!=-1)
	{
		sprintf(newFolderName,"%s_%d",name,++i);
		sprintf(URL,"%s/%s",localPath,newFolderName);
	}

	mkdir(URL,0700);
	free(URL);
	return newFolderName;
}

void ClearSearchResults(int count)
{
	int i;

	for(i=0;i<count;i++)
	{
		free(SearchResults[i]->object.name);
		free(SearchResults[i]->object.path);
		free(SearchResults[i]);

		SearchResults[i]=NULL;
	}
}

int SendHeaderMessage(int socket, int NextMsgSize)
{
	int retVal;
	char *buf;

	buf=MakeHeaderMsg(NextMsgSize);	//Next msg=join
	retVal=SendData(socket,buf,LEN_HEAD_MSG);
	if(retVal==ERROR)
	{
		perror("Sending header message failed");
		return ERROR;
	}
	//printf("\nHeader message send");

	return 1;
}

long long int ReadObjects(char *folder, int level)
{
	int i,retVal;
	long long int folderSize=0;
	struct dirent *dir;
	DIR *dpr;
	FILE *fpr;
	struct Object *newObject;
	char *objectAddr;

	dpr=opendir(folder);
	if(dpr)
	{

		while((dir=readdir(dpr))!=NULL)
		{
			if(dir->d_type!=DT_DIR || (strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0))
			{
				//File or Folder
				newObject=(struct Object*)malloc(sizeof(struct Object));
				newObject->name=(char *)malloc(strlen(dir->d_name)+1);
				newObject->path=(char *)malloc(strlen(folder)+1);

				strcpy(newObject->name,dir->d_name);
				strcpy(newObject->path,folder);			//Parent Folder
				newObject->level=level;

				newObject->nextObject=objectHead;
				objectHead=newObject;

				objectAddr=Merge3(folder,"/",newObject->name);		//release memmory

				if(dir->d_type==DT_DIR)
				{
					//Object is a folder
					strcpy(newObject->type,"dir");
					newObject->size=ReadObjects(objectAddr,level+1);
				}
				else
				{
					//Object is a file
					strcpy(newObject->type,GetFileExtention(dir->d_name));
					fpr=fopen(objectAddr,"r");
					if(fpr)
					{
						fseek(fpr,0,SEEK_END);
						newObject->size=ftell(fpr);
						fclose(fpr);
						//printf("\n URL : %s,  Size : %ld",objectAddr,newObject->size);
					}
					else
					{
						newObject->size=0;
					}
				}
				folderSize+=newObject->size;

				free(objectAddr);
			}
		}
		closedir(dpr);
	}

	return folderSize;
}

long long int SendObjects(int serverSockID,struct Object *object, int nextBufSize )
{
	int i,bufSize,retVal;
	long long int count;
	char * buf;
	if(!object)		//Sending first file. Also at the end  //Post Order
	{
		buf=MakeHeaderMsg(nextBufSize);
		retVal=SendData(serverSockID,buf,LEN_HEAD_MSG);
		if(retVal==ERROR)
		{
			perror("Sending publishing message failed");
		}
		free(buf);

		return 0;
	}

	bufSize=35+strlen(object->name)+strlen(object->path)+2;
	buf=(char *)malloc(bufSize);

	strcpy(buf,"add");							//		Buf[0-4]
	//		lenNextMsg 	 						//		Buf[5-9]
	strcpy(buf+10,object->type);				//		Buf[10-14]
	sprintf(buf+15,"%lld",object->size);			//		Buf[15-29]
	sprintf(buf+30,"%d",object->level);			//		Buf[30-34]
	strcpy(buf+35,object->name);					//		Buf[35-----]
	strcpy(buf+35+strlen(object->name)+1,object->path);

	count=SendObjects(serverSockID,object->nextObject,bufSize);

	sprintf(buf+5,"%d",nextBufSize);
	retVal=SendData(serverSockID,buf,bufSize);
	if(retVal==ERROR)
	{
		perror("Sending publishing message failed");
	}
	count++;
	printf(GREEN"\r[Publish]%lld:%s"RESET,count,object->name);
	free(buf);

	//Start releasing object
	free(object->name);
	free(object->path);
	free(object);
	return count ;
}

//Sends data
int SendData(int sockID,char *buf, int len)
{
	int written,remaining,i;

	written=0;
	remaining=len;

	while(remaining>0)
	{
		written=write(sockID,buf+(len-remaining),remaining);
		if(written<0)//Error
		{
			perror("\n\nError:");
			return -1;
		}
		remaining-=written;
	}
	return len;
}

//Reads data
int ReadData(int sockID,char *buf,int len)
{
	int readed,remaining;

	readed=0;
	remaining=len;

	while(remaining>0)
	{
		readed=read(sockID,buf+len-remaining,remaining);
		if(readed<=0)
			return readed;
		remaining-=readed;
	}
	return len;
}

char* MakePublishMsg(int lenNextMsg)
{
	char* buf=(char *)malloc(LEN_PUBLISH_MSG);
	strcpy(buf,"pbls");
	sprintf(buf+5,"%d",lenNextMsg);

	return buf;
}

char* MakeJoinMsg(int lenNextMsg,int port)
{
	char* buf=(char *)malloc(LEN_JOIN_MSG);
	strcpy(buf,"join");
	sprintf(buf+5,"%d",lenNextMsg);
	sprintf(buf+10,"%d",port);

	return buf;
}

char* MakeHeaderMsg(int lenNextMsg)
{
	char * buf=(char *)malloc(LEN_HEAD_MSG);
	strcpy(buf,"head");
	sprintf(buf+5,"%d",lenNextMsg);

	return buf;
}

void* EventThread(void* argv)
{
	char *dest;
	char input[10];

	dest=(char *)argv;
	
	while(1)
	{
		scanf("%s",input);
		if(strcmp(input,"b")==0 || strcmp(input,"B")==0)
		{
			strcpy(dest,"B");
			break;
		}
	}

}

//------------------------Printing Fns----------------------------------------------------------
char* Merge3(char* s1,char* s2,char* s3)
{
	char *result=(char *)malloc(strlen(s1)+strlen(s2)+strlen(s3)+1);

	strcpy(result,s1);
	strcat(result,s2);
	strcat(result,s3);

	return result;
}

char* Merge5(char* s1,char* s2,char* s3,char *s4,char* s5)
{
	char *result=(char *)malloc(strlen(s1)+strlen(s2)+strlen(s3)+strlen(s4)+strlen(s5)+1);

	strcpy(result,s1);
	strcat(result,s2);
	strcat(result,s3);
	strcat(result,s4);
	strcat(result,s5);

	return result;
}

char *GetFileExtention(char *filename)
{
    char *dot = strrchr(filename, '.');

    if(!dot || dot == filename)
    	return "";

    return dot + 1;
}

void MyPrintf(int lines)
{
	Lines=lines;
	if(lines>0)
		printf("%s",PRINT);
}

void MyComment(char Sender, int newLine)
{

	if(newLine==0)
	{
		if(Lines==0)
		{
			printf(BLUE"\r%s"RESET,COMMENT);
		}
		else if(Lines==1)
		{
			printf(BLUE" \033[1A\r%s\033[1B"RESET,COMMENT);
		//	printf("%s", PRINT);
		}
	}
	else
	{
		if(Lines==0)
			printf(BLUE"\n%s"RESET,COMMENT);
		else if(Lines==1)
		{
			printf(BLUE"\r%s"RESET,COMMENT);
			printf("%s", PRINT);
		}
		else if(Lines==2)
		{
			printf(BLUE"\033[F\r%s"RESET,COMMENT);
			printf("%s", PRINT);
		}
		else if(Lines==3)
		{
			printf(BLUE"\033[F\r%s"RESET,COMMENT);
			printf("%s", PRINT);
		}
	}

}
