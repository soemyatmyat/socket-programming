#include "util.h"


// Since UDP is connectionless, each call to recvfrom is independent, and there is no need for an explicit listen state. The server can receive data from any client at any time as long as it sends data to the server's bound address and port.
int createUnConnectUDPSocket() {

    // create a socket, AF_INET family and SOCK_DGRAM = UPD connection (data diagram)
    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) {
        perror("UDP Socket creation failed");
        return -1;
    }

    return udpSocket;

}

int createClientSocket(const int port, const char* ip_addr) {

    // Get server information using getaddrinfo
    struct addrinfo hints, *serverInfo;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(ip_addr, std::to_string(port).c_str(), &hints, &serverInfo) != 0) {
        std::cerr << "Error getting server information" << std::endl;
        return -1;
    }    

    int clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == -1) {
        perror("Socket creation failed");
        return -1;
    }
    // Connect to the server
    if (connect(clientSocket, serverInfo->ai_addr, serverInfo->ai_addrlen) == -1) {
        std::cerr << "Socket connection failed with " << ip_addr << "." << strerror(errno) << std::endl;
        perror("Connection failed");
        close(clientSocket);
        return -1;
    }

    // printf("Connected to %s on port %d.\n", ip_addr, port);    
    return clientSocket;

}

int createServerSocket(const int port, const char* ip_addr="INADDR_ANY") {
    int serverSocket;
    // Specify the server's address and port 
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;

    // INADDR_ANY: binds the socket to any available local address, making it open to both local and external connections. 
    // INADDR_LOOPBACK: binds the socket to local connection only
    // unsigned long int addr_value;
    if (strcmp(ip_addr, "INADDR_ANY") == 0) {
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    } else if (strcmp(ip_addr, "INADDR_LOOPBACK") == 0) {
        serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); 
    } else {
        inet_pton(AF_INET, ip_addr, &serverAddr.sin_addr);
    }
    // serverAddr.sin_addr.s_addr = htonl(addr_value); 
    serverAddr.sin_port = (unsigned short)htons(port);


    // create a socket, AF_INET family and SOCK_STREAM = TCP connection 
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Socket creation fialed");
        return -1;
    }

    // bind socket 
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0 ) {
        close(serverSocket);
        perror("Socket binding failed");
        return -1;
    }
    
    // listen socket
    if (listen(serverSocket, 1) < 0) {
        close(serverSocket);
        perror("");
        return -1;
    }   
    return serverSocket;
}

int readline(const int fd, char* buf, const size_t max) {
    size_t i;
    int begin = 1;

    for (i = 0; i < max; i++) {
        char tmp;
        int what = read(fd,&tmp,1);
        if (what == -1)
            return -1;
        if (begin) {
            if (what == 0)
                return recv_nodata;
            begin = 0;
        }
        if (what == 0 || tmp == '\n') {
            buf[i] = '\0';
            return i;
        }
        buf[i] = tmp;
    }
    buf[i] = '\0';
    return i;
}

void daemonize() {
    //  process continues as the daemon
    // // 1. Change working directory to root to avoid issues with unmounting file systems
    // if (chdir("/") < 0) {
    //     perror("chdir error");
    //     exit(EXIT_FAILURE);
    // }

    // 2. Close all file descriptors 
    for (int i = 0; i < getdtablesize(); i++)
        close(i);

    // 3. Reopen the necessary ones
    open("/dev/null", O_RDWR); // stdin, fd = 0 points to bit bucket
    open("out.log", O_WRONLY | O_CREAT | O_APPEND, 0666); // stdout, fd = 1 redirects to output-file
    open("err.log", O_WRONLY | O_CREAT | O_APPEND, 0666); // stderr, fd = 2 redirects to error file

    // 4. Detach from controlling terminal 
    int fd = open("/dev/tty", O_RDWR);
    if (fd == -1) {
        perror("open");
    }
    if (ioctl(fd, TIOCNOTTY, 0) == -1) {
        perror("ioctl");
        close(fd);
    }   
    close(fd);

}

std::vector<char*> tokenize(char* commandBuffer) {
    // tokenize the command buffer into arguments
    char copiedBuffer[MAX_LINE_LENGTH];
    strcpy(copiedBuffer, commandBuffer);

    std::vector<char*> args;
    char* token = strtok(const_cast<char*>(copiedBuffer), " ");
    while (token != nullptr) {
        args.push_back(token);
        token = strtok(nullptr, " ");
    }
    args.push_back(nullptr); // remove null
    return args;
}
