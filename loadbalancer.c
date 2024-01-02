#include <stdio.h>
#include <stdbool.h>

#define MAX_SERVERS 10

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
 * @brief Parses server information stored in file specified by file_name. 
 * @param file_name Name of file storing the server data.
 * @return Number of servers parsed, or -1 if file could not be opened.
*/
int parse_servers_file(char *file_name) {
    FILE *file;
    if ((file = fopen(file_name, "r")) == NULL) {
        perror("Error opening file");
        return -1;
    }

    char line[70];
    int count = 0;
    while (count < MAX_SERVERS && fgets(line, 70, file) != NULL) {
        struct server *s = &servers[count];
        /* Assuming that the lines of the servers list file are formatted as follows:
            NAME ADDRESS PORT
            Example: SERVER_0 127.0.0.1 2000
        */
        if (sscanf(line, "%19s %15s %d", s->name, s->address, &s->port) != EOF) count++;
    }

    fclose(file);
    return count;
}


int init_servers() {
    if (parse_servers_file("servers_list.txt") == -1) {
        // File failed to open
    }


    return 0;
}

int main(int argc, char *argv[]) {
    init_servers();
    return 0;
}