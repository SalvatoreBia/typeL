#ifndef BACKEND_H
#define BACKEND_H

#include <uuid/uuid.h>
#include <pthread.h>
#include "network.h"

#define WORD_FILE       "word_list.txt"
#define WORD_MAX_LEN    25
#define WORD_CHUNK      50


extern char **words_g;
extern int  words_len_g;


typedef struct __typing_session
{
	int      has_started;
	char     **list;
	client_t *players[MAX_LOBBY_COUNT];
	int      players_count;
	pthread_mutex_t lock;
} session_t;


typedef struct __typing_session_list
{
	session_t **sessions;
	int count;
	pthread_mutex_t lock;
} session_list_t;


session_t* create_session();
int add_player(session_t *session, client_t *client);
void remove_player(session_list_t *list, session_t *session, const char *uuid_str);
void free_session(session_t *session);

session_list_t* create_session_list();
int add_session(session_list_t *list, session_t *session);
void remove_session(session_list_t *list, session_t *session);
void free_session_list(session_list_t *list);
session_t* find_free_session(session_list_t *list);

int is_correct(const char *target, const char *input);
void init_words_g();
char **get_chunk();
int fcount_lines(const char *filename);


#endif // BACKEND_H
