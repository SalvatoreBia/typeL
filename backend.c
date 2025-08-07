#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>


#define WORD_FILE       "word_list.txt"
#define WORD_MAX_LEN    25
#define WORD_CHUNK      30

#define UUID_LEN        37
#define MAX_LOBBY_COUNT 8


char **words_g;
int  words_len_g;


typedef struct __typing_session
{
	int    has_started;
	int    is_single_player;
	char   **list;
	char   **next;
	char   player_ids[MAX_LOBBY_COUNT][UUID_LEN];
	int    players_count;
} session_t;


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
		fclose(file);
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
	session->next = get_chunk();
	session->players_count = 0;

	return session;
}

int add_player(session_t *session, const char *uuid_str)
{
	if (session == NULL)
	{
		perror("***ERROR: can't add player since provided session is null!");
		exit(EXIT_FAILURE);
	}

	if (session->players_count == MAX_LOBBY_COUNT)
		return 0;

	strncpy(session->player_ids[session->players_count], uuid_str, UUID_LEN - 1);
	session->player_ids[session->players_count][UUID_LEN - 1] = '\0';

	session->players_count++;

	return session->players_count;
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
			return;
		}
		i++;
	}
}


int is_correct(const char *target, const char *input)
{
	return strcmp(target, input) == 0;
}


void swap_session_words(session_t *session)
{
	if (session == NULL)
	{
		perror("ERROR: session is null!");
		exit(EXIT_FAILURE);
	}

	if (session->list) 
	{
		for (int i = 0; i < WORD_CHUNK; i++)
			free(session->list[i]);
		free(session->list);
	}

	session->list = session->next;

	session->next = get_chunk();
	if (!session->next) 
	{
		perror("ERROR: failed to generate next chunk!");
		exit(EXIT_FAILURE);
	}
}



void free_session(session_t *session)
{
	if (session)
	{
		for (int i = 0; i < WORD_CHUNK; i++)
		{
			free(session->list[i]);
			free(session->next[i]);
		}

		free(session->list);
		free(session->next);

		free(session);
	}
}


int main(int argc, char **argv)
{
	srand(time(NULL));
	
	init_words_g();

	return 0;
}
