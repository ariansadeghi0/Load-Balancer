#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAX_SERVERS 10
#define SERVERS_METADATA_PATH "./servers_metadata.txt"
#define MAX_PATH_LEN 260
#define LB_PORT 1800
#define MAX_QUEUED_CONNECTIONS 100

enum server_status {ACTIVE, INACTIVE, ERROR};

typedef struct server_details {
    char name[20];
    char address[16];
    int port;
    int sockfd;
    enum server_status status;
    int num_connections;
    int max_connections;
    pthread_mutex_t lock;
} server_details;

server_details **servers;


/**
 * @brief Initializes a new server_details structure and returns a pointer to it.
 *
 * @return Pointer to the initialized server_details structure.
 */
server_details *init_server() {
    server_details *res = malloc(sizeof(server_details));
    pthread_mutex_init(&res->lock, NULL);
    return res;
}

/**
 * @brief Deallocates the memory occupied by the server at the specified index.
 *
 * @param server_index Index of the server in the servers array.
 */
void deallocate_server(int server_index) {
    pthread_mutex_destroy(&servers[server_index]->lock);
    free(servers[server_index]);
    servers[server_index] = NULL;
}

/**
 * @brief Loads server metadata stored in file specified by file_path. 
 * @param file_path Path of file storing the servers metadata.
 * @return Number of servers loaded, or -1 if file could not be opened.
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
        server_details *s = init_server();
        servers[count] = s;
        /* Assumes that the lines of the servers metadata file are formatted as follows:
            NAME ADDRESS PORT
            Example: SERVER_0 127.0.0.1 2000
        */
        if (sscanf(line, "%19s %15s %d", s->name, s->address, &s->port) != EOF) count++;
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
 * Function is to be used as a thread routine.
 *
 * @param arg Pointer to the server_details structure containing server information.
 */
void *connect_to_server(void *arg) {
    server_details *server = (server_details *)arg;

    pthread_mutex_lock(&server->lock);

    server->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server->port);
    int temp = inet_pton(AF_INET, server->address, &(server_addr.sin_addr));
    if (temp <= 0) {
        if (temp == 0) fprintf(stderr, "Address not in acceptable format: %s\n", server->address);
        if (temp < 0) perror("inet_pton");
        server->status = ERROR;
        goto cleanup_and_return;
    }
    
    if (connect(server->sockfd, (struct sockaddr *) &server_addr, sizeof server_addr) == -1) {
        perror("connect");
        server->status = ERROR;
        goto cleanup_and_return;
    }

    server->status = ACTIVE;
    cleanup_and_return:
    pthread_mutex_unlock(&server->lock);
    return NULL;
}


/**
 * @brief Establishes connections to all the populated servers in the servers array using threads.
 *
 * A seperate thread is created for each server to establish connections concurrently.
 */
void connect_to_servers() {
    pthread_t server_threads[MAX_SERVERS];

    // Creating threads to establish a connection with the servers
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (servers[i] == NULL) continue;

        if (pthread_create(&server_threads[i], NULL, &connect_to_server, (void *)servers[i])) {
            perror("pthread_create");
            continue;
        }
    }

    // Joining threads once they have established a connection with the servers
    for (int i = 0; i < MAX_SERVERS; i++) {
        if (servers[i] == NULL) continue;

        if (pthread_join(server_threads[i], NULL)) {
            perror("pthread_join");
            continue;
        }

        // Deallocating server if an error occured while trying to establish connection with it
        if (servers[i]->status == ERROR) deallocate_server(i);
    }
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


int main(int argc, char *argv[]) {
    servers = malloc(MAX_SERVERS * sizeof(server_details *));
    for (int i = 0; i < MAX_SERVERS; i++) servers[i] = NULL;

    init_servers_metadata();

    connect_to_servers();

    int inbound_sock_fd;
    if ((inbound_sock_fd = init_inbound_socket(LB_PORT)) == -1) {
        fprintf(stderr, "Failed to initialize inbound socket.\n");
        goto cleanup_and_exit;
    }

    cleanup_and_exit:
    free(servers);
    return 0;
}