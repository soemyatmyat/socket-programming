#include "shfd.h"
#include <arpa/inet.h>

// parameterizations
const int read_delay = 3; // 3 seconds delay in read
const int write_delay = 6; // 6 seconds delay in write
const int timeout = 20000; // 20 seconds 
int file_server_socket = -1; // server_socket

// Mutex to keep track of active threads
int t_pre = 0; // preallocated threads
int t_act = 0; // threads currently serving clients
pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

// Mutex to protect file access
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<int, std::string> fd_to_filename;
std::unordered_map<std::string, int> filename_to_index;
std::vector<pthread_mutex_t*> write_mutex_arr;
std::vector<pthread_mutex_t*> read_mutex_arr;
std::vector<pthread_cond_t*> write_pass_arr;
std::vector<pthread_cond_t*> read_pass_arr;
std::vector<int> active_readers_arr;
std::vector<int> writing_arr;

// file usage message:
const std::string file_usage = "Usage: FOPEN [filename]\nFSEEK [identifier] [offset]\nFREAD [identifier] [length]\nFWRITE [identifier] [bytes]\nFCLOSE [identifier]\n";

struct CommandResult {
    std::string response;
    std::string output;
    std::string error;
};

int get_fd_from_filename(std::string filename) {
    pthread_mutex_lock(&lock);
    for (const auto& entry : fd_to_filename) {
        if (entry.second == filename) {
            int fd = entry.first;
            pthread_mutex_unlock(&lock);
            return fd;
        }
    }
    pthread_mutex_unlock(&lock);
    return -1;
}

std::string get_filename_of_fd(int fd) {
    std::string filename = "";
    if (fd_to_filename.find(fd) != fd_to_filename.end()) {
        filename = fd_to_filename[fd];
    }
    return filename;
}

CommandResult openFile(std::string filename) {
    CommandResult result;

    // check if this filename has already been opened
    int fd = get_fd_from_filename(filename);
    if (fd != -1) {
        result.response = "ERR " + std::to_string(fd) + " File already opened by another client.";
        return result;
    }
    
    // otherwise
    fd = open(filename.c_str(), O_RDWR | O_CREAT , S_IRUSR | S_IWUSR);
    if (fd == -1) {
        const char* err_msg = strerror(errno);
        result.response = "FAIL " + std::to_string(errno) + " " + err_msg;
    } else {
        pthread_mutex_lock(&lock); // lock before modifying filename_to_index 
        fd_to_filename[fd] = filename; // store filename to fd 
        if (filename_to_index.find(filename) == filename_to_index.end()) {
            filename_to_index[filename] = filename_to_index.size(); // assign index for each file

            int index = filename_to_index[filename];
            active_readers_arr.push_back(0); // each file will their own mutexes, conditional variables, active_readers and writing
            writing_arr.push_back(0);
            write_mutex_arr.push_back(new pthread_mutex_t);
            pthread_mutex_init(write_mutex_arr[index], NULL);
            read_mutex_arr.push_back(new pthread_mutex_t);
            pthread_mutex_init(read_mutex_arr[index], NULL);
            write_pass_arr.push_back(new pthread_cond_t);
            pthread_cond_init(write_pass_arr[index], NULL);
            read_pass_arr.push_back(new pthread_cond_t);
            pthread_cond_init(read_pass_arr[index], NULL);
            
        }
        pthread_mutex_unlock(&lock);
        result.response = "OK " + std::to_string(fd) + " File Open.";
    }
    return result;
}

CommandResult closeFile(int fd) {
    CommandResult result;
    // Call fcntl with F_GETFD to query the file descriptor's status.
    int check = fcntl(fd, F_GETFD);
    // If fcntl returns -1, an error occurred (file descriptor doesn't exist).
    if (check == -1) {
        const char* err_msg = strerror(ENOENT);
        result.response = "ERR " + std::to_string(ENOENT) + " " + err_msg + ": " + std::to_string(fd) + ".";
    } else {
        if (close(fd) == -1) {
            const char* err_msg = strerror(errno);
            result.response = "FAIL " + std::to_string(errno) + " " + err_msg + ": " + std::to_string(fd) + ".";
        } else {
            fd_to_filename.erase(fd); // remove fd from fd_to_filename
            result.response = "OK 0 File Closed for " + std::to_string(fd) + ".";
        }
    }
    return result;
}

CommandResult readFile(int fd, int length) {
    CommandResult result;
    // Call fcntl with F_GETFD to query the file descriptor's status.
    int check = fcntl(fd, F_GETFD);
    // If fcntl returns -1, an error occurred (file descriptor doesn't exist).
    if (check == -1) {
        const char* err_msg = strerror(ENOENT);
        result.response = "ERR " + std::to_string(ENOENT) + " " + err_msg + ": " + std::to_string(fd) + ".";
    } else {

        std::string filename = get_filename_of_fd(fd);
        int index = filename_to_index[filename];
        // if there is a writer, wait for read_pass
        
        while (writing_arr[index] != 0) {
            pthread_mutex_lock(write_mutex_arr[index]); 
            //printf("Waiting for read pass...\n");
            pthread_cond_wait(read_pass_arr[index], write_mutex_arr[index]);
            pthread_mutex_unlock(write_mutex_arr[index]);
        }

        // updating active_readers counter 
        pthread_mutex_lock(read_mutex_arr[index]);
        active_readers_arr[index]++;
        pthread_mutex_unlock(read_mutex_arr[index]);

        // reading 
        char buffer[length + 1]; // extra space for null-termination
        if (delay == 1) {
            // transmit beginning of a read opration to the standard output
            printf("%lu: %s >> Reading starts...\n", (unsigned long)pthread_self(), filename.c_str());
        }
        ssize_t bytesRead = read(fd, buffer, length); // read until length
        if (delay == 1) {
            // transmit end of a read operation to the standard output   
            printf("%lu: %s >> Sleeping for %ds\n", (unsigned long)pthread_self(), filename.c_str(), read_delay);      
            sleep(read_delay);
            printf("%lu: %s >> Reading ends...\n", (unsigned long)pthread_self(), filename.c_str());  
        }

        // updating active_readers counter and notify wirte pass availability
        pthread_mutex_lock(read_mutex_arr[index]);
        active_readers_arr[index]--; // decrease the read count
        if (active_readers_arr[index] == 0) { // signal the write_cv if there are no readers anymore
            pthread_cond_signal(write_pass_arr[index]); // notify write pass availablity
        }
        pthread_mutex_unlock(read_mutex_arr[index]); 

        //if (bytesRead > 0) {
        buffer[sizeof(buffer)-1] = '\0'; // null-terminated 
        result.response = "OK " + std::to_string(length) + " " + std::to_string(bytesRead) + ".";
        //} else 
    }
    return result;
}

CommandResult writeFile(int fd, char* to_write) {
    CommandResult result;
    // Call fcntl with F_GETFD to query the file descriptor's status.
    int check = fcntl(fd, F_GETFD);
    // If fcntl returns -1, an error occurred (file descriptor doesn't exist).
    if (check == -1) {
        const char* err_msg = strerror(ENOENT);
        result.response = "ERR " + std::to_string(ENOENT) + " " + err_msg + ": " + std::to_string(fd) + ".";
    } else {
        // to_write[strlen(to_write)-1] = '\0'; // replace newline with null terminator
        std::string filename = get_filename_of_fd(fd);
        int index = filename_to_index[filename];
        // acquire both read and write locks (no read can happen when someone is writing)
        pthread_mutex_lock(write_mutex_arr[index]);
        // if there is a writer or active readers, wait for write pass
        while (active_readers_arr[index] != 0 || writing_arr[index] != 0) {
            pthread_cond_wait(write_pass_arr[index], write_mutex_arr[index]); 
        }

        writing_arr[index] = 1; // (got the pass) going to write 
        // printf("Active writer: %d\n", writing);
        pthread_mutex_unlock(write_mutex_arr[index]); // release the write lock

        // write happens
        
        if (delay == 1) {
            // transmit beginning of a read opration to the standard output
            printf("%lu: %s >> Writing starts...\n", (unsigned long)pthread_self(), filename.c_str());
        }
        ssize_t bytesWritten = write(fd, to_write, sizeof(to_write));
        if (delay == 1) {
            // transmit beginning of a read opration to the standard output
            printf("%lu: %s >> Sleeping for %ds\n", (unsigned long)pthread_self(), filename.c_str(), write_delay);      
            sleep(write_delay);
            printf("%lu: %s >> Writing ends...\n", (unsigned long)pthread_self(), filename.c_str());
        }
        writing_arr[index] = 0; // writing done
        
        // printf("Releasing passes from write Function\n");
        pthread_cond_signal(write_pass_arr[index]); // notify write pass availability (one at a time, only one write can happen)
        pthread_cond_broadcast(read_pass_arr[index]); // broadcast read pass availability as read can occur simutaneously

        if (bytesWritten == -1) {
            const char* err_msg = strerror(errno);
            result.response = "FAIL " + std::to_string(errno) + " " + err_msg + ": " + std::to_string(fd) + ".";
        } else {
            result.response = "OK 0 Text Written.";
        }
    }
    return result;
}

CommandResult seekFile(int fd, int offset) {
    CommandResult result;
    // Call fcntl with F_GETFD to query the file descriptor's status.
    int check = fcntl(fd, F_GETFD);
    // If fcntl returns -1, an error occurred (file descriptor doesn't exist).
    if (check == -1) {
        const char* err_msg = strerror(ENOENT);
        result.response = "ERR " + std::to_string(ENOENT) + " " + err_msg + ": " + std::to_string(fd) + ".";
    } else {
        off_t newOffset = lseek(fd, offset, SEEK_CUR); 
        if (newOffset == -1) {
            const char* err_msg = strerror(errno);
            result.response = "FAIL " + std::to_string(errno) + " " + err_msg + ": " + std::to_string(fd) + ".";
        } else {
            result.response = "OK 0 Moved the current position in the file (" + std::to_string(fd) + "), it's now at " + std::to_string(newOffset) + ".";
        }
    }
    return result;
}

// Manages the file commands
CommandResult executeFileCommand(char* commandBuffer) {
    CommandResult result;
    std::vector<char*> args = tokenize(commandBuffer); // tokenize the commands

    char* command = args[0]; // the first argument is command 
    int argc = args.size(); // subsequent arguments are parameters

    // command check
    if (strcmp(command, "FOPEN") == 0) { // FOPEN filename
        if (argc != 3) {
            result.response = "illegal command!\n";
            result.response += file_usage;
        } else {
            std::string filename = args[1];
            if (replica_port != 0) 
                replicate_to_peers(commandBuffer); 

            result = openFile(filename);
        }
    } else if (strcmp(command, "FSEEK") == 0) { // FSEEK identifier offset
        if (argc != 4) {
            result.response = "illegal command!\n";
            result.response += file_usage;
        } else {
            try {
                int fd = std::stoi(args[1]);
                int offset = std::stoi(args[2]);

                if (replica_port != 0) {
                    std::string filename = get_filename_of_fd(fd);
                    std::string length_str = args[2];
                    if (filename != "" ) {
                        const char* prefix = "FSEEK ";
                        char* reconstructCommand = new char[strlen(prefix) + filename.length() + length_str.length()+ 1];
                        strcpy(reconstructCommand, prefix);
                        strcat(reconstructCommand, filename.c_str());
                        strcat(reconstructCommand, " ");
                        strcat(reconstructCommand, args[2]);
                        replicate_to_peers(reconstructCommand);
                    }
                }
                result = seekFile(fd, offset);
            } catch (const std::invalid_argument& e) {
                result.response = "illegal command!\n";
                result.response += file_usage;
            }
        }
    } else if (strcmp(command, "FREAD") == 0) { // FREAD identifier length
        if (argc != 4) {
            result.response = "illegal command!\n";
            result.response += file_usage;
        } else {
            int fd = std::stoi(args[1]);
            int length = std::stoi(args[2]);
            std::string length_str = args[2];
            result = readFile(fd, length);

            if (replica_port != 0) { // when replication is up
                std::string filename = get_filename_of_fd(fd);
                if (filename != "" ) {
                    const char* prefix = "FREAD ";
                    char* reconstructCommand = new char[strlen(prefix) + filename.length() + length_str.length() + 1];
                    strcpy(reconstructCommand, prefix);
                    strcat(reconstructCommand, filename.c_str());
                    strcat(reconstructCommand, " ");
                    strcat(reconstructCommand, length_str.c_str());
                    int peers_readBytes = replicate_to_peers(reconstructCommand, 1);

                    //std::cout << "Peer's readBytes: " << read_bytes_arr << std::endl;

                    // Get Local read_bytes
                    std::string response = result.response;
                    char* charPtr = new char[response.size() + 1];
                    strcpy(charPtr, response.c_str());
                    std::vector<char*> args = tokenize(charPtr);
                    char* command = args[0];

                    int read_bytes = 0;
                    if (strcmp(command, "OK") == 0) {
                        try {
                            read_bytes = std::stoi(args[2]);
                        } catch (const std::invalid_argument& e) {
                            std::cerr << "Invalid format in interpreting response of read bytes in local." << std::endl;
                        }
                    }
                    // Then we compute majority votes 
                    if (peers_readBytes != read_bytes) { // peers_readBytes == read_Bytes <= OK
                        int final_readBytes = (peers_readBytes + read_bytes) / 2;
                        if (final_readBytes != length) { // (peer_readBytes + read_Bytes / 2) = length <= OK
                            int peers_size = peers.size() + 1; // how many peers 
                            int margin = length / peers_size; // acceptable if majoriy is equal to length 
                            if (final_readBytes < margin) {
                                result.response = "FAIL -1 SYNC Issue";
                            }

                        }
                    }

                } // if filename is not empty 

            } // sync computation
        }
    } else if (strcmp(command, "FWRITE") == 0) { // FWRITE identifer bytes
        if (argc != 4) {
            result.response = "illegal command!\n";
            result.response += file_usage;
        } else {
            int fd = std::stoi(args[1]);
            char* write_bytes = args[2];

            if (replica_port != 0) {
                std::string filename = get_filename_of_fd(fd);
                std::string write_bytes_str = args[2];
                const char* prefix = "FWRITE ";
                char* reconstructCommand = new char[strlen(prefix) + filename.length() + write_bytes_str.length() + 1];
                strcpy(reconstructCommand, prefix);
                strcat(reconstructCommand, filename.c_str());
                strcat(reconstructCommand, " ");
                strcat(reconstructCommand, write_bytes);
                replicate_to_peers(reconstructCommand);
            }
            result = writeFile(fd, write_bytes);
        }
    } else if (strcmp(command, "FCLOSE") == 0) { // FCLOSE identifier

        if (argc != 3) {
            result.response = "illegal command!\n";
            result.response += file_usage;
        } else {
            int fd = std::stoi(args[1]);
            if (replica_port != 0) { // handle with threads
                // get filename for replication
                std::string filename = get_filename_of_fd(fd);
                if (filename != "" ) {
                    const char* prefix = "FCLOSE ";
                    char* reconstructCommand = new char[strlen(prefix) + filename.length() + 1];
                    strcpy(reconstructCommand, prefix);
                    strcat(reconstructCommand, filename.c_str());
                    replicate_to_peers(reconstructCommand);
                }
            }
            result = closeFile(fd);

        }
        
    } else { // unknown command
        result.response = "illegal command: "+ std::string(command) + "\n";
        result.response += file_usage;
    }
    return result;
}

std::string execute_peer_command(char* commandBuffer) { 
    CommandResult result;
    // Tokenize
    std::vector<char*> args = tokenize(commandBuffer);
    std::cout << "Executing peer request of: " << commandBuffer << std::endl;
    char* command = args[0];

    if (strcmp(command, "FOPEN") == 0) { // FOPEN filename
        std::string filename = args[1];
        result = openFile(filename);
    } else if (strcmp(command, "FCLOSE") == 0) { 
        //convert filename to fd
        std::string filename  = args[1];
        int fd = get_fd_from_filename(filename);
        result = closeFile(fd);
    } else if (strcmp(command, "FSEEK") == 0) {
        //convert filename to fd
        std::string filename  = args[1];
        int fd = get_fd_from_filename(filename);
        int offset = std::stoi(args[2]);
        result = seekFile(fd, offset);
    } else if (strcmp(command, "FWRITE") == 0) {
        //convert filename to fd
        std::string filename  = args[1];
        char* write_bytes = args[2];
        int fd = get_fd_from_filename(filename);
        result = writeFile(fd, write_bytes);
    } else if (strcmp(command, "FREAD") == 0) {
        //convert filename to fd
        std::string filename  = args[1];
        int length = std::stoi(args[2]);
        int fd = get_fd_from_filename(filename);
        result = readFile(fd, length);
    }

    std::cout << "Peer requst executed: " << result.response << std::endl;

    return result.response;
}

int preallocate_threads_file_client();

// waits for client connection and manages the commands for file server
void* handle_file_client(void* arg) {

    // Update Pre-allocated threads count
    pthread_mutex_lock(&file_lock);
    t_pre++;
    pthread_mutex_unlock(&file_lock);

    // Prep client acceptance
    int client_socket;
    struct sockaddr_in clientAddr;
    unsigned int clientAddr_len = sizeof(clientAddr);

    // Get server socket from argument
    // int server_socket = *((int*)arg);
    static_cast<void>(arg);

    // Create a pollfd structure for the server socket
    struct pollfd pollrec; 
    pollrec.fd = file_server_socket;
    pollrec.events = POLLIN;

    // Non-blocking with Poll 
    while(1) {

        // Use Poll with timeout of 20 seconds to check if there is any incoming client
        int polled = poll(&pollrec, 1, timeout);

        if (polled == -1) {
            perror("Error in poll");
        } else if (polled == 0) { // 20 seconds is up: // Evaluate to be or not to be
            pthread_mutex_lock(&file_lock);

            if (terminate == 1) {
                t_pre--;
                pthread_mutex_unlock(&file_lock);
                return NULL; // bye
            }
            if (t_pre > t_incr) {
            // if (t_pre > t_act + t_incr) {  // there should always be t_incr idle threads 
                t_pre--; 
                pthread_mutex_unlock(&file_lock);
                return NULL; // not to be, bye
            }
            pthread_mutex_unlock(&file_lock);

        } else { // someone is coming
            client_socket = accept(file_server_socket, (struct sockaddr*)&clientAddr, &clientAddr_len);
            if (client_socket < 0) {
                std::cerr << "File >> Error accepting client connection." << std::endl;
                return NULL;
            }

            // Update Active threads count 
            std::cout << "Accepted File client connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << std::endl;
            pthread_mutex_lock(&file_lock);
            t_act++;
            t_pre--;
            if (t_pre == 0 && t_act < t_max + t_incr) {
                // release batch of t_incr 
                preallocate_threads_file_client();
            }
            pthread_mutex_unlock(&file_lock);

            // client incoming but no peers yet, start discovering NOW
            if (t_act > 0 && replica_port !=0 && discover_peers == 1 && peers.size() == 0) {
                std::string DISCOVERY_MESSAGE = std::to_string(replica_port);
                udp_broadcast(udp_port, DISCOVERY_MESSAGE); 
            }

            // Receive data from the client 
            CommandResult result;
            char commandBuffer[MAX_LINE_LENGTH];
            int n;
            while ((n = readline(client_socket,commandBuffer,MAX_LINE_LENGTH-1)) != recv_nodata) {
                // command: quit
                if (strcmp(commandBuffer, "quit") == 0) {
                    printf("File >> Received quit, sending EOF.\n");
                    shutdown(client_socket, SHUT_RDWR); 
                    close(client_socket);
                    printf("File >> Outgoing file client...\n");
                    pthread_mutex_lock(&file_lock);
                    t_act--;
                    pthread_mutex_unlock(&file_lock);
                    // return NULL;
                    break;
                }
                // carry out the file command
                try {
                    result = executeFileCommand(commandBuffer);
                } catch (const std::invalid_argument& e) { // catch any invalid arguments
                    result.response = "illegal command!\n";
                    result.response += file_usage;
                }

                // send the response to client from executing file command
                const char* response = result.response.c_str();
                send(client_socket, response, strlen(response), 0);
                send(client_socket, "\n", 1, 0);

                // after serving client, check if termination is triggered?
                if (terminate == 1) {
                    response = "Connection closed by server. Bye.";
                    send(client_socket, response, strlen(response), 0);
                    send(client_socket, "\n", 1, 0);
                    printf("File >> Connection closed by server.\n");
                    shutdown(client_socket, SHUT_RDWR);
                    close(client_socket);
                    printf("File >> Outgoing file client...\n");
                    pthread_mutex_lock(&file_lock);
                    t_act--; // no more active 
                    t_pre--; // not to be 
                    pthread_mutex_unlock(&file_lock);
                    return NULL; // bye
                }
                    
            } // receive data from the client till client loses interest 

            // when disconnected by the client 
            if (n == recv_nodata) {
                printf("File >> Connection closed by file client.\n");
                shutdown(client_socket, SHUT_RDWR);
                close(client_socket);
                printf("File >> Outgoing file client...\n");
                pthread_mutex_lock(&file_lock);
                t_act--; // no more active
                t_pre++; // go back to wait for next client
                pthread_mutex_unlock(&file_lock);

            }


        } // someone is coming

    } // while: wait for client
    return NULL;
}

int preallocate_threads_file_client() {
    // Setting up the thread creation:
    pthread_t tid;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // Spawn threads to take care of file clients 
    for (int i = 0; i < t_incr; i++)
        if (pthread_create(&tid, &ta, handle_file_client, NULL) != 0) {
            perror("pthread_create");
            return -1;
        }

    return 0;
}

int file_server(int port, const char* ip_addr) {

    // Put into daemon mode: util.cpp 
    if (foreground == 0)
        daemonize();

    // Prep server: util.cpp
    file_server_socket = createServerSocket(port, ip_addr);
    if (file_server_socket < 0) {
        perror("File Server Socket Error.");
        return -1;
    }
    std::cout << "File Server is up and listening on " << port << std::endl;

    // Replica server
    if (replica_port != 0) {
        replica_server(replica_port, ip_addr);
    }

    // Spawn a thread to listen on UDP Port
    if (discover_peers == 1) {
        udp_listener(udp_port); 
    }

    // Threads handle file clients, Go threads, go threads!
    preallocate_threads_file_client();

    // Now, parent process just wait for the signal for graceful termination
    int sig;
    if (sigwait(&signal_set, &sig) == -1) {
        perror("Error waiting for signal");
        return 1;
    }

    std::cout << "File >> Received QUIT. Cleaning up and terminating." << std::endl;
    terminate = 1;

    // Wait until active threads become zero 
    while (1) {
        sleep(15);
        pthread_mutex_lock(&file_lock);
        printf("in file server process: \n");
        printf("t_act: %d\n", t_act);
        printf("t_pre: %d\n", t_pre);
        if (t_act <= 0 && t_pre <= 0) {
            pthread_mutex_unlock(&file_lock);
            break;
        }
        pthread_mutex_unlock(&file_lock);
    }


    printf("File >> File server Terminating. Bye. \n");
    // Close all the descriptors
    for (int i = 0; i < getdtablesize(); i++)
        close(i);
    
    // Then, close file server socket
    close(file_server_socket); // will reach now. 

    /*
    while (1) {
        sleep(10);
    }
    */
    
    return 0;
}
