#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cjson/cJSON.h>
#include <time.h>
#include <errno.h>
#include "network.h"
#include "backend.h"


pthread_mutex_t client_lock_g = PTHREAD_MUTEX_INITIALIZER;
session_list_t  *list_g;
int active_clients_g = 0;


void send_response(int socket, const char* status, const char* msg)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) return;

    cJSON_AddStringToObject(json, "status", status);
    cJSON_AddStringToObject(json, "message", msg);

    char *str = cJSON_PrintUnformatted(json);
    if (str)
    {
        send(socket, str, strlen(str), 0);
        free(str);
    }

    cJSON_Delete(json);
}

void notify_all_players(session_t *session, const char *key, const char *msg)
{
	pthread_mutex_lock(&session->lock);
	for (int i = 0; i < MAX_LOBBY_COUNT; i++)
	{
		if (session->players[i])
			send_response(session->players[i]->socket, key, msg);
	}
	pthread_mutex_unlock(&session->lock);
}


void *session_countdown(void *arg)
{
    session_t *session = (session_t*) arg;
    
    for (int i = 10; i >= 1; i--)
    {
        char str[10];
        sprintf(str, "%d", i);
        notify_all_players(session, "countdown", str);
        sleep(1);
    }

    pthread_mutex_lock(&session->lock);
    session->has_started = 1;
    pthread_mutex_unlock(&session->lock);

    
    cJSON *text = cJSON_CreateArray();
    if (!text)
        return NULL;
    
    for (int i = 0; i < WORD_CHUNK; i++)
    {
        cJSON *pair = cJSON_CreateObject();
        if (!pair) return NULL;

        char str[10];
        sprintf(str, "%d", i);
        
        cJSON_AddStringToObject(pair, str, session->list[i]);
        cJSON_AddItemToArray(text, pair);
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;

    cJSON_AddItemToObject(root, "words", text);
    char *msg = cJSON_PrintUnformatted(root);
    notify_all_players(session, "text", msg);

    free(msg);
    cJSON_Delete(root);
    
    return NULL;
}


void *handle_client(void *arg)
{
    // 1. receive UUID and verify it
    client_t *client = (client_t*) arg;
    char buf[1024] = {0};
    int n = recv(client->socket, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
    {
        printf("Couldn't verify user\n");
        goto cleanup;
    }

    buf[n] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        send_response(client->socket, "error", "invalid format");
        goto cleanup;
    }

    cJSON *uuid_json = cJSON_GetObjectItemCaseSensitive(json, "uuid");
    if (!cJSON_IsString(uuid_json) || strlen(uuid_json->valuestring) >= UUID_LEN)
    {
        send_response(client->socket, "error", "invalid parameters");
        cJSON_Delete(json);
        goto cleanup;
    }

    strncpy(client->uuid, uuid_json->valuestring, UUID_LEN - 1);
    client->uuid[UUID_LEN - 1] = '\0';
    cJSON_Delete(json);

    printf("Client connected with UUID: %s\n", client->uuid);

    // 2. find a lobby for the user, based on its preference (single or multiplayer.
    //    obviously if it's the latter, the session_list will be emtpy)
    session_t *tmp = find_free_session(list_g);
    if (tmp == NULL)
    {
        send_response(client->socket, "error", "couldn't find available session");
        goto cleanup;
    }

    // 3. add the player to the session, and if the player is added to an already created
    //    lobby such that after the add_player the count goes to 2, it means that the lobby
    //    can start (the thread starts a countdown and send it to all the users in the lobby)
    int count = add_player(tmp, client);
    if (count == 0)
    {
        send_response(client->socket, "error", "failed to add player to session");
        goto cleanup;
    }

    printf("Player added to session. Current count: %d\n", count);
    send_response(client->socket, "success", "added to lobby");

    if (count == 2)
    {
        printf("Starting countdown for session with 2 players\n");
        pthread_t countdown_thread;
        if (pthread_create(&countdown_thread, NULL, session_countdown, (void*)tmp) != 0)
        {
            perror("failed to create countdown thread");
            send_response(client->socket, "error", "failed to start game");
            goto cleanup;
        }
        pthread_detach(countdown_thread);
    }

    // 4. Keep the connection alive to receive messages during the game
    //    Wait for the game to start or for client to disconnect
    while (1)
    {
        char dummy_buf[1024];
        int result = recv(client->socket, dummy_buf, sizeof(dummy_buf), MSG_DONTWAIT);
        
        if (result == 0)
        {
            printf("Client %s disconnected\n", client->uuid);
            break;
        }
        else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            printf("Error receiving from client %s\n", client->uuid);
            break;
        }
        
        pthread_mutex_lock(&tmp->lock);
        int game_started = tmp->has_started;
        pthread_mutex_unlock(&tmp->lock);
        
        if (game_started)
        {
            sleep(1);
        }
        else
        {
            sleep(1);
        }
    }

    remove_player(list_g, tmp, client->uuid);

    cleanup:
        close(client->socket);

        pthread_mutex_lock(&client_lock_g);
        active_clients_g--;
        pthread_mutex_unlock(&client_lock_g);

        free(client);
        return NULL;
}


int main()
{
    init_words_g();
    
    list_g = create_session_list();

    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("server socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*) &address, sizeof(address)) < 0)
    {
        perror("socket binding failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("socket listening failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1)
    {
        if ((client_socket = accept(server_fd, (struct sockaddr*) &address, &addrlen)) < 0)
        {
            perror("failed to accept connection");
            continue;
        }

        pthread_mutex_lock(&client_lock_g);
        int is_full = (active_clients_g >= MAX_CLIENTS);
        pthread_mutex_unlock(&client_lock_g);

        if (is_full)
        {
            printf("Server full, rejecting connection\n");
            const char *msg = "Server is full, try again later\n";
            send(client_socket, msg, strlen(msg), 0);
            close(client_socket);
            continue;
        }

        client_t *client = malloc(sizeof(client_t));
        if (client == NULL)
        {
            perror("failed to malloc client_t");
            close(client_socket);
            continue;
        }

        client->socket = client_socket;
        client->uuid[0] = '\0';

        pthread_mutex_lock(&client_lock_g);
        active_clients_g++;
        pthread_mutex_unlock(&client_lock_g);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*) client) != 0)
        {
            perror("pthread_create failed");
            close(client_socket);
            free(client);

            pthread_mutex_lock(&client_lock_g);
            active_clients_g--;
            pthread_mutex_unlock(&client_lock_g);
        }
        else 
        {
            pthread_detach(thread_id);
        }
    }

    close(server_fd);
    return 0;
}