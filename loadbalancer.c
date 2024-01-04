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

enum server_status {ACTIVE, INACTIVE, ERROR};

typedef struct server_details {
    char name[20];
    char address[16];
    int port;
    int sockfd;
    enum server_status status;
    int num_connections;
    int max_connections;
} server_details;

server_details **servers;

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
        server_details *s = malloc(sizeof(server_details));
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


void deallocate_server(int server_index) {
    free(servers[server_index]);
    servers[server_index] = NULL;
}


void *connect_to_server(void *arg) {
    server_details *server = (server_details *)arg;

    server->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server->port);
    int temp = inet_pton(AF_INET, server->address, &(server_addr.sin_addr));
    if (temp <= 0) {
        if (temp == 0) fprintf(stderr, "Address not in acceptable format: %s\n", server->address);
        if (temp < 0) perror("inet_pton");
        server->status = ERROR;
        return NULL;
    }
    
    if (connect(server->sockfd, (struct sockaddr *) &server_addr, sizeof server_addr) == -1) {
        perror("connect");
        server->status = ERROR;
        return NULL;
    }

    server->status = ACTIVE;
    return NULL;
}


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


int main(int argc, char *argv[]) {
    servers = malloc(MAX_SERVERS * sizeof(server_details *));
    for (int i = 0; i < MAX_SERVERS; i++) servers[i] = NULL;

    init_servers_metadata();

    connect_to_servers();

    free(servers);
    return 0;
}