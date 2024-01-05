#include "shfd.h"

// parameterizations 
int discover_peers = 0;
int terminate = 0;
int foreground = 0;
sigset_t signal_set;
char hostname[256]; // hostname 

// file server parameterization
int file_port = 9002; // dummy values
int delay = 0;
int t_max = 20; // default: 20
int t_incr = 5; // default: 5

// shell server parameterization
int shell_port = 9001; // dummy values
int active_shell_client = 0; 
int max_shell_client = 1;

// peers;
const int udp_port = 2345; // constant (to add to README)
int replica_port = 0; // dummy values
std::vector<Peer> peers; 


// handle arguments and pass on the command to other modules
int main (int argc, char** argv) {
    
    // In the case where client closes connection/ close socket, ignore it since it's already handled.
    signal(SIGPIPE, SIG_IGN);

    // add all the signals for graceful termination process
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGQUIT);
    sigaddset(&signal_set, SIGTERM);
    sigaddset(&signal_set, SIGINT);

    // Block the signal so that it doesn't terminate the program immediately
    if (sigprocmask(SIG_BLOCK, &signal_set, nullptr) == -1) {
        perror("Error blocking signal");
        return 1;
    }

    // For arguments 
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i]; 
        // Process command-line switches
        if (arg.size() > 1 && arg[0] == '-') { 
            try {
            // check for specific switches
                if (arg == "-f") { // file server port
                    file_port = std::stoi(argv[i+1]);
                    i++;
                } else if (arg == "-s") { // shell server port 
                    shell_port = std::stoi(argv[i+1]);
                    i++;
                } else if (arg == "-t") { // t_incr
                    t_incr = std::stoi(argv[i+1]);
                    i++;
                } else if (arg == "-D") { // delay for read/ write
                    delay = 1;
                } else if (arg == "-v") { // verbose
                    // verbose
                } else if (arg == "-d") { // daemon
                    foreground = 1;
                } else if (arg == "-T") { // t_max
                    t_max = std::stoi(argv[i+1]);
                    i++;
                } else if (arg == "-p") {
                    replica_port = std::stoi(argv[i+1]);
                    i++;
                } else if (arg == "-x") {
                    if (peers.size() > 0) {
                        std::cerr << argv[0] << ": Illegal option -" << arg;
                        std::cerr << server_usage << std::endl;
                        return 1;
                    } 
                    discover_peers = 1;
                } else {
                    std::cerr << argv[0] << ": Illegal option -" << arg;
                    std::cerr << server_usage << std::endl;
                    return 1;
                }
            } catch (const std::invalid_argument& e) {
                std::cerr << argv[0] << ": Illegal option -" << arg;
                std::cerr << server_usage << std::endl;
                return 1;
            }
        } else {
            // Peer arguments
            size_t colonPos = arg.find(':');
            if (discover_peers == 0) {
                if (colonPos != std::string::npos) {
                    Peer peer;
                    peer.host = arg.substr(0, colonPos);
                    peer.port = std::stoi(arg.substr(colonPos + 1));
                    peers.push_back(peer);
                } 
            } else {
                std::cerr << argv[0] << ": Illegal option -" << arg;
                std::cerr << server_usage << std::endl;
                return 1;
            }
        }
        
    }  // arguments

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("Error getting hostname");
    }

    pid_t shell_pid = fork();
    if (shell_pid <0) {
        perror("Fork error");
        exit(EXIT_FAILURE);
    } else if (shell_pid == 0) { // child shell pid 
        const char* ip_address = "INADDR_LOOPBACK";
        return shell_server(shell_port, ip_address);
    }
    if (foreground == 1) {
        const char* ip_address = "INADDR_ANY";
        return file_server(file_port, ip_address);
    } else {
        pid_t file_pid = fork();
        if (file_pid <0) {
            perror("Fork error");
            exit(EXIT_FAILURE);
        } else if (file_pid == 0) {
            const char* ip_address = "INADDR_ANY";
            return file_server(file_port, ip_address);
        }
    }

    return 0; // parent exited
}

