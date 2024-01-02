#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define MAX_SERVERS 10
#define SERVERS_METADATA_PATH "./servers_metadata.txt"
#define MAX_PATH_LEN 260

typedef struct server {
    char name[20];
    char address[16];
    int port;
    bool is_active;
    int num_connections;
    int max_connections;
} server;

struct server servers[MAX_SERVERS];

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
        struct server *s = &servers[count];
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

int main(int argc, char *argv[]) {
    init_servers_metadata();
    return 0;
}