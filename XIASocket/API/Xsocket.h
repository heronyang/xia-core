#ifndef XSOCKET_H
#define XSOCKET_H

#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif
//Socket library side: Control address/info
#define DEFAULT_MYADDRESS "192.0.0.1" 
#define MYPORT "0"//Chooses random port

//Click side: Control/data address/info
//The actual IPs don't matter, it just has to be in the correct subnet
#define DEFAULT_CLICKCONTROLADDRESS "192.0.0.2" 
#define CLICKCONTROLPORT "5001"
//#define CLICKOPENPORT "5001"
//#define CLICKBINDPORT "5002"
#define CLICKCLOSEPORT "5003"
//#define CLICKCONNECTPORT "5004"
//#define CLICKACCEPTPORT "5005"


#define DEFAULT_CLICKDATAADDRESS "192.0.0.2" 
#define CLICKDATAPORT "10000"
//#define CLICKPUTCIDPORT "10002"
//#define CLICKSENDTOPORT "10001"


//set xia.click sorter to sort based on these ports. 


#define ATTEMPTS 3 //Number of attempts at opening a socket 
#define MAXBUFLEN 2000

//Function list
extern int Xsendto(int sockfd,const void *buf, size_t len, int flags,char * dDAG, size_t dlen);
extern int Xrecvfrom(int sockfd,void *buf, size_t len, int flags,char * dDAG, size_t *dlen);
extern int Xsocket();
extern int Xconnect(int sockfd, char* dest_DAG);
extern int Xbind(int sockfd, char* SID);
extern int Xclose(int sock);
extern int Xrecv(int sockfd, void *buf, size_t len, int flags);
//extern int Xsend(int sockfd,const void *buf, size_t len, int flags);
extern int Xsend(int sockfd,void *buf, size_t len, int flags);
extern int XgetCID(int sockfd, char* dDAG, size_t dlen);
extern int XputCID(int sockfd, const void *buf, size_t len, int flags,char* sDAG, size_t dlen);
extern int Xaccept(int sockfd);
extern void error(const char *msg);
#ifdef __cplusplus
}
#endif

#endif

