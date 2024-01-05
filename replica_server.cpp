#include "a4.h"

int replica_socket = -1; // replica socket
int wait_time = 500; // by default, 5 miliseconds 
int assign_sockets_peers = 0;


struct PeerMessage {
    Peer peer;
    char* commandBuffer;
};

/**
 * Receiving commands from peers
*/

// handler peer request
void* handle_peer(void* arg) {
    int client_socket = *((int*)arg);
    char peerCommand[MAX_LINE_LENGTH];
    int n;
    while ((n = readline(client_socket,peerCommand,MAX_LINE_LENGTH-1)) != recv_nodata) {
        std::cout << "Received message from peer: " << peerCommand << std::endl;
        std::string reply = execute_peer_command(peerCommand);

        // Send acknowledge back to peer:: hard-coding
        if (strncmp(peerCommand, "FREAD", strlen("FREAD")) == 0) {
            const char* response = reply.c_str();
            send(client_socket, response, strlen(response), 0);
        }
        std::cout << "Done executing peer request of: " << peerCommand << std::endl;

    } 
    close(client_socket);
    return NULL; // thread terminates
}

// a thread monitors the replica server and when a peer wants to talk, a new thread is spawn
void* handle_replica_server(void* arg) {

    static_cast<void>(arg);

    // Prep client acceptance
    int client_socket;
    struct sockaddr_in clientAddr;
    unsigned int clientAddr_len = sizeof(clientAddr);

    // Thread initialization 
    pthread_t tid;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);

    while (1) {
        // Accept connection from peers
        client_socket = accept(replica_socket, (struct sockaddr*)&clientAddr, &clientAddr_len); // blocking code
        if (client_socket <0) {
            perror("Error accepting peer connection.");
            return NULL;
        }
        std::cout << "Accepted Peer connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << std::endl;
        

        // Spawn a new thread to handle the peer request 
        if (pthread_create(&tid, &ta, handle_peer, &client_socket) != 0) {
            perror("pthread_create");
            return NULL;
        }

        // Termination is triggered?
        if (terminate == 1) 
            break;

    }

    printf("Replica >> Replica server Terminating. Bye. \n");
    close(replica_socket); // will never reach
    return 0;

}

int replica_server(int port, const char* ip_addr) {
    // Setting up the thread creation:
    pthread_t tid;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // Prep server: util.cpp
    replica_socket = createServerSocket(port, ip_addr);
    if (replica_socket <0) {
        perror("Replica Server Socket Error.");
        return -1;
    }
    std::cout << "Replica Server is up and listening on " << port << std::endl;

    if (pthread_create(&tid, &ta, handle_replica_server, NULL) != 0) {
        perror("pthread_create");
        return -1;
    }
    return 0;
}


/**
 * Transimitting commands to peers
*/

// a thread send the command to peer 
void* send_command_to_peer(void* arg) {
    PeerMessage* pm = static_cast<PeerMessage*>(arg);
    //auto pm = std::shared_ptr<PeerMessage>(static_cast<PeerMessage*>(arg));
    Peer peer = pm->peer;
    int peer_socket = peer.peer_socket;
    char* commandBuffer = pm->commandBuffer;
    if (send(peer_socket, commandBuffer, strlen(commandBuffer), 0) <= 0) {
        perror("Error in trasmitting command to peer");
    }
    // delete pm;

    // hard-coding
    if (strncmp(commandBuffer, "FREAD", strlen("FREAD")) == 0) {
        // get the reply from peers;
        char responseBuffer[MAX_LINE_LENGTH]; 
        memset(responseBuffer, 0, sizeof(responseBuffer)); 

        // Create a pollfd structure for the server socket
        struct pollfd pollpeer; 
        pollpeer.fd = peer_socket;
        pollpeer.events = POLLIN;

        while (1) {
            int polledpeer = poll(&pollpeer, 1, wait_time); 

            if (polledpeer == -1) {
                perror("Error in poll peer");
            } else if (polledpeer == 0) {
                pthread_exit(NULL);
            } else {
                ssize_t bytes_received = recv(peer_socket, responseBuffer, sizeof(responseBuffer), 0);
                if (bytes_received > 0) {
                    std::cout << "Received acknowlege from peer: " << responseBuffer << std::endl;
                    std::vector<char*> args = tokenize(responseBuffer);
                    char* command = args[0];
                    if (strcmp(command, "OK") == 0) {
                        int read_bytes = std::stoi(args[2]); // extract the read bytes from peer 
                        int* read_bytes_pltr = new int(read_bytes); 
                        std::string command = commandBuffer;
                        std::cout << "Done communicating to peer: " << peer.host << " with msg: " << command.substr(0, command.length()-1) << std::endl;
                        pthread_exit(read_bytes_pltr);
                    }
                }
            }
        }


    }
    std::string command = commandBuffer;
    std::cout << "Done communicating to peer: " << peer.host << " with msg: " << command.substr(0, command.length()-1) << std::endl;


    return NULL;
}

void assign_sockets_to_peers() {
    // assign peer sockets (only works if peers are already discovered)
    for (Peer& peer: peers) {
        peer.peer_socket = createClientSocket(peer.port, peer.host.c_str()); 
        std::cout << "Peer: " << peer.host << ", " << peer.peer_socket << std::endl;
    }
    assign_sockets_peers = 1;
}

// spawn threads to concurrrently replicate the command
int replicate_to_peers(char* commandBuffer, int wait) {

    // this needs to only run once, but I don't know whether this is the most appropriate place
    if (assign_sockets_peers == 0) 
        assign_sockets_to_peers();

    pthread_t tpeer;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    if (wait == 0) {
        pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);
    }

    char* reconstructPeerCommand = new char[strlen(commandBuffer) + 2];
    strcpy(reconstructPeerCommand, commandBuffer);
    strcat(reconstructPeerCommand, "\n");

    std::vector<pthread_t> threadIds;
    for (const Peer& peer: peers) {
        if (peer.peer_socket == -1)
            continue;

        PeerMessage* pm = new PeerMessage;
        pm->peer = peer;
        pm->commandBuffer = reconstructPeerCommand;
        std::cout << "Start communicating to peer: " << peer.host << ", " << peer.peer_socket << " with msg: " << commandBuffer << std::endl;
        if (pthread_create(&tpeer, &ta, send_command_to_peer, pm) != 0) {
            perror("pthread_create");
            delete pm; // Cleanup in case of failure
            return -1;
        }
        if (wait == 1)
            threadIds.push_back(tpeer);
    }

    if (wait == 1) {
        int read_bytes_final = 0;
        for (const pthread_t& id : threadIds) { 
            void* threadResult;
            pthread_join(id, &threadResult); // blocking code
            if (threadResult != nullptr) {
                int* result_ptr = static_cast<int*>(threadResult);
                int read_bytes = *result_ptr;
                read_bytes_final += read_bytes;  
                delete result_ptr;
            } 
        }
        return read_bytes_final / threadIds.size();
    }

    
    return 0;
    
}
