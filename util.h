#include <netdb.h>
#include <iostream>
#include <sys/poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <vector>

// parameterizations
const int MAX_LINE_LENGTH = 1024;
const int recv_nodata = -2;


int createClientSocket(const int port, const char* ip_addr); 

/*
 * Given the port number and IP_ADDR,
 * this function constructs server socket 
 * and set it into listen mode 
 * return server_socket 
*/
// int createServerSocket(const int PORT, const unsigned long int ip_addr);
int createServerSocket(const int PORT, const char* ip_addr);

/*
 * Credit: Prof Stefan
*/
int readline(const int fd, char* buf, const size_t max); 

/**
 * Put the process into background and disconnect it from tty
*/
void daemonize();

/**
 * 
*/
std::vector<char*> tokenize(char* commandBuffer);

int createUnConnectUDPSocket();