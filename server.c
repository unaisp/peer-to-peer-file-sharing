/*
	server.c
	This is the program to run server
	It has a process to accept connections and will create a seperate thread for handling client connections.
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
#include <sys/shm.h>
#include <sys/ipc.h>
#include <pthread.h>


#define MAX_QUE 5
#define ERROR -1

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

struct  Object
{
	char *name;
	char type[5];
	long long int size;
	char *path;
	int level;
	struct Object *nextObject;
};

struct Peer
{
	int status;
	int port;						//	Port no on which clienting listening
	char IP[20];
	struct Object *firstObject;  
	struct Peer *nextPeer;
}*headPeer;

struct ClientConnection
{
	int sockID;
	struct sockaddr_in sockAddr;
	pthread_t threadID;
};

//Main Functions
void * HandleClientNew(void *);
void* ProbingThread(void *);
//Suporting
void ClearPeerObjects(struct Object *);
int GetAndDisplayPeers(struct Peer*);
int SendData(int ,char *, int );
int ReadData(int ,char *,int );
struct Peer *IdentifyPeer(char *, int);

//Printing
void clear();
void MyError(const char *s);
void Error(char * ,int ,int );
void ClientError(struct  ClientConnection*,char * ,int );
void ClientPrint(struct  ClientConnection*,char *);
char * BytesToString(char * , long long int );

pthread_mutex_t mutex;

void main()
{
	int i;
	int serverSockID;
	int lenSocket;
	struct sockaddr_in serverAddr;
	int peerIP,peerListeniningPort;
	struct ClientConnection *newConnection;
	pthread_t prbThreadID;

	//Initializations
	lenSocket=sizeof(struct sockaddr_in);

	//Server Socket Creation
	if((serverSockID=socket(AF_INET,SOCK_STREAM,0))==ERROR)
		Error("Socket creation failed",1,1);
		
	//Server Socket Address Initialization
	serverAddr.sin_family=AF_INET;
	serverAddr.sin_port=htons(10000);
	serverAddr.sin_addr.s_addr=INADDR_ANY;
	memset(&serverAddr.sin_zero,0,sizeof(serverAddr.sin_zero));

	system("clear");

	//Binding socket to server address
	 if(bind(serverSockID,(struct sockaddr *)&serverAddr,lenSocket)==ERROR)
		Error("Binding failed",1,1);

	//Server started listening
	if(listen(serverSockID,MAX_QUE)==ERROR)
		Error("Listening failed",1,1);

	clear();

	printf(YELLOW"\n//////////////////////////////////////");
	printf("\n              NAPSTER SEVER");
	printf("\n//////////////////////////////////////");
	printf("\nServer Started running" );
	printf("\nServer Port : %d\n\n"RESET,ntohs(serverAddr.sin_port));	

	pthread_create(&prbThreadID,NULL,ProbingThread,NULL);

	headPeer==NULL;
	while(1)
	{
		//New client connection accepting
		newConnection=(struct ClientConnection*)malloc(sizeof(struct ClientConnection));
		if((newConnection->sockID=accept(serverSockID,(struct sockaddr *)&newConnection->sockAddr,&lenSocket))==ERROR)
		{
			//Error on accepting connectection
			Error("Connection accepting failed",1,0);
		}
		else
		{			
			//Client connected to peer;		
			if(pthread_create(&(newConnection->threadID),NULL,HandleClientNew,newConnection)!=0)
				MyError("Child thread creation failed");
		}		
	}

}

void * HandleClientNew(void * arg)
{
	struct ClientConnection *con;
	struct Peer *connectedPeer,*peer;
	int lenNextMsg,resultSize,prevResultSize,retVal;
	int lenName,lenPath;
	char *buf, query[100];
	char type[5];
	char IP[20];
	char *prevResult,*result,sizeToString[10];
	int port;
	struct Object *object,*newObject;

	con=(struct ClientConnection *)arg;
	strcpy(IP,inet_ntoa(con->sockAddr.sin_addr));	
	printf(YELLOW"\n[%s]Connected."RESET,IP);

	lenNextMsg=LEN_HEAD_MSG;
	while(lenNextMsg>0)
	{
		buf=(char *)malloc(lenNextMsg);
		//printf("\n[%s]Server waiting for %d Byte of data",IP,lenNextMsg);

		retVal=ReadData(con->sockID,buf,lenNextMsg);

		if(retVal==ERROR)
		{	
			printf(RED"\n[%s]Error on reading data from Buffer. Connection closed."RESET,inet_ntoa(con->sockAddr.sin_addr));
			close(con->sockID);
			free(con);
			free(buf);
			break;
		}
		if(retVal==0) // client disconnected
		{
			printf(YELLOW"\n[%s]Connection disconnected."RESET,inet_ntoa(con->sockAddr.sin_addr));
			close(con->sockID);
			free(con);
			free(buf);
			break;
		}

		memcpy(type,buf,5);
		
		if(strcmp(type,"head")==0)			//	First Packet
		{
			lenNextMsg=atoi(buf+5);
		}
		else if(strcmp(type,"join")==0 || strcmp(type,"recn")==0)				//Join & Reconnect
		{
			lenNextMsg=atoi(buf+5);						//	Buf[5-9]
			port=atoi(buf+10);							//	Buf[10-14]
			strcpy(IP,inet_ntoa(con->sockAddr.sin_addr));	

			connectedPeer=IdentifyPeer(IP,port);

			if(connectedPeer==NULL)
			{
				//No record found.
				connectedPeer=(struct Peer *)malloc(sizeof(struct Peer));
				connectedPeer->port=port;
				strcpy(connectedPeer->IP,IP);
				connectedPeer->firstObject=NULL;

				//Apply Lock
				pthread_mutex_lock(&mutex);
				connectedPeer->nextPeer=headPeer;
				headPeer=connectedPeer;
				pthread_mutex_unlock(&mutex);
				//Release Lock;
			}
			connectedPeer->status=1;
			if(strcmp(type,"join")==0)
				printf(CYAN"\n[%s]Join message arrived. Listening Port=%d."RESET,IP,port);
		}
		else if(strcmp(type,"pbls")==0)			//Publish
		{
			printf(CYAN"\n[%s]Publish message received"RESET,IP);			
			lenNextMsg=atoi(buf+5);			//	Buf[5-9]
			ClearPeerObjects(connectedPeer->firstObject);
			connectedPeer->firstObject=NULL;		
			connectedPeer->status=1;
		}
		else if(strcmp(type,"add")==0)			//Adding Entry
		{		
			newObject = (struct Object*)malloc(sizeof(struct Object));
			lenName=strlen(buf+35)+1;
			lenPath=strlen(buf+35+lenName)+1;

			newObject->name=(char *)malloc(lenName);
			newObject->path=(char *)malloc(lenPath);

			lenNextMsg=atoi(buf+5);				//	Buf [5-9]
			strcpy(newObject->type,buf+10);		//	Buf [10-14]
			newObject->size=atoi(buf+15);		//	Buf [15-29]
			newObject->level=atoi(buf+30);		//	Buf [30-34]
			strcpy(newObject->name,buf+35);		//	Buf [35------]
			strcpy(newObject->path,buf+35+lenName);

			newObject->nextObject=connectedPeer->firstObject;
			connectedPeer->firstObject=newObject;

			printf(CYAN"\n[%s]file : %s/%s.  Size : %s"RESET,IP,newObject->path,newObject->name,BytesToString(sizeToString,newObject->size));
		}          
		else if(strcmp(type,"srch")==0)			//Search
		{
			connectedPeer->status=1;
			lenNextMsg=atoi(buf+5);				//	Buf [5-9]
			strcpy(query,buf+10);				//	Buf [10-----]

			//Reply : Type+lenNxtMsg+IP+Port+ObjectType+ObjectSize+Name+Path;

			printf(CYAN"\n[%s]Search request received. Query="BLUE"%s."RESET,IP,query);
			pthread_mutex_lock(&mutex);
			peer=headPeer;
			pthread_mutex_unlock(&mutex);
			result=NULL;
			prevResult=NULL;
			while(peer)
			{
				if(peer->status<-5)
				{
					peer=peer->nextPeer;
					continue;
				}

				object=peer->firstObject;
				while(object)
				{
					if(strcmp(object->name,query)==0)
					{
						resultSize=5+5+20+5+5+15+strlen(object->name)+strlen(object->path)+2;

						if(!prevResult)
						{
							//Sending Header Message
							result=(char *)malloc(LEN_HEAD_MSG);
							strcpy(result,"head");
							sprintf(result+5,"%d",resultSize);

							retVal=SendData(con->sockID,result,LEN_HEAD_MSG);
							if(retVal==ERROR)
							{
								printf("\n[%s]Search reply failed",IP);
								lenNextMsg=LEN_HEAD_MSG;
							}
							free(result);
						}
						else 
						{
							sprintf(prevResult+5,"%d",resultSize);
							retVal=SendData(con->sockID,prevResult,prevResultSize);
							if(retVal==ERROR)
								perror("Message sending failed");
							free(prevResult);
						}

						result=(char *)malloc(resultSize);
						strcpy(result,"rslt");						// Result[0-4]
						strcpy(result+10,peer->IP);					//	Result [10-29]
						sprintf(result+30,"%d",peer->port);			//	Result [30-34]
						strcpy(result+35,object->type);				//	Result[35-39]
						sprintf(result+40,"%lld",object->size);		//	Result [40-54]
						strcpy(result+55,object->name);				// Result [55---]
						strcpy(result+55+strlen(object->name)+1,object->path);
						//This result will send when next match occurs

						prevResult=result;
						prevResultSize=resultSize;
						
					}
					object=object->nextObject;
				}
				peer=peer->nextPeer;
			}

			if(!prevResult)
			{
				//No Match occurs
				//Sending Header Message
				result=(char *)malloc(LEN_HEAD_MSG);
				strcpy(result,"head");
				sprintf(result+5,"%d",0);		//0=COmpleted

				retVal=SendData(con->sockID,result,LEN_HEAD_MSG);
				if(retVal==ERROR)
				{
					printf("\n[%s]Search reply failed",IP);
					lenNextMsg=LEN_HEAD_MSG;
				}
				free(result);
			}
			else 		//Last result
			{
				sprintf(prevResult+5,"%d",0);
				retVal=SendData(con->sockID,prevResult,prevResultSize);
				if(retVal==ERROR)
					printf("\n[%s]Search reply failed",IP);
				free(prevResult);
			}
		}
		free(buf);
	}
}

void* ProbingThread(void * argv)
{
	int sockID,lenSocket,retVal;
	struct Peer *peer,*temp,*next;
	struct sockaddr_in peerAddr;
	char buf[10];

	lenSocket=sizeof(struct sockaddr_in);

	strcpy(buf,"prob");
	strcpy(buf+5,"0");

	while(1)
	{
		sleep(30);
		pthread_mutex_lock(&mutex);
		peer=headPeer;
		pthread_mutex_unlock(&mutex);
		printf(YELLOW"\nProbing started"RESET);

		while(peer)
		{
			next=peer->nextPeer;
			sockID=socket(AF_INET,SOCK_STREAM,0);
			if(sockID==ERROR)
				perror("\nSocket error");

			peerAddr.sin_family=AF_INET;
			peerAddr.sin_addr.s_addr=inet_addr(peer->IP);
			peerAddr.sin_port=htons(peer->port);
			memset(&peerAddr.sin_zero,0,sizeof(peerAddr.sin_zero));

			if(connect(sockID,(struct sockaddr *)&peerAddr,lenSocket)==ERROR)
			{
				//Error
				peer->status--;
				printf(BLUE"\nPeer[%s:%d] is not availble. Status = %d."RESET,peer->IP,peer->port,peer->status);				

				if(peer->status<=-10)
				{
					ClearPeerObjects(peer->firstObject);
					if(headPeer==peer)//First Node
					{	
						pthread_mutex_lock(&mutex);
						headPeer=headPeer->nextPeer;
						pthread_mutex_unlock(&mutex);
					}
					else	
					{	
						temp=headPeer;
						while(temp->nextPeer!=peer)	
							temp=temp->nextPeer;
						temp->nextPeer=peer->nextPeer;

					}
					free(peer);
					printf(RED"Peer is cleared"RESET);
				}
			}
			else
			{
				//Sending Header Message	
				retVal=SendData(sockID,buf,LEN_HEAD_MSG);
				if(retVal==ERROR)
				{
					perror("\nSending header message failed");
				}
				close(sockID);
				peer->status=1;
				printf(BLUE"\nPeer[%s:%d] is alive. Status = %d."RESET,peer->IP,peer->port,peer->status);	
			}
			
			peer=next;
		}
	}
}


///////////////////////////////////////////////////////////////////////////


void ClearPeerObjects(struct Object *object)
{
	if(!object)
		return;
	
	ClearPeerObjects(object->nextObject);
	free(object->name);
	free(object->path);
	free(object);
}

struct Peer* IdentifyPeer(char * IP, int port)
{
	struct Peer* peer;

	pthread_mutex_lock(&mutex);
	peer=headPeer;
	pthread_mutex_unlock(&mutex);
	while(peer)
	{
		if(strcmp(IP,peer->IP)==0 && port==peer->port)
			return peer;
		peer=peer->nextPeer;
	}
	return NULL;
}

//Sends data
int SendData(int sockID,char *buf, int len)
{
	int written,remaining;

	written=0;
	remaining=len;
	
	while(remaining>0)
	{
		written=write(sockID,buf+len-remaining,remaining);
		if(written<0)//Error
			return -1;
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
void clear()
{
	system("clear");
}

//////////////////Prinitng
void Error(char * msg,int showPerror,int exitServer)
{
	printf(RED"\n");

	if(showPerror)
		perror(msg);
	else
		printf("%s",msg);

	printf(""RESET);

	if(exitServer)
	{
		printf("\n");
		exit(-1);
	}
}
void MyError(const char *s)
{
	perror(s);
	printf("\n");
	exit(-1);
}
void ClientError(struct ClientConnection *con,char * msg,int showPerror)
{
	char buf[100];
	int threadID;
	printf(RED"\n");

	sprintf(buf,RED"\n[%s]%s",inet_ntoa(con->sockAddr.sin_addr),msg);

	if(showPerror)
		perror(buf);
	else
		printf("[%s]%s",inet_ntoa(con->sockAddr.sin_addr),msg);

	printf("."RESET);
}

void ClientPrint(struct ClientConnection *con,char *msg)
{
	printf("\n[%s]%s",inet_ntoa(con->sockAddr.sin_addr),msg);
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
