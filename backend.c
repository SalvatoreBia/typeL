#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include "backend.h"
#include "network.h"


char **words_g;
int  words_len_g;


int fcount_lines(const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (file == NULL)
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


void init_words_g()
{
	int lines = fcount_lines(WORD_FILE);
	words_len_g = lines;

	FILE *file = fopen(WORD_FILE, "r");
	if (file == NULL)
	{
		perror("***ERROR: failed to open file!");
		exit(EXIT_FAILURE);
	}

	char buf[WORD_MAX_LEN];
	int count = 0;

	words_g = malloc(lines * sizeof(char*));
	if (words_g == NULL)
	{
		perror("***ERROR: failed to allocate words_g!");
		fclose(file);
		exit(EXIT_FAILURE);
	}

	while (fgets(buf, WORD_MAX_LEN, file) && count < lines)
	{
		buf[strcspn(buf, "\n")] = '\0';

		words_g[count] = malloc(strlen(buf) + 1);
		if (words_g[count] == NULL)
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


char **get_chunk()
{
	char **chunk = (char**) malloc(WORD_CHUNK * sizeof(char*));
	if (chunk == NULL)
	{
		perror("***ERROR: failed to allocate memory for word chunk!");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < WORD_CHUNK; i++)
	{
		int random_idx = rand() % words_len_g;
		const char *tmp = words_g[random_idx];
		
		chunk[i] = (char*) malloc(strlen(tmp) + 1);
		if (chunk[i] == NULL)
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


session_t* create_session(int is_single_player)
{
	session_t *session = (session_t*) malloc(sizeof(session_t));
	if (session == NULL)
	{
		perror("***ERROR: failed to initialize typing session!");
		exit(EXIT_FAILURE);
	}

	session->has_started = 0;
	session->is_single_player = is_single_player;
	session->list = get_chunk();
	session->players_count = 0;

	for (int i = 0; i < MAX_LOBBY_COUNT; i++)
		session->player_ids[i][0] = '\0';

	return session;
}

int add_player(session_t *session, const char *uuid_str)
{
	if (session == NULL)
	{
		perror("***ERROR: can't add player since provided session is null!");
		exit(EXIT_FAILURE);
	}

	if (uuid_str == NULL)
		return -1;

	if (session->is_single_player && session->players_count == 1)
		return -1;

	for (int i = 0; i < MAX_LOBBY_COUNT; i++)
	{
		if (session->player_ids[i][0] == '\0')
		{
			strncpy(session->player_ids[i], uuid_str, UUID_LEN - 1);
			session->player_ids[i][UUID_LEN - 1] = '\0';
			session->players_count++;
			return i;
		}
	}

	return 0;
}


void remove_player(session_t *session, const char *uuid_str)
{
	if (session == NULL)
	{
		perror("***ERROR: session is null!");
		exit(EXIT_FAILURE);
	}

	if (session->players_count == 0) return;

	for (int i = 0; i < session->players_count;)
	{
		if (strcmp(session->player_ids[i], uuid_str) == 0)
		{
			session->players_count--;
			session->player_ids[i][0] = '\0';
			return;
		}
		i++;
	}
}


int is_correct(const char *target, const char *input)
{
	return strcmp(target, input) == 0;
}


void free_session(session_t *session)
{
	if (session)
	{
		for (int i = 0; i < WORD_CHUNK; i++)
		{
			free(session->list[i]);
		}

		free(session->list);

		free(session);
	}
}


session_list_t* create_session_list()
{
	session_list_t *list = (session_list_t*) malloc(sizeof(session_list_t));
	if (list == NULL)
	{
		perror("***ERROR: memory allocation failed for session list!");
		exit(EXIT_FAILURE);
	}

	list->count = 0;

	list->sessions = (session_t**) malloc(MAX_SESSIONS * sizeof(session_t*));
	if (list->sessions == NULL)
	{
		perror("***ERROR: memory allocation failed for session object!");
		free(list);
		exit(EXIT_FAILURE);
	}	

	for (int i = 0; i < MAX_SESSIONS; i++)
		list->sessions[i] = NULL;
		
	return list;
}


int add_session(session_list_t *list, session_t *session)
{

	if (session == NULL)
		return 0;
	
	if (list == NULL)
	{
		perror("***ERROR: pointer to session_list_t is null!");
		exit(EXIT_FAILURE);
	}

	if (list->count == MAX_SESSIONS)
		return 0;

	for (int i = 0; i < MAX_SESSIONS; i++)
	{
		if (list->sessions[i] == NULL)
		{
			list->sessions[i] = session;
			list->count++;
			return i;
		}
	}
	
	return 0;
}


void remove_session(session_list_t *list, session_t *session)
{
	if (session == NULL)
		return;
				
	if (list == NULL)
	{
		perror("***ERROR: pointer to session_list_t is null!");
		exit(EXIT_FAILURE);
	}

	if (list->count == 0)
		return;

	for (int i = 0; i < MAX_SESSIONS;)
	{
		if (list->sessions[i] == session)
		{
			free_session(list->sessions[i]);
			list->sessions[i] = NULL;
			list->count--;
			return;
		}

		i++;
	}
}


void free_session_list(session_list_t *list)
{
	if (list)
	{
		for (int i = 0; i < MAX_SESSIONS; i++)
			free_session(list->sessions[i]);
		free(list->sessions);
		free(list);
	}
}

// ~~~~~~~~~~~~~~~~~~~~~ MAIN ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main()
{
	srand(time(NULL));

	init_words_g();

	session_list_t *list = create_session_list();

	start_server(list);

	free_session_list(list);

	return 0;
}
