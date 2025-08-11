#pragma once
#include <pthread.h>
#include <time.h>

#define NAME_MAX_LEN      16
#define MAX_SESSIONS      16
#define MAX_LOBBY_COUNT   8
#define WORD_CHUNK        50
#define WORD_MAX_LEN      64
#define WORD_FILE         "word_list.txt"
#define UUID_LEN          64
#define SERVER_PORT       9000
#define MAX_CLIENTS       (MAX_LOBBY_COUNT * MAX_SESSIONS)

#define PLAYER_INACTIVE_KICK_SEC 60
#define SESSION_HARD_TIMEOUT_SEC 600

typedef struct client_s
{
	int socket;
	char uuid[UUID_LEN];
	char name[NAME_MAX_LEN];
	struct timespec last_activity_ts; 
} client_t;

typedef struct session_s
{
	int has_started;
	int ended;	 
	char **list; 
	int players_count;

	clock_t clock;			  
	struct timespec start_ts; 

	pthread_t countdown_tid;
	int countdown_running; 

	pthread_mutex_t lock;
	client_t *players[MAX_LOBBY_COUNT];
} session_t;

typedef struct session_list_s
{
	pthread_mutex_t lock;
	session_t **sessions;
	int count;
} session_list_t;


void init_words_g(void);
session_list_t *create_session_list(void);
void free_session_list(session_list_t *list);
session_t *find_free_session(session_list_t *list);
int add_session(session_list_t *list, session_t *session);
void remove_session(session_list_t *list, session_t *session);

session_t *create_session(void);
int add_player(session_t *session, client_t *client);
void remove_player(session_list_t *list, session_t *session, const char *uuid_str);

int is_correct(const char *target, const char *input);
int wpm(session_t *session, int correct_words);
