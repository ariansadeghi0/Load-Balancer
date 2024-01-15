#include <pthread.h>

/** @brief Maximum number of servers the load balancer is restricted to. */
#define MAX_SERVERS 10

/** @brief Path to the file storing the servers metadata. */
#define SERVERS_METADATA_PATH "./servers_metadata.txt"

/** @brief Maximum length of file paths. */
#define MAX_PATH_LEN 260

/** @brief Default port number for the load balancer. */
#define LB_PORT 1800

/** @brief Maximum number of queued connections in the load balancer's listen backlog. */
#define MAX_QUEUED_CONNECTIONS 100

/** @brief Default maximum connections per server. */
#define DEFAULT_SERVER_MAX_CONNECTIONS 1000

/** @brief Timeout in milliseconds for polling on clients for reads. */
#define POLL_TIMEOUT_IN_MS 100

/** @brief Enumeration representing the status of a server. */
enum server_status {
    ACTIVE,    /** Server is active and available for connections. */
    INACTIVE,  /** Server is inactive. */
    ERROR      /** Server encountered an error. */
};

/** @brief Structure representing a client connected to the load balancer. */
typedef struct client_t {
    int id;                           /** Unique identifier for the client. */
    int sockfd;                       /** Socket file descriptor for the client. */
    struct sockaddr_in client_address; /** Client's socket address structure. */
} client_t;

/** @brief Structure representing the details of a server. */
typedef struct server_details_t {
    char name[20];               /** Name of the server. */
    char address[16];            /** IP address of the server. */
    int port;                    /** Port number on which the server listens. */
    int sockfd;                  /** Socket file descriptor for the server. */
    enum server_status status;   /** Status of the server (ACTIVE, INACTIVE, ERROR). */
    pthread_mutex_t lock;        /** Mutex for thread-safe access to server details. */
} server_details_t;

/** @brief Structure representing the connection details of a server. */
typedef struct connection_details_t {
    int num_connections;          /** Number of active connections to the server. */
    int max_connections;          /** Maximum allowed connections for the server. */
    pthread_cond_t poll_connections_cv; /** Condition variable for signaling for polling. */
    pthread_mutex_t lock;         /** Mutex for thread-safe access to connection details. */
} connection_details_t;

/** @brief Structure representing the server's pollin details. */
typedef struct server_pollin_t {
    struct pollfd *client_pollin_fds; /** Array of pollfd structures for client connections. */
    client_t **assigned_clients;      /** Array of assigned clients being polled for reads. */
    pthread_mutex_t lock;             /** Mutex for thread-safe access to pollin details. */
} server_pollin_t;

/** @brief Structure representing a server in the load balancer system. */
typedef struct server_t {
    server_details_t server_details; /** Details of the server. */
    server_pollin_t server_pollin;   /** Pollin details for the server. */
    connection_details_t connection_details; /** Connection details for the server. */
} server_t;

/** @brief Array of pointers to server structures, representing the servers in the load balancer. */
extern server_t *servers[MAX_SERVERS];