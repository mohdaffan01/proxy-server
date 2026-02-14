#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_BYTES 4096    //max allowed size of request/response
#define MAX_CLIENTS 400     //max number of client requests served at a time
#define MAX_SIZE 200*(1<<20)     //size of the cache
#define MAX_ELEMENT_SIZE 10*(1<<20)     //max size of an element in cache

typedef struct cache_element cache_element;

struct cache_element{
    char* data;         //data stores response
    int len;          //length of data i.e.. sizeof(data)...
    char* url;        //url stores the request
	time_t lru_time_track;    //lru_time_track stores the latest time the element is  accesed
    cache_element* next;    //pointer to next element
};

cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();

int port_number = 8080;				// Default Port
int proxy_socketId;					// socket descriptor of proxy server
pthread_t tid[MAX_CLIENTS];         //array to store the thread ids of clients
sem_t semaphore;	                //if client requests exceeds the max_clients this semaphore puts the
                                    //waiting threads to sleep and wakes them when traffic on queue decreases
//sem_t cache_lock;			       
pthread_mutex_t lock;               //lock is used for locking the cache


cache_element* head;                //pointer to the cache
int cache_size;             //cache_size denotes the current size of the cache

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

int connectRemoteServer(char* host_addr, int port_num)
{
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0){
        perror("Socket creation failed");
        return -1;
    }

    struct hostent *host = gethostbyname(host_addr);
    if(host == NULL){
        fprintf(stderr, "No such host exists\n");
        close(remoteSocket);  
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    memcpy(&server_addr.sin_addr,
           host->h_addr_list[0],
           host->h_length);

    if(connect(remoteSocket,
               (struct sockaddr*)&server_addr,
               sizeof(server_addr)) < 0)
    {
        perror("Connect failed");
        close(remoteSocket);   
        return -1;
    }

    return remoteSocket;
}


int handle_request(int clientSocket,ParsedRequest *request,char *tempReq)
{
    char *buf = malloc(MAX_BYTES);
    if(!buf) return -1;

    // Safe request line creation
    int written = snprintf(buf, MAX_BYTES,
                           "GET %s %s\r\n",
                           request->path,
                           request->version);

    if(written <= 0){
        free(buf);
        return -1;
    }

    // Force connection close
    ParsedHeader_set(request, "Connection", "close");

    if(ParsedHeader_get(request, "Host") == NULL)
        ParsedHeader_set(request, "Host", request->host);

    ParsedRequest_unparse_headers(request,buf + written,MAX_BYTES - written);

    int server_port = request->port ? atoi(request->port) : 80;

    int remoteSocketID =
        connectRemoteServer(request->host, server_port);

    if(remoteSocketID < 0){
        free(buf);
        return -1;
    }

    // Send request to remote server
    send(remoteSocketID, buf, strlen(buf), 0);

    // -------------------------------
    // RECEIVE RESPONSE SAFELY
    // -------------------------------

    char *cache_buffer = NULL;
    int total_size = 0;

    while(1)
    {
        int bytes = recv(remoteSocketID, buf,MAX_BYTES, 0);

        if(bytes <= 0)
            break;

        // Send to client
        send(clientSocket, buf, bytes, 0);

        // Expand cache buffer safely
        char *new_buffer =
            realloc(cache_buffer, total_size + bytes);

        if(!new_buffer){
            free(cache_buffer);
            free(buf);
            close(remoteSocketID);
            return -1;
        }

        cache_buffer = new_buffer;

        memcpy(cache_buffer + total_size,
               buf,
               bytes);

        total_size += bytes;
    }

    // Add to cache (binary safe)
    if(cache_buffer && total_size > 0)add_cache_element(cache_buffer, total_size, tempReq);

    free(cache_buffer);
    free(buf);
    close(remoteSocketID);

    return 0;
}


int checkHTTPversion(char *msg)
{
	int version = -1;

	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										// Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}

//handle the socket connection of client
 //handles client HTTP requests by checking cache first, if not found fetches from remote server, then cleans up and exits.
void* thread_fn(void* socketNew){
    sem_wait(&semaphore);//if limit is full then stop here

    int socket = *((int*)socketNew);
    free(socketNew);

    char *buffer = calloc(MAX_BYTES, sizeof(char));//allocate buffer to store the request
    int bytes_recv = recv(socket, buffer, MAX_BYTES, 0);

    while(bytes_recv > 0){//http request not comes complete in one time it comes in the form of chunks then it check that complete request comes or not
        int len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_recv = recv(socket, buffer + len, MAX_BYTES - len, 0);//recieve more data because request is incomplete 
        }
        else break;
    }

    if(bytes_recv <= 0){//client gone or error
        close(socket);
        free(buffer);
        sem_post(&semaphore);
        return NULL;
    }

    char *tempReq = malloc(strlen(buffer) + 1);
    strcpy(tempReq, buffer);   // safe copy with null terminator

    cache_element* temp = find(tempReq);//search in cache

    //check the request present in cache 
    if(temp != NULL){ //temp = not null -> cache hit 
        send(socket, temp->data, temp->len, 0);//cache data send to the client socket
        printf("Data retrieved from cache\n");
    }else{//cache miss
        ParsedRequest* request = ParsedRequest_create();//store the http parts
        if(request == NULL){
            close(socket);
            free(buffer);
            free(tempReq);
            sem_post(&semaphore);//release the semaphore
            return NULL;
        }

        if (ParsedRequest_parse(request, buffer, strlen(buffer)) == 0){
            if(!strcmp(request->method,"GET")){//check is it get method 
                if(request->host && request->path && request->version){// check is it correct url and http version
                    if(handle_request(socket, request, tempReq) < 0)
                        sendErrorMessage(socket, 500);
                }
            }
        }

        ParsedRequest_destroy(request);//free parsing structure
    }

    shutdown(socket, SHUT_RDWR);//close connection both read and write
    close(socket);
    free(buffer);
    free(tempReq);

    sem_post(&semaphore);//release semaphore
    return NULL;
}



int main(int argc, char * argv[]){//./proxy_server 8080 (./proxy_server - argv[0] , 8080 - argv[1] )
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;

    sem_init(&semaphore, 0, MAX_CLIENTS);//initialize the semaphore
    pthread_mutex_init(&lock, NULL);

    if(argc == 2)
        port_number = atoi(argv[1]);//atoi ->convert string into integer
    else{
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    printf("Starting Proxy Server on Port : %d\n", port_number);

    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);//create new socket
    if(proxy_socketId < 0){
        perror("socket failed");
        exit(1);
    }

    int reuse = 1;
    setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("bind failed");
        exit(1);
    }

    if(listen(proxy_socketId, MAX_CLIENTS) < 0){
        perror("listen failed");
        exit(1);
    }

    printf("Proxy running...\n");

    while(1)//server always run and accept the connection
    {
        client_len = sizeof(client_addr);

        client_socketId = accept(proxy_socketId,(struct sockaddr*)&client_addr,(socklen_t*)&client_len);

        if(client_socketId < 0){
            perror("accept failed");
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("Client connected: %s:%d\n",
                ip, ntohs(client_addr.sin_port));//ntos - convert network byte order to host byte order of port

        //  allocate memory for thread argument
        int *pclient = malloc(sizeof(int));
        *pclient = client_socketId;

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, thread_fn, pclient);//create new thread

        // detach so no zombie threads
        pthread_detach(thread_id);
    }

    close(proxy_socketId);//close the linsening socket
    return 0;
}

//it takes the url string and compare the url and cache url
cache_element* find(char* url){

// Checks for url in the cache if found returns pointer to the respective cache element or else returns NULL
    cache_element* site=NULL;
	//sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);//set lock because many threads or request not access 
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    if(head!=NULL){
        site = head;
        while (site!=NULL)
        {
            if(!strcmp(site->url,url)){
				printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            site=site->next;
        }       
    }
	else {
    printf("\nurl not found\n");
	}
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);//release the lock because other threads enter
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;
}

void remove_cache_element()
{
    if(head == NULL)
        return;

    cache_element *prev = NULL;
    cache_element *curr = head;
    cache_element *lru = head;
    cache_element *lru_prev = NULL;

    // Find LRU node
    while(curr != NULL)
    {
        if(curr->lru_time_track < lru->lru_time_track){
            lru = curr;
            lru_prev = prev;
        }
        prev = curr;
        curr = curr->next;
    }

    // Remove node
    if(lru_prev == NULL)
        head = lru->next;
    else
        lru_prev->next = lru->next;

    cache_size -= (lru->len + strlen(lru->url) +
                   sizeof(cache_element) + 1);

    free(lru->data);
    free(lru->url);
    free(lru);
}


int add_cache_element(char* data, int size, char* url)
{
    pthread_mutex_lock(&lock);

    int element_size = size + strlen(url)
                      + sizeof(cache_element) + 1;

    if(element_size > MAX_ELEMENT_SIZE){
        pthread_mutex_unlock(&lock);
        return 0;
    }

    while(cache_size + element_size > MAX_SIZE){
        remove_cache_element();   // SAFE now
    }

    cache_element* element =
        malloc(sizeof(cache_element));

    element->data = malloc(size);
    memcpy(element->data, data, size);

    element->url = malloc(strlen(url)+1);
    strcpy(element->url, url);

    element->len = size;
    element->lru_time_track = time(NULL);

    element->next = head;
    head = element;

    cache_size += element_size;

    pthread_mutex_unlock(&lock);
    return 1;
}
