#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

#include "loadbalancer.h"

#define DEBUG

#ifdef DEBUG
#define DEBUG_POLL_TIMEOUT_IN_MS 10000
#endif

server_t *servers[MAX_SERVERS];

/**
 * @brief Initializes a new server_t structure and returns a pointer to it.
 *
 * @param server_index Index of the server in the servers array.
 * @param max_connections Maximum allowed connections for the server.
 * @return Pointer to the initialized server_t structure.
 */
server_t *init_server(int server_index, int max_connections) {
    server_t *server = malloc(sizeof(server_t));
    servers[server_index] = server;

    pthread_mutex_init(&server->server_details.lock, NULL);
    pthread_mutex_init(&server->connection_details.lock, NULL);
    pthread_mutex_init(&server->server_pollin.lock, NULL);

    pthread_mutex_lock(&server->connection_details.lock);
    server->connection_details.num_connections = 0;
    server->connection_details.max_connections = max_connections;
    pthread_cond_init(&server->connection_details.poll_connections_cv, NULL);
    pthread_mutex_unlock(&server->connection_details.lock);

    pthread_mutex_lock(&server->server_pollin.lock);
    server->server_pollin.client_pollin_fds = malloc(sizeof(struct pollfd) * max_connections);
    server->server_pollin.assigned_clients = malloc(sizeof(client_t *) * max_connections);
    pthread_mutex_unlock(&server->server_pollin.lock);
    
    return server;
}


/**
 * @brief Deallocates the memory occupied by the server at the specified index.
 *
 * @param server_index Index of the server in the servers array.
 */
void deallocate_server(int server_index) {
    server_t *server = servers[server_index];

    free(server->server_pollin.client_pollin_fds);
    free(server->server_pollin.assigned_clients);

    pthread_cond_destroy(&server->connection_details.poll_connections_cv);

    pthread_mutex_destroy(&server->server_details.lock);
    pthread_mutex_destroy(&server->connection_details.lock);
    pthread_mutex_destroy(&server->server_pollin.lock);

    servers[server_index] = NULL;
    free(server);
}


/**
 * @brief Loads server metadata stored in a file specified by file_path.
 *
 * This function reads the metadata from the file, initializes servers, and populates the servers array.
 *
 * @param file_path Path of the file storing the servers metadata.
 * @return Number of servers loaded, or -1 if the file could not be opened.
 */
int load_servers_metadata(char *file_path) {
    FILE *file;
    if ((file = fopen(file_path, "r")) == NULL) {
        perror("Error opening file");
        return -1;
    }

    char line[70];
    int count = 0;
    while (count < MAX_SERVERS && fgets(line, 70, file) != NULL) {
        server_t *s = init_server(count, DEFAULT_SERVER_MAX_CONNECTIONS);
        /* Assumes that the lines of the servers metadata file are formatted as follows:
            NAME ADDRESS PORT
            Example: SERVER_0 127.0.0.1 2000
        */
        pthread_mutex_lock(&s->server_details.lock);
        if (sscanf(line, "%19s %15s %d", s->server_details.name, s->server_details.address,
                    &s->server_details.port) != EOF) count++;

        pthread_mutex_unlock(&s->server_details.lock);
    }

    fclose(file);
    return count;
}


/**
 * @brief Initializes server metadata by loading from a default metadata file.
 * If the default metadata file does not exist or is not accessible, then the user is 
 * prompted to provide the path to another metadata file.
*/
void init_servers_metadata() {
    char metadata_path[MAX_PATH_LEN];
    strncpy(metadata_path, SERVERS_METADATA_PATH, MAX_PATH_LEN);

    while (load_servers_metadata(metadata_path) == -1) {
        // File failed to open. Prompting user to provide new file or quit.
        printf("Provide file path to server metadata:\n");
        fflush(stdin);
        fgets(metadata_path, MAX_PATH_LEN, stdin);
        metadata_path[strlen(metadata_path) - 1] = '\0'; // Removing \n
    }
}


/**
 * @brief Establishes a connection to the server specified in the argument.
 *
 * @param server Pointer to the server_t structure containing server information.
 * @return 0 on success, -1 on failure.
 */
int connect_to_server(server_t *server) {
    pthread_mutex_lock(&server->server_details.lock);

    int ret = 0;
    server->server_details.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server->server_details.port);
    int temp = inet_pton(AF_INET, server->server_details.address, &(server_addr.sin_addr));
    if (temp <= 0) {
        if (temp == 0) fprintf(stderr, "Address not in acceptable format: %s\n", server->server_details.address);
        if (temp < 0) perror("inet_pton");
        ret = -1;
        goto cleanup_and_return;
    }
    
    if (connect(server->server_details.sockfd, (struct sockaddr *) &server_addr, sizeof server_addr) == -1) {
        perror("connect");
        ret = -1;
        goto cleanup_and_return;
    }

    cleanup_and_return:
    pthread_mutex_unlock(&server->server_details.lock);
    return ret;
}


/**
 * @brief Thread routine for handling client-server communication.
 *
 * This routine continuously polls for data from clients, processes it, and sends it to the given server.
 *
 * @param arg Pointer to the server_t structure containing server information.
 */
void *routine(void *arg) {
    server_t *server = (server_t *) arg;
    server_details_t *server_details = &server->server_details;
    connection_details_t *connection_details = &server->connection_details;
    server_pollin_t *server_pollin = &server->server_pollin;
    struct pollfd *fds = server_pollin->client_pollin_fds;

    while (1) {
        pthread_mutex_lock(&connection_details->lock);
        while (connection_details->num_connections <= 0) {
            pthread_cond_wait(&connection_details->poll_connections_cv, &connection_details->lock);
        }
        int num_connections = connection_details->num_connections;
        pthread_mutex_unlock(&connection_details->lock);

        pthread_mutex_lock(&server_pollin->lock);
        #ifdef DEBUG
        int num_fds = poll(fds, num_connections, DEBUG_POLL_TIMEOUT_IN_MS);
        #else
        int num_fds = poll(fds, num_connections, POLL_TIMEOUT_IN_MS);
        #endif
        if (num_fds <= 0) {
            if (num_fds < 0) perror("poll");
            pthread_mutex_unlock(&server_pollin->lock);
            continue;
        }

        for (int i = 0; i < num_connections; i++) {
            if (fds[i].revents && POLLIN) {
                // Client has sent data, ready for reading
                char buf[1024];
                int count = recv(fds[i].fd, buf, 1023, 0);
                // TODO: Better process received data, and associate with client before sending
                buf[count] = '\0';
                if (count == 0) {
                    // Client disconnected
                    // TODO: Implement disconnection handeling 
                    pthread_mutex_lock(&connection_details->lock);
                    connection_details->num_connections--;
                    pthread_mutex_unlock(&connection_details->lock);
                } else if (count > 0) {
                    #ifdef DEBUG
                    pthread_mutex_lock(&server_details->lock);
                    printf("Server %s - data read from client %d on fd %d:\n%s\n", server_details->name, server_pollin->assigned_clients[i]->id, fds[i].fd, buf);
                    pthread_mutex_unlock(&server_details->lock);
                    #endif
                    // TODO: Send data read from client to the server.
                }
            }
        }

        pthread_mutex_unlock(&server_pollin->lock);
    }
}


/**
 * @brief Initializes and connects to servers, and creates threads corresponding to each.
 *
 * This function initializes server metadata, connects to servers, and creates detached threads for each server.
 *
 * @return Number of servers successfully connected.
 */
int init_servers() {
    for (int i = 0; i < MAX_SERVERS; i++) servers[i] = NULL;
    init_servers_metadata();
    
    int num_connected = 0;
    pthread_t thread;
    // Connecting to servers
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (servers[i] == NULL) continue;

        // Attempting to connect to server. Server is deallocated if an error occurs.
        if (connect_to_server(servers[i]) == -1){
            deallocate_server(i);
            continue;
        }

        // Setting up detached thread for server
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&thread, &thread_attr, &routine, (void *)servers[i])) {
            perror("pthread_create");
            deallocate_server(i);
            continue;
        }
        num_connected++;
    }

    return num_connected;
}


/**
 * @brief Initializes and configures an inbound socket for listening on the specified port.
 *
 * @param port Port number for the inbound socket.
 * @return File descriptor of the initialized socket, or -1 if an error occurs.
 */
int init_inbound_socket(int port) {
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    int sock_fd;
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    if (bind(sock_fd, (struct sockaddr *) &address, sizeof(address)) == -1) {
        perror("bind");
        return -1;
    }

    if (listen(sock_fd, MAX_QUEUED_CONNECTIONS) == -1) {
        perror("listen");
        return -1;
    }

    return sock_fd;
}


/**
 * @brief Assigns a client to a server with the least connection load.
 *
 * @param client Pointer to the client_t structure containing client information.
 */
void assign_client(client_t *client) {
    // Determining which server to assign client to, based on least connection load
    float load = 1;
    server_t *server = NULL, *temp;
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (servers[i] == NULL) continue;
        
        connection_details_t *details = &servers[i]->connection_details;
        pthread_mutex_lock(&details->lock);
        float temp_load = (float)details->num_connections / (float)details->max_connections;
        pthread_mutex_unlock(&details->lock);
        if (temp_load < load) {
            load = temp_load;
            server = servers[i];
        }
    }

    #ifdef DEBUG
    pthread_mutex_lock(&server->server_details.lock);
    printf("Client %d assigned to Server %s\n", client->id, server->server_details.name);
    pthread_mutex_unlock(&server->server_details.lock);
    #endif

    // Assigning client to chosen server
    pthread_mutex_lock(&server->connection_details.lock);
    pthread_mutex_lock(&server->server_pollin.lock);

    int num_connections = server->connection_details.num_connections;
    server->server_pollin.assigned_clients[num_connections] = client;
    server->server_pollin.client_pollin_fds[num_connections].fd = client->sockfd;
    server->server_pollin.client_pollin_fds[num_connections].events = POLLIN;
    server->connection_details.num_connections++;

    // Signalling server's assigned thread that there are clients to begin polling on
    pthread_cond_signal(&server->connection_details.poll_connections_cv);

    pthread_mutex_unlock(&server->server_pollin.lock);
    pthread_mutex_unlock(&server->connection_details.lock);
}


int main(int argc, char *argv[]) {
    if (init_servers() == 0) {
        fprintf(stderr, "All server connection attempts failed.\n");
        goto cleanup_and_exit;
    }

    int inbound_sock_fd;
    if ((inbound_sock_fd = init_inbound_socket(LB_PORT)) == -1) {
        fprintf(stderr, "Failed to initialize inbound socket.\n");
        goto cleanup_and_exit;
    }

    // TODO: Implement signal handling.

    int client_id = 0;
    while (1) {
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof client_address;
        int client_sock_fd = accept(inbound_sock_fd, (struct sockaddr *) &client_address,
                                    &client_address_len);
        if (client_sock_fd == -1) {
            perror("accept");
            continue;
        }
        
        // Creating and initializing new client structure
        client_t *client = malloc(sizeof(client_t));
        client->id = client_id++;
        client->client_address = client_address;
        client->sockfd = client_sock_fd;

        #ifdef DEBUG
        printf("New client connected - id:%d\n", client->id);
        #endif

        // Assigning client to a server
        assign_client(client);
    }

    cleanup_and_exit:
    // TODO: Handle deallocation of allocated memory
    return 0;
}