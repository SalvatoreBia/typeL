#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/*
 * A simple typing test game inspired by monkeytype.
 *
 * Words are displayed in a flowing paragraph.  As the user types,
 * each character is coloured green when correct or red when
 * incorrect.  The active character is highlighted with a reverse
 * video caret.  When the user presses space they move on to the
 * next word without needing to hit return.  A timer runs only after
 * the first keypress.  Press TAB + ENTER at any time to restart
 * the test with new random words.  Press ESC to quit.
 */

#define WORDLIST_FILE "word_list.txt"
#define WORD_SIZE_CAP 128
#define DEFAULT_WORD_COUNT 50
#define TEST_DURATION_SEC 30.0

/* Global word list loaded from file. */
static char **word_list = NULL;
static int word_list_lines = 0;

/* Session state. */
typedef struct
{
    char **words;         /* words selected for this session */
    char **typed_words;   /* what the user actually typed for each word */
    int total_words;      /* how many words in this session */
    int current_word;     /* index of the current active word */
    int correct_words;    /* number of correctly typed words */
    int total_typed_chars;/* all characters typed (excluding spaces/backspaces) */
    int correct_chars;    /* correctly typed characters */
    double elapsed_time;  /* seconds since start (once typing has begun) */
} session_t;

/* Count the number of lines in the word list file. */
static int count_lines(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (!file)
        return 0;
    int lines = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF)
        if (ch == '\n')
            lines++;
    fclose(file);
    return lines;
}

/* Load the words from disk into memory. */
static void load_word_list(void)
{
    word_list_lines = count_lines(WORDLIST_FILE);
    if (word_list_lines <= 0)
        return;
    word_list = calloc(word_list_lines, sizeof(char*));
    FILE *file = fopen(WORDLIST_FILE, "r");
    if (!file)
        return;
    char buf[WORD_SIZE_CAP];
    int i = 0;
    while (fgets(buf, sizeof buf, file) && i < word_list_lines)
    {
        buf[strcspn(buf, "\r\n")] = '\0';
        word_list[i] = strdup(buf);
        i++;
    }
    fclose(file);
    word_list_lines = i;
}

/* Free the global word list. */
static void free_word_list(void)
{
    if (!word_list)
        return;
    for (int i = 0; i < word_list_lines; i++)
        free(word_list[i]);
    free(word_list);
    word_list = NULL;
    word_list_lines = 0;
}

/* Choose a number of random words for the session. */
static char **choose_random_words(int count)
{
    if (!word_list || word_list_lines == 0)
        return NULL;
    char **chosen = calloc(count, sizeof(char*));
    for (int i = 0; i < count; i++)
    {
        int idx = rand() % word_list_lines;
        chosen[i] = strdup(word_list[idx]);
    }
    return chosen;
}

/* Initialise a session structure. */
static void init_session(session_t *sess, int word_count)
{
    sess->words = choose_random_words(word_count);
    sess->typed_words = calloc(word_count, sizeof(char*));
    for (int i = 0; i < word_count; i++)
    {
        sess->typed_words[i] = calloc(WORD_SIZE_CAP, sizeof(char));
        sess->typed_words[i][0] = '\0';
    }
    sess->total_words = word_count;
    sess->current_word = 0;
    sess->correct_words = 0;
    sess->total_typed_chars = 0;
    sess->correct_chars = 0;
    sess->elapsed_time = 0.0;
}

/* Free memory allocated by a session structure. */
static void free_session(session_t *sess)
{
    if (sess->words)
    {
        for (int i = 0; i < sess->total_words; i++)
            free(sess->words[i]);
        free(sess->words);
        sess->words = NULL;
    }
    if (sess->typed_words)
    {
        for (int i = 0; i < sess->total_words; i++)
            free(sess->typed_words[i]);
        free(sess->typed_words);
        sess->typed_words = NULL;
    }
}

/* Determine if a word was typed correctly. */
static int word_typed_correctly(const char *expected, const char *typed)
{
    size_t len_exp = strlen(expected);
    size_t len_typ = strlen(typed);
    if (len_exp != len_typ)
        return 0;
    return (strcmp(expected, typed) == 0);
}

/* Draw all words with per-character colouring. */
static void draw_words(WINDOW *win, const session_t *sess, int maxy, int maxx)
{
    int y = 1;
    int x = 2;
    for (int i = 0; i < sess->total_words; i++)
    {
        const char *word = sess->words[i];
        const char *typed = sess->typed_words[i];
        size_t exp_len = strlen(word);
        size_t typ_len = strlen(typed);
        /* wrap to next line if necessary */
        if (x + (int)exp_len >= maxx - 2)
        {
            y++;
            x = 2;
        }
        /* For each position j, determine what to draw and with which attributes. */
        size_t max_len = exp_len;
        if (i == sess->current_word && typ_len > exp_len)
            max_len = typ_len;
        for (size_t j = 0; j < max_len; j++)
        {
            int attr = A_NORMAL;
            char ch;
            if (j < typ_len)
            {
                ch = typed[j];
            }
            else
            {
                ch = (j < exp_len) ? word[j] : ' ';
            }
            /* Determine attribute based on position relative to current word. */
            if (i < sess->current_word)
            {
                /* past words: colour chars red if mistyped else white */
                if (j < exp_len)
                {
                    if (j < typ_len && typed[j] == word[j])
                        attr = COLOR_PAIR(1);
                    else
                        attr = COLOR_PAIR(2);
                }
                else if (j < typ_len)
                {
                    /* extra typed chars beyond expected length are always wrong */
                    attr = COLOR_PAIR(2);
                }
            }
            else if (i == sess->current_word)
            {
                if (j < typ_len)
                {
                    if (j < exp_len && typed[j] == word[j])
                        attr = COLOR_PAIR(1);
                    else
                        attr = COLOR_PAIR(2);
                }
                else if (j == typ_len && j < exp_len)
                {
                    /* the next character to type: highlight caret */
                    attr = A_REVERSE | COLOR_PAIR(1);
                }
                else
                {
                    /* untyped characters */
                    attr = A_DIM | COLOR_PAIR(1);
                }
            }
            else
            {
                /* future words */
                attr = A_DIM | COLOR_PAIR(1);
            }
            wattron(win, attr);
            if (j < exp_len)
            {
                mvwaddch(win, y, x, word[j]);
            }
            else if (j < typ_len)
            {
                /* extra typed characters beyond expected length */
                mvwaddch(win, y, x, typed[j]);
            }
            else
            {
                mvwaddch(win, y, x, ' ');
            }
            wattroff(win, attr);
            x++;
        }
        /* print a space after each word */
        if (x >= maxx - 2)
        {
            y++;
            x = 2;
        }
        else
        {
            /* treat space similarly: done words show typed or expected, active words highlight if at start of new word */
            int attr = A_NORMAL;
            if (i < sess->current_word)
            {
                attr = COLOR_PAIR(1);
            }
            else if (i == sess->current_word)
            {
                if (typ_len > exp_len)
                    attr = COLOR_PAIR(2);
                else if (typ_len == exp_len)
                    attr = A_REVERSE | COLOR_PAIR(1);
                else
                    attr = A_DIM | COLOR_PAIR(1);
            }
            else
            {
                attr = A_DIM | COLOR_PAIR(1);
            }
            wattron(win, attr);
            mvwaddch(win, y, x, ' ');
            wattroff(win, attr);
            x++;
        }
    }
}

/* Run the typing test UI with restart and stats display logic. */
static void run_session(session_t *sess)
{
    /* Initialise curses only once. */
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    int height = LINES - 2;
    int width  = COLS - 2;
    WINDOW *win = newwin(height, width, 1, 1);
    box(win, 0, 0);
    wrefresh(win);

    int running = 1;
    while (running)
    {
        /* Reset session with new random words */
        free_session(sess);
        init_session(sess, DEFAULT_WORD_COUNT);

        int started = 0;
        struct timespec t_start = {0};
        sess->elapsed_time = 0.0;
        int tab_pressed = 0;
        int restart_requested = 0;

        /* Non-blocking during test */
        nodelay(stdscr, TRUE);
        while (1)
        {
            /* update elapsed time if started */
            if (started)
            {
                struct timespec t_now;
                clock_gettime(CLOCK_MONOTONIC, &t_now);
                double diff = (t_now.tv_sec - t_start.tv_sec) + (t_now.tv_nsec - t_start.tv_nsec) / 1e9;
                sess->elapsed_time = diff;
                if (sess->elapsed_time >= TEST_DURATION_SEC || sess->current_word >= sess->total_words)
                    break;
            }
            else
            {
                sess->elapsed_time = 0.0;
            }
            int ch = getch();
            if (ch != ERR)
            {
                if (ch == 27) /* ESC to quit */
                {
                    running = 0;
                    break;
                }
                /* detect tab + enter restart */
                if (ch == '\t')
                {
                    tab_pressed = 1;
                    continue;
                }
                if ((ch == '\n' || ch == KEY_ENTER) && tab_pressed)
                {
                    restart_requested = 1;
                    break;
                }
                tab_pressed = 0; /* any other key resets the tab state */

                if (sess->current_word < sess->total_words)
                {
                    char *expected = sess->words[sess->current_word];
                    char *typed    = sess->typed_words[sess->current_word];
                    size_t typ_len = strlen(typed);
                    size_t exp_len = strlen(expected);
                    /* start timer on first relevant key */
                    if (!started && (isprint(ch) || ch == ' ' || ch == '\n' ||
                                     ch == KEY_BACKSPACE || ch == 127 || ch == 8))
                    {
                        started = 1;
                        clock_gettime(CLOCK_MONOTONIC, &t_start);
                    }
                    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8)
                    {
                        if (typ_len > 0)
                            typed[typ_len - 1] = '\0';
                    }
                    else if (ch == ' ' || ch == '\n')
                    {
                        /* finish current word */
                        for (size_t j = 0; j < strlen(typed); j++)
                        {
                            if (j < exp_len && typed[j] == expected[j])
                                sess->correct_chars++;
                        }
                        if (word_typed_correctly(expected, typed))
                            sess->correct_words++;
                        sess->current_word++;
                    }
                    else if (isprint(ch))
                    {
                        if (typ_len < WORD_SIZE_CAP - 1)
                        {
                            typed[typ_len] = (char)ch;
                            typed[typ_len + 1] = '\0';
                            sess->total_typed_chars++;
                        }
                    }
                }
            }
            /* draw test interface */
            werase(win);
            box(win, 0, 0);
            int maxy, maxx;
            getmaxyx(win, maxy, maxx);
            draw_words(win, sess, maxy, maxx);
            mvwprintw(win, maxy - 2, 2, "Time: %.1f / %.0f s", sess->elapsed_time, TEST_DURATION_SEC);
            wrefresh(win);
            napms(10);
        }
        /* if requested, restart immediately without showing stats */
        if (restart_requested)
            continue;
        if (!running)
            break;

        /* count last word's correct chars if test ended mid-word */
        if (sess->current_word < sess->total_words)
        {
            char *expected = sess->words[sess->current_word];
            char *typed    = sess->typed_words[sess->current_word];
            size_t exp_len = strlen(expected);
            for (size_t j = 0; j < strlen(typed); j++)
            {
                if (j < exp_len && typed[j] == expected[j])
                    sess->correct_chars++;
            }
            if (word_typed_correctly(expected, typed))
                sess->correct_words++;
        }

        /* show stats */
        nodelay(stdscr, FALSE);
        werase(win);
        box(win, 0, 0);
        int row = 2;
        mvwprintw(win, row++, 2, "Test finished!");
        mvwprintw(win, row++, 2, "Elapsed time: %.1f seconds (max %.0f)", sess->elapsed_time, TEST_DURATION_SEC);
        mvwprintw(win, row++, 2, "Words completed: %d", sess->current_word);
        mvwprintw(win, row++, 2, "Correct words: %d", sess->correct_words);
        /* compute total chars including spaces between typed words */
        int spaces_typed = sess->current_word;
        int total_chars  = sess->total_typed_chars + spaces_typed;
        int errors       = total_chars - sess->correct_chars;
        double minutes   = ((sess->elapsed_time > 0.0) ? sess->elapsed_time : 1.0) / 60.0;
        double gross_wpm = (total_chars / 5.0) / minutes;
        double net_wpm   = gross_wpm - (errors / minutes);
        if (net_wpm < 0)
            net_wpm = 0.0;
        double accuracy  = (total_chars > 0) ? ((double)sess->correct_chars / total_chars * 100.0) : 0.0;
        mvwprintw(win, row++, 2, "Total characters (incl. spaces): %d", total_chars);
        mvwprintw(win, row++, 2, "Correct characters: %d", sess->correct_chars);
        mvwprintw(win, row++, 2, "Uncorrected errors: %d", errors);
        mvwprintw(win, row++, 2, "Gross WPM: %.1f", gross_wpm);
        mvwprintw(win, row++, 2, "Net WPM: %.1f", net_wpm);
        mvwprintw(win, row++, 2, "Accuracy: %.1f%%", accuracy);
        mvwprintw(win, row + 1, 2, "Press TAB + ENTER to restart, ESC to quit");
        wrefresh(win);

        /* wait for restart or quit choice */
        tab_pressed = 0;
        while (1)
        {
            int key = getch();
            if (key == 27)
            {
                running = 0;
                break;
            }
            if (key == '\t')
            {
                tab_pressed = 1;
                continue;
            }
            if ((key == '\n' || key == KEY_ENTER) && tab_pressed)
            {
                break; /* restart */
            }
            tab_pressed = 0;
        }
    }

    /* clean up curses */
    delwin(win);
    endwin();
}

int main(void)
{
    srand((unsigned)time(NULL));
    load_word_list();
    if (!word_list || word_list_lines == 0)
    {
        fprintf(stderr, "Failed to load word list from %s\n", WORDLIST_FILE);
        return 1;
    }
    session_t sess;
    /* initial dummy allocation – will be reset in run_session */
    init_session(&sess, DEFAULT_WORD_COUNT);
    run_session(&sess);
    free_session(&sess);
    free_word_list();
    return 0;
}
