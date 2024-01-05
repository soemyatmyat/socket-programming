#include "util.h"
#include <signal.h>
#include <unordered_map>

// parameterizations
extern int discover_peers;
extern int foreground;
extern sigset_t signal_set; // Set up a signal set containing the signals to wait for
extern int terminate;

// file server parameterization
extern int file_port; // initialized in a4.cpp
extern int t_max;
extern int t_incr;
extern int delay;
extern std::unordered_map<std::string, int> filename_to_index;
extern pthread_mutex_t lock;

// shell server parameterization
extern int shell_port; // initialized in a4.cpp
extern int active_shell_client;
extern int max_shell_client;

// replication server parameterization
struct Peer {
    std::string host;
    int port;
    int peer_socket;
};
extern std::vector<Peer> peers;
extern int replica_port; // initialized in a4.cpp
extern int wait_response; // for 2PC, initialized in replica_server.cpp
extern std::vector<int> readBytes_arr; 
extern const int udp_port; 

// server usage message:
const std::string server_usage = "\nUsage: <executable> -f [port_number] -s [port_number] -t [threads_incr] -T [threads_max] -p [replication_port] -x -D -d\n OR \n Usage: <executable> -f [port_number] -s [port_number] -t [threads_incr] -T [threads_max] -p [replication_port] <peer_ip:peer_replication_port> -D -d\n";

/*** File server stuff: ***/
/*
* Constructs a file server 
*/
int file_server(int port, const char* ip_addr);



/*** Shell server stuff: ***/   
/*
* Constructs a shell server 
*/
int shell_server(int port, const char* ip_addr);

/*** Replica server stuff: ***/
/*
* Constructs a replica server 
*/
int replica_server(int port, const char* ip_addr);

/**
 * 
*/
std::string execute_peer_command(char* commandBuffer);

/**
 * 
*/
int replicate_to_peers(char* commandBuffer, int wait = 0);

/**
 * 
*/
int udp_listener(int port);

/**
 * 
*/
int udp_broadcast(int port, std::string message);