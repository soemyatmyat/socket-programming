#include "a4.h"

// parameterizations
pthread_mutex_t shell_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t shell_cond = PTHREAD_COND_INITIALIZER;
int shell_server_socket = -1; // server_socket
const int timeout = 20000; // 20 seconds 

struct CommandResult {
    std::string response;
    std::string output;
    std::string error;
};

CommandResult execute_shell_command(const char* command) {
    CommandResult result;
    // Create pipes to capture standard output and standard error
    int outputPipe[2];
    int errorPipe[2];
    int responsePipe[2];

    pipe(outputPipe);
    pipe(errorPipe);
    pipe(responsePipe);

    // Fork a child process to execute the command
    pid_t childPid = fork();

    if (childPid == 0) {
        // Redirect stdout and stderr to the pipe
        dup2(outputPipe[1], STDOUT_FILENO);
        dup2(errorPipe[1], STDERR_FILENO);

        // Execute the shell command
        int exitStatus = system(command);
        // std::cout << exitStatus << std::endl;
        // Prepare the response based on the exit status of shell command execution
        std::string response;
        if (WIFEXITED(exitStatus)) {
            int status = WEXITSTATUS(exitStatus);
            if (status == 0) {
                response = "OK " + std::to_string(status) + " Command executed successfully.";
            } else if (status == 127) {
                response = "ERR " + std::to_string(status);

            } else {
                response = "FAIL " + std::to_string(status);
            }
        } 
        const char* response_c = response.c_str(); // convert to c string
        write(responsePipe[1], response_c, strlen(response_c)); // write to pipe
        // Close write ends of the pipes
        close(outputPipe[1]); 
        close(errorPipe[1]); 
        close(responsePipe[1]); 
        exit(0);

    } else {
        // Close write ends of the pipes
        close(outputPipe[1]);
        close(errorPipe[1]);
        close(responsePipe[1]);

        // Wait for the child process to complete
        int exitStatus;
        waitpid(childPid, &exitStatus, 0);

        // Read the output from the pipes
        char buffer[MAX_LINE_LENGTH];

        ssize_t bytesRead;
        while ((bytesRead = read(outputPipe[0], buffer, sizeof(buffer))) > 0) {
            result.output += std::string(buffer, bytesRead);
        }

        while ((bytesRead = read(errorPipe[0], buffer, sizeof(buffer))) > 0) {
            result.error += std::string(buffer, bytesRead);
        }

        while ((bytesRead = read(responsePipe[0], buffer, sizeof(buffer))) > 0) {
            result.response += std::string(buffer, bytesRead);
        }

        if (result.error.size() > 0)
            result.response += " " + result.error;

        // Close read ends of the pipes
        close(outputPipe[0]);
        close(errorPipe[0]);
        close(responsePipe[0]);

    }

    return result;
}

/*
This function manages the shell commands for shell server
*/
void* handle_shell_client(void* arg) {

    // Prep client acceptance
    int client_socket;
    struct sockaddr_in clientAddr;
    unsigned int clientAddr_len = sizeof(clientAddr);

    // Get server socket from argument
    // int server_socket = *((int*)arg);
    static_cast<void>(arg);

    // Create a pollfd structure for the server socket
    struct pollfd pollrec; 
    pollrec.fd = shell_server_socket;
    pollrec.events = POLLIN;

    while (1) {
        // Use Poll with timeout of 20 seconds to check if there is any incoming client
        int polled = poll(&pollrec, 1, timeout);

        if (polled == -1) {
            perror("Error in poll");
        } else if (polled == 0) {
            if (terminate == 1) {
                std::cout << "Shell >> Shell server Terminating. Bye. \n" << std::endl;
                close(shell_server_socket);
                return NULL;
            }
        } else {
            client_socket = accept(shell_server_socket, (struct sockaddr*)&clientAddr, &clientAddr_len);
            if (client_socket < 0) {
                perror("Shell >> Error accepting client connection.");
                continue;
            }

            printf("Shell >> Incoming shell client.\n");
            CommandResult result;
            char commandBuffer[MAX_LINE_LENGTH];
            int n;

            // Receive data
            while ((n = readline(client_socket,commandBuffer,MAX_LINE_LENGTH-1)) != recv_nodata) {
                if (strcmp(commandBuffer, "quit") == 0) {
                    printf("Shell > Received quit, sending EOF.\n"); 
                    shutdown(client_socket, SHUT_RDWR); 
                    close(client_socket);
                    printf("Shell > Outgoing shell client...\n");
                    pthread_mutex_lock(&shell_lock);
                    active_shell_client--;
                    pthread_cond_signal(&shell_cond);
                    pthread_mutex_unlock(&shell_lock);
                    break;
                }
                const char* response;
                if (strcmp(commandBuffer, "CPRINT") == 0) {
                    // concatenate output and response together as one reply
                    // printf("out is %s\n", result.output.c_str());
                    // printf("response is %s\n", result.response.c_str());
                    std::string out = result.output + result.response;
                    if (out.size() == 0) {
                        out = "ERR " + std::to_string(EIO) + " No command has been issued in the current session.";
                    }
                    send(client_socket, out.c_str(), strlen(out.c_str()), 0);
                    send(client_socket, "\n", 1, 0);
                    continue;
                } else {
                    // send command for shell execution
                    result = execute_shell_command(commandBuffer);
                    response = result.response.c_str();
                    send(client_socket, response, strlen(response), 0);
                    send(client_socket, "\n", 1, 0);
                }
            } // serve this client until they lose interest

            // when disconnected by the client 
            if (n == recv_nodata) {
                printf("Shell > Connection closed by client.\n");
                shutdown(client_socket, SHUT_RDWR);
                close(client_socket);
                printf("Shell > Outgoing cllient...\n");
                pthread_mutex_lock(&shell_lock);
                active_shell_client--;
                pthread_cond_signal(&shell_cond);
                pthread_mutex_unlock(&shell_lock);
            }
        }
    }
}

int release_preallocated_threads(int server_socket) {
    // Setting up the thread creation:
    pthread_t tid;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // Spawn thread to handle the shell client
    for (int i = 0; i < max_shell_client; i++)
        if (pthread_create(&tid, &ta, handle_shell_client, &server_socket) != 0) {
            perror("pthread_create");
            return -1;
        }
    return 0;
}

int shell_server(int port, const char* ip_addr) {
    // Put into daemon mode
    if (foreground == 0)
        daemonize();

    // Prep server
    shell_server_socket = createServerSocket(port, ip_addr);
    if (shell_server_socket < 0) {
        perror("Error creating shell server socket");
        return -1;
    }
    std::cout << "Shell Server is up and listening on " << port << std::endl;

    // Go threads, go threads!
    release_preallocated_threads(shell_server_socket);
    
    // Wait for the signal for graceful termination (could only happen for background kill)
    int sig;
    if (sigwait(&signal_set, &sig) == -1) {
        perror("Error waiting for signal");
        return 1;
    }

    std::cout << "Shell >> Received QUIT. Cleaning up and terminating." << std::endl;
    terminate = 1;
    //printf("Shell >> Shell server Terminating. Bye. \n");
    //close(shell_server_socket);

    return 0;
}