#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cjson/cJSON.h>
#include <errno.h>
#include "network.h"
#include "backend.h"

pthread_mutex_t client_lock_g = PTHREAD_MUTEX_INITIALIZER;
session_list_t *list_g;
int active_clients_g = 0;


static inline double timespec_diff_sec(const struct timespec *a, const struct timespec *b)
{
    return (a->tv_sec - b->tv_sec) + (a->tv_nsec - b->tv_nsec) / 1e9;
}


static void send_json_line(int fd, cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    if (!s)
    {
        cJSON_Delete(root);
        return;
    }
#ifdef MSG_NOSIGNAL
    send(fd, s, strlen(s), MSG_NOSIGNAL);
    send(fd, "\n", 1, MSG_NOSIGNAL);
#else
    send(fd, s, strlen(s), 0);
    send(fd, "\n", 1, 0);
#endif
    free(s);
    cJSON_Delete(root);
}

static void send_event(int fd,
                       const char *type,
                       const char *player,
                       const char *message,
                       cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        if (data)
            cJSON_Delete(data);
        return;
    }

    if (type)
        cJSON_AddStringToObject(root, "type", type);
    if (player)
        cJSON_AddStringToObject(root, "player", player);
    if (message)
        cJSON_AddStringToObject(root, "message", message);
    if (data)
        cJSON_AddItemToObject(root, "data", data);

    send_json_line(fd, root);
}

static void notify_all_players_event(session_t *session,
                                     const char *type,
                                     const char *player,
                                     const char *message,
                                     cJSON *data)
{
    int fds[MAX_LOBBY_COUNT], n = 0;

    pthread_mutex_lock(&session->lock);
    for (int i = 0; i < MAX_LOBBY_COUNT; i++)
        if (session->players[i])
            fds[n++] = session->players[i]->socket;
    pthread_mutex_unlock(&session->lock);

    for (int i = 0; i < n; i++)
    {
        cJSON *copy = data ? cJSON_Duplicate(data, 1) : NULL;
        send_event(fds[i], type, player, message, copy);
    }
    if (data)
        cJSON_Delete(data);
}

void *session_countdown(void *arg)
{
    session_t *session = (session_t *)arg;

    pthread_mutex_lock(&session->lock);
    session->countdown_running = 1;
    pthread_mutex_unlock(&session->lock);

    for (int i = 10; i >= 1; i--)
    {
        cJSON *d = cJSON_CreateObject();
        if (d)
            cJSON_AddNumberToObject(d, "value", i);
        notify_all_players_event(session, "countdown", NULL, NULL, d);
        sleep(1);
    }

    pthread_mutex_lock(&session->lock);
    session->has_started = 1;
    session->clock = clock(); // legacy
    clock_gettime(CLOCK_MONOTONIC, &session->start_ts);
    pthread_mutex_unlock(&session->lock);

    cJSON *words = cJSON_CreateArray();
    if (words)
    {
        for (int i = 0; i < WORD_CHUNK; i++)
        {
            cJSON_AddItemToArray(words, cJSON_CreateString(session->list[i]));
        }
        cJSON *d = cJSON_CreateObject();
        if (d)
        {
            cJSON_AddItemToObject(d, "words", words);
            notify_all_players_event(session, "words", NULL, NULL, d);
        }
        else
        {
            cJSON_Delete(words);
        }
    }

    pthread_mutex_lock(&session->lock);
    session->countdown_running = 0;
    pthread_mutex_unlock(&session->lock);
    return NULL;
}

void *handle_client(void *arg)
{
    client_t *client = (client_t *)arg;
    client->last_activity_ts.tv_sec = 0;
    client->last_activity_ts.tv_nsec = 0;

    // 1) handshake: uuid + name
    char buf[1024] = {0};
    int n = recv(client->socket, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
    {
        printf("Couldn't verify user\n");
        goto cleanup;
    }

    buf[n] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        send_event(client->socket, "error", NULL, "invalid format", NULL);
        goto cleanup;
    }

    cJSON *uuid_json = cJSON_GetObjectItemCaseSensitive(json, "uuid");
    cJSON *name_json = cJSON_GetObjectItemCaseSensitive(json, "name");
    if (!cJSON_IsString(uuid_json) ||
        !cJSON_IsString(name_json) ||
        strlen(uuid_json->valuestring) >= UUID_LEN ||
        strlen(name_json->valuestring) >= NAME_MAX_LEN)
    {
        send_event(client->socket, "error", NULL, "invalid parameters", NULL);
        cJSON_Delete(json);
        goto cleanup;
    }

    strncpy(client->uuid, uuid_json->valuestring, UUID_LEN - 1);
    client->uuid[UUID_LEN - 1] = '\0';
    strncpy(client->name, name_json->valuestring, NAME_MAX_LEN - 1);
    client->name[NAME_MAX_LEN - 1] = '\0';
    cJSON_Delete(json);

    printf("Client connected: name=%s uuid=%s\n", client->name, client->uuid);

    // 2) lobby
    session_t *tmp = find_free_session(list_g);
    if (!tmp)
    {
        send_event(client->socket, "error", NULL, "couldn't find available session", NULL);
        goto cleanup;
    }

    int count = add_player(tmp, client);
    if (count == 0)
    {
        send_event(client->socket, "error", NULL, "failed to add player to session", NULL);
        goto cleanup;
    }

    printf("Player added to session. Current count: %d\n", count);
    send_event(client->socket, "lobby", client->name, "added to lobby", NULL);

    if (count == 2)
    {
        printf("Starting countdown for session with 2 players\n");
        if (pthread_create(&tmp->countdown_tid, NULL, session_countdown, (void *)tmp) != 0)
        {
            perror("failed to create countdown thread");
            send_event(client->socket, "error", NULL, "failed to start game", NULL);
            goto cleanup;
        }
    }

    // 3) loop di gioco
    int word_counter = 0;
    while (1)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        int should_end = 0;
        pthread_mutex_lock(&tmp->lock);
        int game_started = tmp->has_started;
        int already_ended = tmp->ended;
        struct timespec start_ts = tmp->start_ts;
        pthread_mutex_unlock(&tmp->lock);

        if (game_started && !already_ended)
        {
            double elapsed_session = timespec_diff_sec(&now, &start_ts);
            if (elapsed_session >= SESSION_HARD_TIMEOUT_SEC)
            {
                pthread_mutex_lock(&tmp->lock);
                if (!tmp->ended)
                {
                    tmp->ended = 1;
                    should_end = 1;
                }
                pthread_mutex_unlock(&tmp->lock);
                if (should_end)
                {
                    notify_all_players_event(tmp, "session_end", NULL, "Session closed after 10 minutes", NULL);
                }
            }
        }
        pthread_mutex_lock(&tmp->lock);
        int ended_now = tmp->ended;
        pthread_mutex_unlock(&tmp->lock);
        if (ended_now)
        {
            send_event(client->socket, "session_end", NULL, "Closing session", NULL);
            break;
        }

        char inbuf[1024];
        int r = recv(client->socket, inbuf, sizeof(inbuf) - 1, MSG_DONTWAIT);

        if (r == 0)
        {
            printf("Client %s disconnected\n", client->uuid);
            break;
        }
        else if (r < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // kick per inattività
                if (game_started)
                {
                    if (client->last_activity_ts.tv_sec == 0 && client->last_activity_ts.tv_nsec == 0)
                        client->last_activity_ts = start_ts;
                    double idle = timespec_diff_sec(&now, &client->last_activity_ts);
                    if (idle >= PLAYER_INACTIVE_KICK_SEC)
                    {
                        send_event(client->socket, "inactive_timeout", NULL, "Kicked after 60s of inactivity", NULL);
                        printf("Kicking %s for inactivity\n", client->uuid);
                        break;
                    }
                }
                struct timespec ts = {.tv_sec = 0, .tv_nsec = 1 * 1000 * 1000}; // 1 ms
                nanosleep(&ts, NULL);
                continue;
            }
            else
            {
                printf("Error receiving from client %s: %s\n", client->uuid, strerror(errno));
                break;
            }
        }
        else
        {
            inbuf[r] = '\0';

            cJSON *msg = cJSON_Parse(inbuf);
            if (!msg)
                continue;

            // richiesta esplicita di disconnessione
            int wants_disconnect = 0;
            cJSON *type = cJSON_GetObjectItemCaseSensitive(msg, "type");
            if (cJSON_IsString(type) && strcmp(type->valuestring, "disconnect") == 0)
                wants_disconnect = 1;
            cJSON *disc = cJSON_GetObjectItemCaseSensitive(msg, "disconnect");
            if (cJSON_IsBool(disc) && cJSON_IsTrue(disc))
                wants_disconnect = 1;

            if (wants_disconnect)
            {
                send_event(client->socket, "bye", NULL, "Disconnected on request", NULL);
                cJSON_Delete(msg);
                break;
            }

            if (!game_started)
            {
                cJSON_Delete(msg);
                continue;
            }

            cJSON *word_item = cJSON_GetObjectItemCaseSensitive(msg, "word");
            if (!word_item || !cJSON_IsString(word_item))
            {
                send_event(client->socket, "error", NULL, "json parsing failed", NULL);
                cJSON_Delete(msg);
                continue;
            }

            client->last_activity_ts = now;

            if (word_counter < WORD_CHUNK &&
                is_correct(tmp->list[word_counter], word_item->valuestring))
            {

                word_counter++;
                int curr_wpm = wpm(tmp, word_counter);

                cJSON *d_self = cJSON_CreateObject();
                if (d_self)
                    cJSON_AddNumberToObject(d_self, "value", curr_wpm);
                send_event(client->socket, "wpm", client->name, NULL, d_self);

                cJSON *d_all = cJSON_CreateObject();
                if (d_all)
                    cJSON_AddNumberToObject(d_all, "value", curr_wpm);
                notify_all_players_event(tmp, "wpm", client->name, NULL, d_all);
            }

            cJSON_Delete(msg);

            if (word_counter >= WORD_CHUNK)
            {
                send_event(client->socket, "completed", client->name,
                           "All words completed! You have 20 seconds before disconnect", NULL);

                time_t start_timeout = time(NULL);
                const time_t timeout_duration = 20;

                for (;;)
                {
                    pthread_mutex_lock(&tmp->lock);
                    int ended_mid = tmp->ended;
                    pthread_mutex_unlock(&tmp->lock);
                    if (ended_mid)
                    {
                        send_event(client->socket, "session_end", NULL, "Closing session", NULL);
                        goto after_loop;
                    }

                    time_t elapsed = time(NULL) - start_timeout;
                    if (elapsed >= timeout_duration)
                    {
                        send_event(client->socket, "timeout", NULL, "20 seconds timeout expired, disconnecting", NULL);
                        goto after_loop;
                    }

                    char tbuf[256];
                    int tr = recv(client->socket, tbuf, sizeof(tbuf), MSG_DONTWAIT);
                    if (tr == 0)
                        goto after_loop;
                    if (tr < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                        goto after_loop;

                    if (elapsed > 0 && (elapsed % 5) == 0)
                    {
                        cJSON *d = cJSON_CreateObject();
                        if (d)
                            cJSON_AddNumberToObject(d, "remaining", timeout_duration - elapsed);
                        send_event(client->socket, "timeout_warning", NULL, NULL, d);
                    }

                    sleep(1);
                }
            }
        }
    }

after_loop:
    remove_player(list_g, tmp, client->uuid);

cleanup:
    close(client->socket);

    pthread_mutex_lock(&client_lock_g);
    active_clients_g--;
    pthread_mutex_unlock(&client_lock_g);

    free(client);
    return NULL;
}

int main(void)
{
    srand((unsigned)time(NULL));

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

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
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
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0)
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
#ifdef MSG_NOSIGNAL
            send(client_socket, msg, strlen(msg), MSG_NOSIGNAL);
#else
            send(client_socket, msg, strlen(msg), 0);
#endif
            close(client_socket);
            continue;
        }

        client_t *client = malloc(sizeof(client_t));
        if (!client)
        {
            perror("failed to malloc client_t");
            close(client_socket);
            continue;
        }

        client->socket = client_socket;
        client->uuid[0] = '\0';
        client->last_activity_ts.tv_sec = 0;
        client->last_activity_ts.tv_nsec = 0;

        pthread_mutex_lock(&client_lock_g);
        active_clients_g++;
        pthread_mutex_unlock(&client_lock_g);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client) != 0)
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
