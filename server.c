/** @file server.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <queue.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "queue.h"
#include "libdictionary.h"

#define BACKLOG 10	// pending connections of queue

const char *HTTP_404_CONTENT = "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1>The requested resource could not be found but may be available again in the future.<div style=\"color: #eeeeee; font-size: 8pt;\">Actually, it probably won't ever be available unless this is showing up because of a bug in your program. :(</div></html>";
const char *HTTP_501_CONTENT = "<html><head><title>501 Not Implemented</title></head><body><h1>501 Not Implemented</h1>The server either does not recognise the request method, or it lacks the ability to fulfill the request.</body></html>";

const char *HTTP_200_STRING = "OK";
const char *HTTP_404_STRING = "Not Found";
const char *HTTP_501_STRING = "Not Implemented";

// decleration of part III
void *processRequest(void *data);

/**
 * Processes the request line of the HTTP header.
 * 
 * @param request The request line of the HTTP header.  This should be
 *                the first line of an HTTP request header and must
 *                NOT include the HTTP line terminator ("\r\n").
 *
 * @return The filename of the requested document or NULL if the
 *         request is not supported by the server.  If a filename
 *         is returned, the string must be free'd by a call to free().
 */
char* process_http_header_request(const char *request)
{
	// Ensure our request type is correct...
	if (strncmp(request, "GET ", 4) != 0)
		return NULL;

	// Ensure the function was called properly...
	assert( strstr("\n", request) == NULL );
	assert( strstr("\r", request) == NULL );

	// Find the length, minus "GET "(4) and " HTTP/1.1"(9)...
	int len = strlen(request) - 4 - 9;

	// Copy the filename portion to our new string...
	char *filename = malloc(len + 1);
	strncpy(filename, request + 4, len);
	filename[len] = '\0';

	// Prevent a directory attack...
	//  (You don't want someone to go to http://server:1234/../server.c to view your source code.)
	if (strstr(filename, ".."))
	{
		free(filename);
		return NULL;
	}

	return filename;
}


/** Entry point to the program. */
int main(int argc, char **argv)
{
	int socketfd, new_fd; // listen on socketfd, new connections on new_fd
	struct addrinfo hints, *serverInfo; // points to results
	struct sockaddr_storage other_addr; // addr info of connector
	int yes = 1;
	
	// make sure there is a port argument
	if(argc < 2)
	{
		printf("Rerun with port number\n");
		return 0;
	}

	char *port = argv[1];

	memset(&hints, 0, sizeof(hints));	// empty struct
	hints.ai_family = AF_UNSPEC;		// IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	// TCP stream sockets
	hints.ai_flags = AI_PASSIVE;		// fill in my IP for me
	
	if(getaddrinfo(NULL, port, &hints, &serverInfo) != 0)
		return 1;
		
	// loop through all the results and bind to the first we can
	struct addrinfo *p;
    for(p = serverInfo; p != NULL; p = p->ai_next) 
	{
		socketfd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketfd == -1)
            continue;

        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, 
				&yes, sizeof(int)) == -1) 
		{
            printf("error with setting socket options\n");
            exit(1);
        }

        if (bind(socketfd, p->ai_addr, p->ai_addrlen) == -1) 
		{
            close(socketfd);
            printf("Error binding\n");
            continue;
        }

        break;
    }

	if(p == NULL)
	{
		printf("Nothing to bind to\n");
		return 2;
	}

	freeaddrinfo(serverInfo); // done with this

	if(listen(socketfd, BACKLOG) == -1)
	{
		printf("Error trying to listen\n");
		exit(1);
	}

/**********  PART II  ***************/
	queue_t *threads = (queue_t *)malloc(sizeof(queue_t));
	queue_init(threads);
	
	// infinite accepting loop
	while(1)
	{
		socklen_t sinSize = sizeof(other_addr);
		printf("\nServer set up. Waiting for connections\n");

		new_fd = accept(socketfd, (struct sockaddr *)&other_addr,
			 &sinSize);
		if(new_fd == -1)
		{
			printf("Problem with accept\n");
			continue;
		}

		pthread_t *newThread = (pthread_t *)malloc(sizeof(pthread_t));
		queue_enqueue(threads, newThread);
		pthread_create(newThread, NULL, processRequest, &new_fd);
	}

	return 0;
}

/**************  PART III  **************/
void *processRequest(void *data)
{
	dictionary_t *dict = (dictionary_t *)malloc(sizeof(dictionary_t));
	dictionary_init(dict);

	int newfd = *((int *)data);
	int buffSize = 250;
	char buff[buffSize];
	memset(buff, 0, buffSize);

	// read once to fill buffer
	if( recv(newfd, buff, buffSize, 0) == -1)
	{
		printf("Couldn't read stream from %d", newfd);
		pthread_exit(NULL);
	}

	int done = 0;
	if(strstr(buff, "\r\n\r\n") != NULL)
		done = 1;

	// process first recv
	char *toProcess = strtok(buff, "\r\n");
	char *fileName = process_http_header_request( toProcess );
	toProcess = strtok(NULL, "\r\n");

	while(toProcess != NULL)
	{
		dictionary_parse(dict, toProcess);
		toProcess = strtok(NULL, "\r\n");
	}
	
	// if not at end, keep recv'ing until done
	if(!done)
	{
		// fill buff at first
		if( recv(newfd, buff, buffSize, 0) == -1 )
		{
			printf("Couldn't read stream from %d", newfd);
			pthread_exit(NULL);
		}
		
		// while buff doesn't contain "\r\n\r\n"
		while( strstr(buff, "\r\n\r\n") == NULL )
		{
			toProcess = strtok(buff, "\r\n");
			
			while(toProcess != NULL)
			{
				dictionary_parse(dict, toProcess);
				toProcess = strtok(NULL, "\r\n");
			}

			if( recv(newfd, buff, buffSize, 0) == -1 )
			{
				printf("Couldn't read stream from %d", newfd);
				pthread_exit(NULL);
			}
		}

		// process buff for last time
		toProcess = strtok(buff, "\r\n");
			
		while(toProcess != NULL)
		{
			dictionary_parse(dict, toProcess);
			toProcess = strtok(NULL, "\r\n");
		}
	}

	// everything added to dictionary
	int status_code, fileLength;
	char *status_code_string;
	FILE *requestFile;

	if(fileName == NULL)
	{
		status_code = 501;
		status_code_string = (char *)malloc( (strlen(HTTP_501_STRING)+1)
								* sizeof(char) );
		status_code_string = strcpy(status_code_string, HTTP_501_STRING);
		fileLength = strlen(status_code_string);
	}

	else
	{
		if(strcmp(fileName, "/") == 0)
		{
			fileName = (char *)realloc(fileName, 12*sizeof(char));
			fileName = strcat(fileName, "index.html");
		}

		char *newName = (char *)malloc( (3+strlen(fileName)+1) 
			* sizeof(char) );
		newName = strcat(newName, "web");
		newName = strcat(newName, fileName);
		
		free(fileName);
		fileName = newName;
		
		requestFile = fopen(newName, "r");
		if( requestFile == NULL )
		{
			status_code = 404;
			status_code_string = (char *)malloc( (strlen(HTTP_404_STRING)
									+1)	* sizeof(char) );
			status_code_string = strcpy(status_code_string,
									 HTTP_404_STRING);
		}
		else
		{
			status_code = 200;
			status_code_string = (char *)malloc( (strlen(HTTP_200_STRING)
									+1) * sizeof(char) );
			status_code_string = strcpy(status_code_string, 
										HTTP_200_STRING);
			
			fseek(requestFile, 0, SEEK_END);
			fileLength = ftell(requestFile);
			rewind(requestFile);
		}
	}

	// Task 3d
	char *content_type = "text/html";
	if(status_code == 200)
	{
		// get file extension
		char *ext = strrchr(fileName, '.');

		if(strcmp(ext, ".html") == 0)
			content_type = "text/html";
		else if(strcmp(ext, ".css") == 0)
			content_type = "text/css";
		else if(strcmp(ext, ".jpg") == 0)
			content_type = "image/jpeg";
		else if(strcmp(ext, ".png") == 0)
			content_type = "image/png";
		else
			content_type = "text/plain";
	}

	// Task 3e
	char *t = "HTTP/1.1";
	char *response = (char *)malloc( (strlen(status_code_string)+
						strlen(t)+4+1) * sizeof(char) );
	sprintf(response, "%s %d %s\r\n", t, status_code,
			 status_code_string);

	t = "Content-Type: ";
	response = (char *)realloc(response, (strlen(response) + strlen(t) + 
					strlen(content_type)+2+1) * sizeof(char));
	sprintf(response, "%s%s%s\r\n", response, t, content_type);
	
	t = "Content-Length: ";
	int intLength = 0;
	int i;
	for(i = fileLength; i>0; i/=10)
		intLength++;
	if(fileLength == 0)
		intLength = 1;
	response = (char *)realloc(response, (strlen(response) + strlen(t) + 
					intLength+2+1) * sizeof(char));
	sprintf(response, "%s%s%d\r\n", response, t, fileLength);

	t = "Connection: ";
	const char *result = dictionary_get(dict, "Connection");
	if( strcasecmp(result, "Keep-Alive") != 0)
		result = "close";
	else
		result = "Keep-Alive";
	response = (char *)realloc(response, (strlen(response) + strlen(t) + 
				strlen(result)+2+1) * sizeof(char));
	sprintf(response, "%s%s%s\r\n", response, t, result);

	response = (char *)realloc(response, (strlen(response) + 2+1) 
					* sizeof(char));
	sprintf(response, "%s\r\n", response);

	// read the file
	char *read = (char *)malloc(fileLength * sizeof(char));
	fread(read, sizeof(char), fileLength, requestFile);
	response = (char *)realloc(response, (strlen(response) + strlen(read)
					 + 2) * sizeof(char));
	sprintf(response, "%s%s", response, read);

	printf("%s\n", response);

	if( send(newfd, response, strlen(response)+1, 0) == -1)
	{
		printf("error sending data");
		pthread_exit(NULL);
	}

	pthread_exit(NULL);
}
