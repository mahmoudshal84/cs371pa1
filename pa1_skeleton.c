/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here
# Student #1: 
# Student #2:
# Student #3: 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    // Add the socket to epoll instance for monitoring
    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    
    epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event);
    
    // Initialize tracking variables
    data->total_rtt = 0;
    data->total_messages = 0;
    
    // Main loop for sending messages
    for (int i = 0; i < num_requests; i++) {
        // Start timing
        gettimeofday(&start, NULL);
        
        // Send the message
        send(data->socket_fd, send_buf, MESSAGE_SIZE, 0);
        
        // Wait for response
        int ready = epoll_wait(data->epoll_fd, events, MAX_EVENTS, 3000);
        if (ready < 0) {
            if (errno != EINTR) {
                printf("epoll failed: %s\n", strerror(errno));
                break;
            }
            continue; // retry on interruption
        }
        
        if (ready == 0) {
            continue;
        }
        
        // Read response
        int n = recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0);
        if (n <= 0) {
            if (n < 0) perror("recv");
            break;
        }
        
        // Record time and do calculations
        gettimeofday(&end, NULL);
        
        long long seconds_diff = end.tv_sec - start.tv_sec;
        long long micros_diff = end.tv_usec - start.tv_usec;
        long long rtt = seconds_diff * 1000000 + micros_diff;
        
        // Accumulate stats
        data->total_rtt += rtt;
        data->total_messages++;
        
        if (i == 0 || i == 100 || i == 1000 || i == 10000 || i == 100000 || i == num_requests-1) {
            printf("Thread #%d: %d msgs, last RTT: %lld us\n", 
                   data->socket_fd % 100, i+1, rtt); // hacky thread ID
        }
    }
    
    // Calculate rate
    if (data->total_messages > 0) {
        double seconds = data->total_rtt / 1000000.0;
        data->request_rate = data->total_messages / seconds;
    } else {
        data->request_rate = 0;
    }

    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    struct sockaddr_in server_addr;
    long long total_rtt = 0;
    long total_messages = 0;
    float total_request_rate = 0.0;

    printf("Client starting with %d threads\n", num_client_threads);
    
    // Set up address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    // Convert IP string to binary
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        printf("Bad IP address\n");
        return;
    }
    
    // Create client threads with inconsistent variable style
    int i;
    for (i = 0; i < num_client_threads; i++) {
        // Create socket for this thread with minimal error checking
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            return;
        }
        
        // Try to connect
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect");
            close(sock);
            return;
        }
        
        // Create epoll instance
        int epfd = epoll_create1(0);
        if (epfd == -1) {
            printf("epoll_create1 failed\n");
            close(sock);
            return;
        }
        
        // Store socket and epoll fd in thread data
        thread_data[i].socket_fd = sock;
        thread_data[i].epoll_fd = epfd;
        
        // Excess print statement that could be cut
        printf("Thread %d ready\n", i);
    }
    
    // Start threads
    for (i = 0; i < num_client_threads; i++) {
        // Different variable style in different loop
        int result = pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
        if (result != 0) {
            printf("Thread creation failed: %s\n", strerror(result));
        }
    }
    
    // Wait for threads
    for (int j = 0; j < num_client_threads; j++) {
        pthread_join(threads[j], NULL);
    }
    
    // Print stats
    printf("\n----- Results -----\n");
    printf("Thread stats:\n");
    for (i = 0; i < num_client_threads; i++) {
        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
        
        printf("Thread %d:\n", i);
        printf("  Messages: %ld\n", thread_data[i].total_messages);
        
        if (thread_data[i].total_messages > 0) {
            printf("  Avg RTT: %.2f ms\n", (double)thread_data[i].total_rtt / 
                   (thread_data[i].total_messages * 1000.0));
            printf("  Rate: %.2f req/s\n", thread_data[i].request_rate);
            
            double avg_time = (double)thread_data[i].total_rtt / 
                            (thread_data[i].total_messages * 1000000.0);
            double prod = thread_data[i].request_rate * avg_time;
            printf("  RPS×T = %f\n", prod);
        }
        
        close(thread_data[i].socket_fd);
        close(thread_data[i].epoll_fd);
    }
    
    // Print overall stats
    printf("\nOverall:\n");
    printf("Average RTT: %lld us\n", total_rtt / total_messages);
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {
    int server_fd, epoll_fd;
    struct sockaddr_in server_addr;
    struct epoll_event event, events[MAX_EVENTS];
    
    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return;
    }
    
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    
    // Configure address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    if (strcmp(server_ip, "0.0.0.0") == 0) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    }
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return;
    }
    
    // Listen
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        return;
    }
    
    // Create epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return;
    }
    
    // Add server socket to epoll
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        close(server_fd);
        close(epoll_fd);
        return;
    }
    
    printf("Server running on %s:%d\n", server_ip, server_port);
    int client_no = 0;

    /* Server's run-to-completion event loop */
    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            perror("epoll_wait");
            break;
        }
        
        int x;
        for (x = 0; x < n; x++) {
            if (events[x].data.fd == server_fd) {
                // Accept new connection
                struct sockaddr_in cli_addr;
                socklen_t addr_size = sizeof(cli_addr);
                
                int conn_sock = accept(server_fd, (struct sockaddr *)&cli_addr, &addr_size);
                if (conn_sock < 0) {
                    perror("accept");
                    continue;
                }
                
                // Set socket non-blocking
                int flags = fcntl(conn_sock, F_GETFL, 0);
                fcntl(conn_sock, F_SETFL, flags | O_NONBLOCK);
                
                // Add to epoll
                event.events = EPOLLIN | EPOLLET;
                event.data.fd = conn_sock;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event);
                
                // Print client info with C-style string handling
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &cli_addr.sin_addr, ip_str, sizeof(ip_str));
                client_no++;
                printf("Client %d connected from %s\n", client_no, ip_str);
                
            } else {
                // Handle data
                int client_fd = events[x].data.fd;
                char buf[MESSAGE_SIZE];
                
                // Receive data
                int bytes = recv(client_fd, buf, sizeof(buf), 0);
                
                if (bytes <= 0) {
                    if (bytes < 0)
                        perror("recv");
                    
                    // Close connection
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    printf("Client disconnected\n");
                    continue;
                }
                
                // Echo back
                send(client_fd, buf, bytes, 0);
            }
        }
    }
    
    close(epoll_fd);
    close(server_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}