#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <time.h>
#include <pthread.h>
#include "backend.h"

char **words_g;
int words_len_g;

static int fcount_lines(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
    {
        perror("***Error trying to opening word list file!");
        exit(EXIT_FAILURE);
    }
    char buf[WORD_MAX_LEN];
    int count = 0;
    while (fgets(buf, WORD_MAX_LEN, file))
        count++;
    fclose(file);
    return count;
}

void init_words_g(void)
{
    int lines = fcount_lines(WORD_FILE);
    words_len_g = lines;

    FILE *file = fopen(WORD_FILE, "r");
    if (!file)
    {
        perror("***ERROR: failed to open file!");
        exit(EXIT_FAILURE);
    }

    words_g = malloc(lines * sizeof(char *));
    if (!words_g)
    {
        perror("***ERROR: failed to allocate words_g!");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    char buf[WORD_MAX_LEN];
    int count = 0;

    while (fgets(buf, WORD_MAX_LEN, file) && count < lines)
    {
        buf[strcspn(buf, "\n")] = '\0';
        words_g[count] = malloc(strlen(buf) + 1);
        if (!words_g[count])
        {
            perror("***ERROR: malloc failed while copying a word");
            for (int i = 0; i < count; i++)
                free(words_g[i]);
            free(words_g);
            fclose(file);
            exit(EXIT_FAILURE);
        }
        strcpy(words_g[count], buf);
        count++;
    }
    fclose(file);
}

static char **get_chunk(void)
{
    char **chunk = (char **)malloc(WORD_CHUNK * sizeof(char *));
    if (!chunk)
    {
        perror("***ERROR: failed to allocate memory for word chunk!");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < WORD_CHUNK; i++)
    {
        int random_idx = rand() % words_len_g;
        const char *tmp = words_g[random_idx];
        chunk[i] = (char *)malloc(strlen(tmp) + 1);
        if (!chunk[i])
        {
            perror("***ERROR: malloc failed while populating word chunk!");
            for (int j = 0; j < i; j++)
                free(chunk[j]);
            free(chunk);
            exit(EXIT_FAILURE);
        }
        strcpy(chunk[i], tmp);
    }
    return chunk;
}

session_t *create_session(void)
{
    session_t *session = (session_t *)malloc(sizeof(session_t));
    if (!session)
    {
        perror("***ERROR: failed to initialize typing session!");
        exit(EXIT_FAILURE);
    }

    session->has_started = 0;
    session->ended = 0;
    session->list = get_chunk();
    session->players_count = 0;
    session->clock = 0;
    session->start_ts.tv_sec = 0;
    session->start_ts.tv_nsec = 0;
    session->countdown_running = 0;

    if (pthread_mutex_init(&session->lock, NULL) != 0)
    {
        perror("***ERROR: failed to initialize session mutex!");
        free(session);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_LOBBY_COUNT; i++)
        session->players[i] = NULL;

    return session;
}

int add_player(session_t *session, client_t *client)
{
    if (!session || !client)
        return 0;

    pthread_mutex_lock(&session->lock);
    for (int i = 0; i < MAX_LOBBY_COUNT; i++)
    {
        if (session->players[i] == NULL)
        {
            session->players[i] = client;
            session->players_count++;
            int ret = session->players_count;
            pthread_mutex_unlock(&session->lock);
            return ret;
        }
    }
    pthread_mutex_unlock(&session->lock);
    return 0;
}

static void free_session(session_t *session)
{
    if (!session)
        return;
    for (int i = 0; i < WORD_CHUNK; i++)
        free(session->list[i]);
    free(session->list);
    pthread_mutex_destroy(&session->lock);
    free(session);
}

session_list_t *create_session_list(void)
{
    session_list_t *list = (session_list_t *)malloc(sizeof(session_list_t));
    if (!list)
    {
        perror("***ERROR: memory allocation failed for session list!");
        exit(EXIT_FAILURE);
    }
    list->count = 0;
    if (pthread_mutex_init(&list->lock, NULL) != 0)
    {
        perror("***ERROR: failed to initialize session list mutex!");
        free(list);
        exit(EXIT_FAILURE);
    }
    list->sessions = (session_t **)malloc(MAX_SESSIONS * sizeof(session_t *));
    if (!list->sessions)
    {
        perror("***ERROR: memory allocation failed for session object!");
        pthread_mutex_destroy(&list->lock);
        free(list);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < MAX_SESSIONS; i++)
        list->sessions[i] = NULL;
    return list;
}

int add_session(session_list_t *list, session_t *session)
{
    if (!list || !session)
        return 0;

    pthread_mutex_lock(&list->lock);
    if (list->count == MAX_SESSIONS)
    {
        pthread_mutex_unlock(&list->lock);
        return 0;
    }

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!list->sessions[i])
        {
            list->sessions[i] = session;
            list->count++;
            int idx = i;
            pthread_mutex_unlock(&list->lock);
            return idx;
        }
    }
    pthread_mutex_unlock(&list->lock);
    return 0;
}

void remove_session(session_list_t *list, session_t *session)
{
    if (!list || !session)
        return;

    pthread_mutex_lock(&list->lock);
    if (list->count == 0)
    {
        pthread_mutex_unlock(&list->lock);
        return;
    }

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (list->sessions[i] == session)
        {
            free_session(list->sessions[i]);
            list->sessions[i] = NULL;
            list->count--;
            break;
        }
    }
    pthread_mutex_unlock(&list->lock);
}

session_t *find_free_session(session_list_t *list)
{
    if (!list)
    {
        perror("***ERROR: session_list_t is null!");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&list->lock);
    if (list->count == MAX_SESSIONS)
    {
        pthread_mutex_unlock(&list->lock);
        return NULL;
    }

    int first_session_available = -1;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (list->sessions[i])
        {
            pthread_mutex_lock(&list->sessions[i]->lock);
            if (list->sessions[i]->players_count < MAX_LOBBY_COUNT &&
                !list->sessions[i]->has_started)
            {
                session_t *result = list->sessions[i];
                pthread_mutex_unlock(&list->sessions[i]->lock);
                pthread_mutex_unlock(&list->lock);
                return result;
            }
            pthread_mutex_unlock(&list->sessions[i]->lock);
        }
        if (!list->sessions[i] && first_session_available == -1)
            first_session_available = i;
    }

    if (first_session_available != -1)
    {
        list->sessions[first_session_available] = create_session();
        list->count++;
        session_t *result = list->sessions[first_session_available];
        pthread_mutex_unlock(&list->lock);
        return result;
    }

    pthread_mutex_unlock(&list->lock);
    return NULL;
}

void remove_player(session_list_t *list, session_t *session, const char *uuid_str)
{
    if (!list || !session || !uuid_str)
        return;

    pthread_mutex_lock(&session->lock);

    if (session->players_count == 0)
    {
        pthread_mutex_unlock(&session->lock);
        return;
    }

    int found = 0;
    for (int i = 0; i < MAX_LOBBY_COUNT; i++)
    {
        if (session->players[i] && strcmp(session->players[i]->uuid, uuid_str) == 0)
        {
            session->players[i] = NULL;
            session->players_count--;
            found = 1;
            break;
        }
    }

    int is_empty = (session->players_count == 0);
    int running = session->countdown_running;
    pthread_mutex_unlock(&session->lock);

    if (found && is_empty)
    {
        if (running)
            pthread_join(session->countdown_tid, NULL);
        printf("Last player removed, freeing session\n");
        remove_session(list, session);
    }
}

int is_correct(const char *target, const char *input)
{
    return strcmp(target, input) == 0;
}

static inline double timespec_diff_sec(const struct timespec *a, const struct timespec *b)
{
    return (a->tv_sec - b->tv_sec) + (a->tv_nsec - b->tv_nsec) / 1e9;
}

int wpm(session_t *session, int correct_words)
{
    if (!session || !session->has_started || correct_words <= 0)
        return 0;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed_s = timespec_diff_sec(&now, &session->start_ts);
    if (elapsed_s < 1e-3)
        elapsed_s = 1e-3;

    int n = correct_words;
    if (n > WORD_CHUNK)
        n = WORD_CHUNK;

    size_t chars = 0;
    for (int i = 0; i < n; ++i)
        chars += strlen(session->list[i]);
    if (n > 1)
        chars += (size_t)(n - 1);

    double wpm_f = (chars / 5.0) / (elapsed_s / 60.0);
    if (wpm_f < 0.0)
        wpm_f = 0.0;
    return (int)(wpm_f + 0.5);
}
