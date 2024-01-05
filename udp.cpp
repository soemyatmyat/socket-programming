#include "shfd.h"

int broadcasted = 0;
int udp_listen_socket = -1; // udp socket
std::string host_ip = "";


int hostExists(const std::vector<Peer>& peers, const std::string& host) {
    for (const auto& peer : peers) {
        if (peer.host == host) {
            return 1; // Host found in the vector
        }
    }
    return 0; // Host not found in the vector
}

void getHostIP() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        std::cerr << "Error getting hostname" << std::endl;
    }

    struct hostent* host_info = gethostbyname(hostname);
    if (host_info == nullptr) {
        std::cerr << "Error getting host information" << std::endl;
    }

    struct in_addr** address_list = reinterpret_cast<struct in_addr**>(host_info->h_addr_list);
    for (int i = 0; address_list[i] != nullptr; ++i) {
        std::cout << "Server IP Address: " << inet_ntoa(*address_list[i]) << std::endl;
        host_ip = inet_ntoa(*address_list[i]);
    }
}

// Thread monitors udp broadcast
void* listen_broadcast(void* arg) {
    // Listen for the broadcast message
    // int udp_server = *((int*)arg);
    static_cast<void>(arg);

    struct sockaddr_in peerAddr; socklen_t psize;
    char commandBuffer[MAX_LINE_LENGTH];
    
    while (1) {

        for (Peer peer: peers) {
            std::cout << "Peer: " << peer.host << ", " << peer.port << ", " << peer.peer_socket << std::endl;
        }   
        ssize_t bytes_received = recvfrom(udp_listen_socket, commandBuffer, sizeof(commandBuffer), 0, (struct sockaddr*)&peerAddr, &psize);
        if (bytes_received > 0) {
            // std::string peer_ip = inet_ntoa(peerAddr.sin_addr);
            std::string commandBuf = commandBuffer;
            size_t colonPos = commandBuf.find(':');
            if (colonPos != std::string::npos) {
                std::string peer_ip = commandBuf.substr(0, colonPos);
                int peer_port = std::stoi(commandBuf.substr(colonPos+1));

                std::cout << "Received broadcasted message from peer: " << peer_ip << "." << std::endl;

                if (!hostExists(peers, peer_ip) && peer_ip != host_ip) {
                    // populate the peer list
                    Peer peer;
                    peer.host = peer_ip;
                    peer.port = peer_port;
                    peers.push_back(peer);
                }
            }
            
            if (broadcasted == 0) {
                std::string message = std::to_string(replica_port);
                udp_broadcast(udp_port, message); // peers discover one other
            }

            // Termination is triggered? // may not reach
            if (terminate == 1) 
                break;
        } else {
            perror("Error receiving recvfrom");
        }
    }

    

    // close the socket (will never reach)
    std::cout << "UDP Listener >> UPP Listener Terminating. Bye. "<< std::endl;
    close(udp_listen_socket);
    return NULL; // thread terminates
}

// Spawn a thread to camp at UDP port and listen for broadcast
int udp_listener(int port) {
    getHostIP();
    // Setting up the thread creation:
    pthread_t tid;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // From util.cpp
    udp_listen_socket = createUnConnectUDPSocket();
    if (udp_listen_socket == -1)
        return -1;

    // Set up address
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = (unsigned short)htons(port);

    // Bind the socket to the address and port
    if (bind(udp_listen_socket, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
        std::cerr << "Error binding socket\n";
        close(udp_listen_socket);
        return -1;
    }

    std::cout << "UDP Server is up and listening on " << udp_port << std::endl;
    // Spawn thread to monitor the incoming broadcast    
    if (pthread_create(&tid, &ta, listen_broadcast, &udp_listen_socket) != 0) {
        perror("pthread_create");
        return -1;
    }
    return 0; // parent process exit, bye thread
}

// Create a UPD Socket and Broadcast a message to peers on the same network
int udp_broadcast(int port, std::string message) {

    // From util.cpp
    int udp_client = createUnConnectUDPSocket();
    if (udp_client == -1)
        return -1;

    // Enable broadcast
    int broadcastEnable = 1;
    setsockopt(udp_client, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    // Specify the server's address and port 
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_BROADCAST; 
    serverAddr.sin_port = (unsigned short)htons(port);

    message = host_ip + ":" + message;
    ssize_t sentBytes = sendto(udp_client, message.c_str(), strlen(message.c_str()), 0, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr));
    if (sentBytes == -1) {
        std::cerr << "Error sending discovery message: " << strerror(errno) << std::endl;
        return -1;
    }

    std::cout << "Broadcasted discovery message to potential peers.\n";

    // Close the socket
    close(udp_client); // job done
    broadcasted = 1; // 

    return 0;

}