# socket-programming (First foray)

A distributed file server (open, read, write, lseek, close files), a TCP program written in C++ with POSIX API for linux. 
- Simulatenous read and writes, handled with mutex and conditional variables. 
- - Read and writes cannot happen at the same time. 
- - Only one write at a time. 
- - Concurrent Reads is ok. 
- Includes peers discovery for synchronization/replication of file status for clients, implemented with UDP broadcasts.


---
## A. Introduction 
This shfd hosts two servers: a regular shell server and a distributed file server on specified ports. 
Command line switches are
- -f port number: specifies the file server port (default 9001)
- -s port number: specifies the shell server port (default 9002)
- -T: max threads: specifies the concurrency limit (default 20), that is, maximum number of concurrent clients to the server 
- -t: increase threads: t threads (default 5) are created/preallocated and put to sleep until a client connects, when there are no more sleeping threads, a t thread is created unless max threads has reached
- -D: delays read and write operations
- -v: enables the verbosity of the program
- -p port number: specifies replication server port. when included as a command line switch, servers handle the replication to peers on the same network 
- -d: disables daemon mode (by default, program goe into a daemon mode)
- -x: enables the peers discovery. Otherwise, for replication to peers, {peer_hostname}:{peer_replication_port} has to be defined when triggering the program.
Program can be launched as ./shfd -s ${port} -f ${port} 

### File Structure
- shfd.cpp, shfd.h: Main Function and Responsible for coordinating between various modules, i.e., Shell Server, File Server, Replica Server
- shell_server.cpp: Constructs a shell server (only accepts local connection and maximum client is 1)
- file_server.cpp: Constructs a file server
- replica_server.cpp: Constructs a replica server and manage the transimitting of commands to peers for synchronization. Critical for distributed shfd system.
- udp.cpp: Constructs a udp listener and broadcast discovery message to peers. Critical for distributed shfd systems.
** Note: udp port listens on 2345 (hard-coded).
- util.cpp, util.h: Helper for common TCP functions, i.e., creating server socket, creating client socket, reading line and more.

### Disconnect Command
quit:: When a client issues a 'quit' command, it terminate the remote shell connection with the server, that is, in the server side, shutdown is called on the clientSocket with SHUT_RDWR to disable both sending and receiving. After that, close is used to close the socket. This signals the client that the server is shutting down, and the client can receive an error indicating the connection has been closed.

In the case where client disconnects without informing the server is also handled on the server side by checking to see whethere there are any incoming data from client.

NOTE: UDP Server is run on port 2345 (hard coded) when command line -x switch is enabled.

---
## B. Implementation Notes

### 1. Servers Initialization
- Shell server is handled by a child process (put into daemon mode by default and only accept local connection)
- File server is handled by a parent process (put into daemon mode by default)
- Replication Server is handled by a thread from the main process in File Server (initialize only when command line switch -p [port] is provided)
- UDP Listening is handled by a thread from the main process in File Server (initialize only when command line switch -x is provided)

### 2. Daemonization and Graceful Termination
#### Daemonize Procedures
By defaults, servers willl go into the daemon mode except changing working directory to root and limiting permissions.
1. When in daemon mode, all the file descriptors are first closed.
2. Then only necessary ones are reopened, namley,
- stdin pointing to /dev/null,
- stdout being redirected to out.log and
- stderr being redirected to err.log for audit and debugging purpose.
3. Program is also detached from the controlling terminal in daemon mode.

#### Graceful Termination
Following signals are considered to be triggerers for Graceful Termination
- SIGQUIT: When in daemon mode, this is triggered with kill -s QUIT [PID] via command line.
- SIGINT: When in attached mode, this is triggered with by pressing Ctrl+C in terminal. Alternativley, this can be triggerd by issuing kill -s INT [PID] via command line.
- SIGTERM: When in daemon mode, this is triggered with kill -s TERM [PID] via command line.

#### Ignore Signals
- SIGNPIPE: Broken pipe. Sent to a process when it attempts to write to a pipe or socket that has been closed. This happened when client closes connection/ close socket. We instruct shfd to Ignore it since it's already handled in the code. This is done so by signal(SIGPIPE, SIG_IGN).

### 3. Concurrency Management for file clients
- Pre-allocated threads manage the file clients
- Every 20 seconds, idle thread considers to be or not to be by evaluating t_pre > t_incr. This 20 seconds wait time is achived by poll function.
Ref: https://man7.org/linux/man-pages/man2/poll.2.html

- Maximum threads to concurrently handle file clients = t_max + t_incr


### 4. Replication/ Synchronization concurrently in distributed system

#### Peer Discovery [Activated with command line -x]
In addition to shell, file and replication server, a thread is monitoring UDP port for broadcasting message.

Upon the first client connection, server broadcasts discovery message to peers on the local shared network using UDP's INADDR_BROADCAST. Discovery message is in the format of [host_ip]:[replication_port]. Subsequently, peers start broadcasting discovery messages, completing the cycle to discover one another.

Initially, &peerAddr was utilized to identify the IP Address of broadcaster, but because of inconsistent results, broadcast message is instead used to get the IP Address of sender and its replication port number.

```
ssize_t bytes_received = recvfrom(udp_listen_socket, commandBuffer, sizeof(commandBuffer), 0, (struct sockaddr*)&peerAddr, &psize);
```


#### Synchronization

1. First, TCP sockets are created for peers. In case of any connection issue, an error message is logged, peer_socket would be assigned to zero and such peer is simply ignored for synchronization.
```
struct Peer {
    std::string host;
    int port;
    int peer_socket;
};
for (Peer& peer: peers) {
    peer.peer_socket = createClientSocket(peer.port, peer.host.c_str());
}
```

2. Then, for synchronization commands,
- the same socket for peer is reused until server terminates, thereby avoiding the expensive procedure of opening a new TCP socket for every synchronization command.
- Though there is a dedicated socket for each peer, for transmitting synchronization command, a new thread (detached) is spawned to achieve concurrency. 
```
struct PeerMessage {
    Peer peer;
    char* commandBuffer;
};
thread_create(&tpeer, &ta, send_command_to_peer, pm)
```
- Replication of FWRITE, FSEEK, FOPEN, FCLOSE are all considered to One-Phase Commit (1PC), that is, server assumes peers carry out the commands as instructed without needing any acknowledgement.
 - Note: Except for FOPEN command, other file commands are reconstucted as FSEEK [filename] [length], FWRITE [filename] [bytes], FREAD [filename] [bytes], FCLOSE [filename]. Rational is each peer operates its own file descriptor table and file descriptor (fd) they return for the file may or may not be the same. Therefore, to avoid ambiguity, filename is used to relay the client's command. Each peer maintains std::unordered_map<int, std::string> fd_to_filename to map filename to fd and vice versa.

- Replication of FREAD, however, involves majority concerns to respond to the client where all peers send back the acknowledge message (success/ fail) with bytes_read to the server.
 - the thread extracts the bytes_read from the peer's reply message and returns the value to the main process. Wait time for peer's reply is maximum 5 miliseconds. (hardcoded)
```
pthread_exit(read_bytes_pltr);
```
 - The server then computes the majority out of the values received plus the value stored locally.
  - Majority is determined as total_read_bytes / total peers > length / total peers. Note: total peers = all the peers on the network including the server itself. 
  - When (total_read_bytes / total peers) < (length / total_peers), majoriy fails to read. In which case, the server responds to the client with a FAIL Message. There is no rollback action involved as it is not mentioned explicitly as a requirement. In practical situation, a rollback would be desired for consistency in synchronization. 
  - If a rollback is required, the code would need to be extended to track the peers successfully read the file and issue a FSEEK command to move the pointer by read_bytes length. 

### 5. Others
#### Verifying file descriptor
To check if the file descriptor is opened in file server for file manipulation commands, fcntl with F_GETFD is used to query the file descriptor's status. Details of the function can be found here: https://man7.org/linux/man-pages/man2/fcntl.2.html

---
## C. Assumptions
Followings are assumed to fulfill the requirements of a3.pdf.
### File Server
1. When delay mode is enabled with command line switch -D, client will wait until the server returns the replies (minimum of 30 seconds (6 *5) + additional 10 buffer seconds) as delay for write is set to 6 seconds and read is set to 3 seconds.
2. Clients opening the same file share the position of the cursor/pointer in the file for reading/ writing as they share the same file descriptor, as per page 2 of a3.pdf.
3. For file reading, the server doesn't notify if it's the end of the file.
4. When one of the clients accessing the file, closes the file, server will close file descriptor associated with the file and none of the clients will be able use that file identifier anymore.
5. We don't do rollback for failures of synchronization. 

---
## D. Description of tests

### (1) Daemonize [pass]
Test case 1: by default, program goes into daemon mode and outputs from program are logged into "out.log" and errors are logged into "err.log". [pass]

### (2) Graceful Termination [pass]
Test case2 : When in attached mode, trigger termination by pressing Ctrl+C in terminal.
Test case 3: When in daemon mode, trigger kill -s QUIT [PID] from command line.
Checked:
- Idle threads terminate
- Active threads serve clients, close sockets and terminate
- Replication server (if active) closes socket and terminates # may not work
- UDP Lister (if active) closes socket and terminates # may not work
- Closed all the descriptors by using getdtablesize

### (3) Shell Server
Test case 4: only accepts local connection [pass]

### (4) File Server

#### (4.1) Concurreny Management (Pre-allocation of threads)
Test case 5: Every 20 seconds, idle threads evaluates to be or not to be. At one time, there can only be a maximum of t_incr idle threads unless t_max + t_incr has reached, in which case, idle thread is zero. [pass]
Test case 6: When there are no more idle threads and t_act < t_max + t_incr, t_incr threads are allocated. [pass]

#### (4.2) Replication/ Synchronization
- Test case 7: Discover peers when command line -x is provided. [pass]
- Test case 8: FOPEN, File open is replicated across distributed system.[pass]
- Test case 9: FCLOSE, File close is replicated across distributed system.[pass]
- Test case 10: FSEEK, File lseek is replicated across distributed system. [pass]
- Test case 11: FWRITE, File write is replicated across distributed system. [pass]
- Test case 12: FREAD, File read is replicated across distributed system. [pass]
- Test case 13: Multiple clients accessing the same file for file manipulation [pass]
    - Scenario A: Read concurrently (Thread-1, Thread-2 and Thread-4), Write (Thread-3) wait for Reads to finish, Write (Thread-5) waits for Thread-3 to finish.
    - Fire commands in sequence with minmum delays
     - Thread-1 > read (3s)
     - Thread-2 > read (3s)
     - Thread-3 > write (5s)
     - Thread-4 > read (3s)
     - Thread-5 > write (5s)

    - Scenario B: Only one write a a time. Thread-2 waits for Thread-1 to release. Thread-3, Thread-4 and Thread-5 also waits. Thread-2 write. After thread-2, Thread-3, Thread-4 and Thread-5 reads concurrently. 
     - Thread-1 > write (6s)
     - Thread-2 > write (6s)
     - Thread-3 > read (3s)
     - Thread-4 > read (3s)
     - Thread-5 > read (3s)
The test suite is complete because the test cases described above, fulfill the requirements of shfd, to the best of my knowledge. 
---


