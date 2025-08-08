#ifndef NETWORK_H
#define NETWORK_H

#define UUID_LEN        37
#define MAX_LOBBY_COUNT 8
#define MAX_SESSIONS    16
#define MAX_CLIENTS (MAX_SESSIONS * MAX_LOBBY_COUNT)
#define SERVER_PORT 9000
#define MAX_BUF_SIZE 2048


typedef struct __client_info
{
    int socket;
    char uuid[UUID_LEN];
} client_t;


void* handle_client(void *arg);


#endif // NETWORK_H
