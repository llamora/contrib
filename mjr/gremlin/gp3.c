#include <dirent.h>
#include <curses.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcp.h>
#include "gt.h"

#define MAX_LIST 1024
#define MAX_WIDTH 512

void curses_init ();
void load_config (int argc, char **argv);
void load_root ();
void refresh_left ();
void refresh_right ();
void dialog (char *message);
void update_list ();
void pl_add (char *name, char *uri);
void pl_update ();
void * pl_thread (void *args);
void my_wait();

void key_up ();
void key_down ();
void key_npage ();
void key_ppage ();
void key_enter ();
void key_left ();
void key_tab ();
void key_backspace ();
void key_add_all ();
void key_pause ();
void key_quit ();

char home[256];
int curline, offset;
enum {LEFT, RIGHT};
int focus;

int htl;
int threads;
int buffer;

WINDOW *win_left, *win_right, *pad_left, *pad_right;

gremlin_tree *gt;
gt_entity *ls;
char **catalogs;

struct pl_item {
    struct pl_item *next;
    pthread_t thread;
    fcp_document *d;
    int id;
    char *name, *uri;
    FILE *data, *mpg123;
};

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
int pl_len, paused, counter;
struct pl_item *head = NULL, *tail;
long last_del;

void
curses_init ()
{
    initscr(); cbreak(); nonl(); keypad(stdscr, TRUE); curs_set(0); noecho();
    win_left = newwin(LINES, COLS/2, 0, 0);
    win_right = newwin(LINES, COLS/2, 0, COLS/2);
    box(win_left, 0, 0); box(win_right, 0, 0);
    wnoutrefresh(stdscr); wnoutrefresh(win_left); wrefresh(win_right);
    pad_left = newpad(MAX_LIST, MAX_WIDTH);
    pad_right = newpad(MAX_LIST, MAX_WIDTH);
    focus = LEFT; pl_len = 0; counter = 0; paused = 0;
}

void
usage (char *me)
{
    fprintf(stderr, "Usage %s [options]\n\n"
	    	    "  -h --htl x              Hops to live for requests. (default 20)\n"
		    "  -t --threads x          Concurrency per request. (default 20)\n"
		    "  -b --buffer x           Number of songs to buffer ahead. (default 2)\n"
		    "  -c --checkout uri       Checks out catalog from URI.\n\n",
		    me);
    exit(2);
}

void
load_config (int argc, char **argv)
{
    int c;
    extern int optind;
    extern char *optarg;

    static struct option long_options[] =
    {
        {"htl",       1, NULL, 'h'},
        {"threads",   1, NULL, 't'},
	{"buffer",    1, NULL, 'b'},
	{"checkout",  1, NULL, 'c'},
	{0, 0, 0, 0}
    };

    char *uri = NULL;
    DIR *dir;
    
    htl = 20;
    threads = 20;
    buffer = 2;
     
    last_del = 0;
    sprintf(home, "%s/.gp3", getenv("HOME"));
    
    while ((c = getopt_long(argc, argv,
		    "h:t:b:c:", long_options, NULL)) != EOF) {
	switch (c) {
	case 'h':
	    htl = atoi(optarg);
	    break;
	case 't':
	    threads = atoi(optarg);
	    break;
	case 'b':
	    buffer = atoi(optarg);
	    break;
	case 'c':
	    uri = strdup(optarg);
	    break;
	case '?':
	    usage(argv[0]);
	    break;
	}
    }

    if (optind != argc) usage(argv[0]);

    if (htl <= 0) {
	fprintf(stderr, "Invalid hops to live.\n");
	exit(2);
    }
    if (threads <= 0) {
	fprintf(stderr, "Invalid number of threads.\n");
	exit(2);
    }
    if (buffer < 0) {
	fprintf(stderr, "Invalid buffer value.\n");
	exit(2);
    }

    dir = opendir(home);
    if (!dir) {
        printf("Creating directory %s... ", home);
        if (mkdir(home, 0755) == 0) {
	    printf("done.\n");
	} else {
	    printf("failed!\n");
	    exit(1);
	}
    }
    closedir(dir);
    
    if (uri) {
	FILE *f, *data = tmpfile();
	char buf[1024];
	gremlin_tree g;
	fcp_document *d = fcp_document_new();
	int b, n, length;
	printf("Requesting %s... ", uri);
	length = fcp_request(NULL, d, uri, htl, threads);
	if (length < 0) {
	    printf("%s!\n", fcp_status_to_string(length));
	    exit(1);
	}
	b = length;
	while (length) {
	    length -= (n = fcp_read(d, buf, 1024));
	    if (n <= 0) {
		printf("Transfer failed: %s!\n",
			fcp_status_to_string(length));
		exit(1);
	    }
	    fwrite(buf, 1, n, data);
	}
	printf("%d bytes read.\n", b);
	rewind(data);
	n = gt_init(&g, data);
	if (n != 0) {
	    printf("Not a catalog file!\n");
	    exit(1);
	}
	printf("Adding %s to %s... ", g.names[0], home);
	sprintf(buf, "%s/%s", home, g.names[0]);
	gt_close(&g);
	rewind(data);
	f = fopen(buf, "w");
	if (!f) {
	    printf("failed!\n");
	    exit(1);
	}
	while (b--) fputc(fgetc(data), f);
	printf("done.\n");
	exit(0);
    }
}

void
load_root ()
{
    int i, n;
    struct dirent **namelist;
    
    gt = NULL;
    if (catalogs) {
	for (i = 0 ; catalogs[i] ; i++) free(catalogs[i]);
	free(catalogs);
    }
    
    n = scandir(home, &namelist, 0, alphasort);
    catalogs = calloc(n - 1, sizeof(char *));
    for (i = 2 ; i < n ; i++) {
	catalogs[i - 2] = strdup(namelist[i]->d_name);
	free(namelist[i]);
    }
    catalogs[n - 2] = NULL;
    free(namelist);
    
    curline = 0;
    offset = 0;
    werase(pad_left);
    if (!catalogs[0]) {
	dialog("No catalogs. Quit and try gp3 --help for instructions.");
	return;
    }
    wattron(pad_left, A_REVERSE);
    mvwaddstr(pad_left, 0, 0, catalogs[0]);
    wattroff(pad_left, A_REVERSE);
    for (i = 1 ; catalogs[i] ; i++)
        mvwaddstr(pad_left, i, 0, catalogs[i]);
    
    refresh_left();
}

void
update_list ()
{
    int i;
    curline = 0;
    offset = 0;
    werase(pad_left);
    if (!ls[0].name) return;
    wattron(pad_left, A_REVERSE);
    mvwaddstr(pad_left, 0, 0, ls[0].name);
    wattroff(pad_left, A_REVERSE);
    for (i = 1 ; ls[i].name ; i++)
	mvwaddstr(pad_left, i, 0, ls[i].name);
}

void
dialog (char *message)
{
    char s[] = "Press any key to continue.";
    WINDOW *dialog = newwin(LINES/8, COLS, LINES/2 - LINES/8, 0);
    box(dialog, 0, 0);
    mvwaddstr(dialog, 1, COLS/2 - strlen(message)/2, message);
    mvwaddstr(dialog, LINES/8 - 2, COLS/2 - strlen(s)/2, s);
    wrefresh(dialog);
    getch();
    box(win_left, 0, 0); box(win_right, 0, 0);
    wnoutrefresh(stdscr); wnoutrefresh(win_left); wrefresh(win_right);
    refresh_left(); refresh_right();
}

void
refresh_right ()
{
    prefresh(pad_right, offset, 0, 1, COLS/2+1, LINES-2, COLS-2);
}

void
refresh_left ()
{
    prefresh(pad_left, offset, 0, 1, 1, LINES-2, COLS/2-2);
}

int
main (int argc, char **argv)
{
    load_config(argc, argv);
    curses_init();
    load_root();
    while (1) {
	switch(getch()) {
	    case KEY_DOWN:      key_down();      break; // scroll down
	    case KEY_UP:        key_up();        break; // scroll up
	    case KEY_NPAGE:     key_npage();     break; // page down
	    case KEY_PPAGE:     key_ppage();     break; // page up
	    case 13:
	    case KEY_RIGHT:     key_enter();     break; // select
	    case KEY_LEFT:      key_left();      break; // go back
	    case '\t':          key_tab();       break; // switch focus
	    case KEY_BACKSPACE: key_backspace(); break; // remove plist entry
	    case 'd':           key_add_all();   break; // queue all songs in dir
	    case 'p':           key_pause();     break; // pause
	    case 'q':           key_quit();      break; // quit
	    default:            beep();          break;
	}
    }
}

void
key_up ()
{
    if (curline == 0) return;
    pthread_mutex_lock(&mutex);
    if (focus == LEFT) {
        if (!gt) { // root catalogs
    	    mvwaddstr(pad_left, curline, 0, catalogs[curline]);
            wattron(pad_left, A_REVERSE);
            curline--;
            mvwaddstr(pad_left, curline, 0, catalogs[curline]);
            wattroff(pad_left, A_REVERSE);
        } else { // in the tree
	    mvwaddstr(pad_left, curline, 0, ls[curline].name);
            wattron(pad_left, A_REVERSE);
            curline--;
            mvwaddstr(pad_left, curline, 0, ls[curline].name);
            wattroff(pad_left, A_REVERSE);
        }
        if (curline == offset - 1) offset--;
    	refresh_left();
    } else {
	if (--curline == offset - 1) offset--;
	pl_update();
    }
    pthread_mutex_unlock(&mutex);
}

void
key_down ()
{
    pthread_mutex_lock(&mutex);
    if (focus == LEFT) {
        if (!gt) { // root catalogs
            if (!catalogs[curline+1]) goto bye;
            mvwaddstr(pad_left, curline, 0, catalogs[curline]);
            wattron(pad_left, A_REVERSE);
            curline++;
            mvwaddstr(pad_left, curline, 0, catalogs[curline]);
            wattroff(pad_left, A_REVERSE);
        } else { // in the tree
	    if (!ls[curline+1].name) goto bye;
	    mvwaddstr(pad_left, curline, 0, ls[curline].name);
            wattron(pad_left, A_REVERSE);
            curline++;
            mvwaddstr(pad_left, curline, 0, ls[curline].name);
            wattroff(pad_left, A_REVERSE);
        }
        if (curline - offset == LINES - 2) offset++;
        refresh_left();
    } else {
	if (curline >= pl_len - 1) goto bye;
	if (++curline - offset == LINES - 2) offset++;
	pl_update();
    }
bye:
    pthread_mutex_unlock(&mutex);
}

void
key_npage ()
{
    mvwaddstr(pad_left, curline, 0, ls[curline].name);
    offset += LINES - 2;
    curline += LINES - 2;
    refresh_left();
}

void
key_ppage ()
{
    mvwaddstr(pad_left, curline, 0, ls[curline].name);
    offset -= LINES - 2;
    curline -= LINES - 2;
    refresh_left();
}

void
key_enter ()
{
    FILE *in;
    int status;
    char path[256];
    if (focus == RIGHT) return; // this means nothing for playlist stuff
    if (!gt) { // open a catalog
	sprintf(path, "%s/%s", home, catalogs[curline]);
	in = fopen(path, "r");
	if (!in) {
	    sprintf(path, "Can't open catalog ~/.gp3/%s!", catalogs[curline]);
	    dialog(path);
	    return;
	}
	gt = malloc(sizeof(gremlin_tree));
	status = gt_init(gt, in);
	if (status != 0) {
	    sprintf(path, "Invalid catalog ~/.gp3/%s!", catalogs[curline]);
	    dialog(path);
	    free(gt); gt = NULL; return;
	}
	ls = gt_ls(gt);
	update_list();
	refresh_left();
    } else {
	if (ls[curline].type == GT_DIR) { // cd
	    gt_cd(gt, ls[curline].name, ls[curline].data);
	    gt_free(ls); ls = gt_ls(gt);
	    update_list();
	    refresh_left();
	} else { // add to playlist
	    pl_add(ls[curline].name, ls[curline].data);
	}
    }
}

void
pl_add (char *name, char *uri)
{
    struct pl_item *new = malloc(sizeof(struct pl_item));
    pthread_mutex_lock(&mutex);
    new->name = strdup(name); new->uri = strdup(uri);
    new->data = NULL; new->next = NULL; new->id = counter++;
    new->mpg123 = NULL; new->d = NULL;
    if (!head) { // first node
	head = tail = new;
    } else {
	tail->next = new;
	tail = new;
    }
    pl_len++;
    pl_update();
    pthread_create(&new->thread, NULL, pl_thread, (void *) new);
    pthread_detach(new->thread);
    pthread_mutex_unlock(&mutex);
}

void
key_left ()
{
    if (focus == RIGHT) return; // nope
    if (!gt) return; // already at root
    if (gt->depth == 0) { // cd to root
	load_root();
    } else {
	gt_cd(gt, "..", NULL);
        gt_free(ls); ls = gt_ls(gt);
	update_list();
    }
    refresh_left();
}

void
key_tab ()
{
    pthread_mutex_lock(&mutex);
    if (focus == LEFT) {
	mvwaddstr(pad_left, curline, 0, gt ? ls[curline].name : catalogs[curline]);
	focus = RIGHT;
	curline = 0; offset = 0;
	refresh_left();
	pl_update();
    } else {
        curline = 0; offset = 0;
	wattron(pad_left, A_REVERSE);
	mvwaddstr(pad_left, curline, 0, gt ? ls[curline].name : catalogs[curline]);
	wattroff(pad_left, A_REVERSE);
	focus = LEFT;
	refresh_left();
	pl_update();
    }
    pthread_mutex_unlock(&mutex);
}

void
key_backspace ()
{
    int i = 0;
    struct pl_item *p, *last = NULL, *pli = head;
    if (focus == LEFT) return; // nope, sorry
    pthread_mutex_lock(&mutex);
    while (pli) {
	if (i == curline) {
	    if (last) last->next = pli->next;
	    else head = pli->next;
	    if (last && !last->next) tail = last;
	    if (i == --pl_len) curline--;
	    if (last && head->id >= pli->id - buffer)
		for (p = pli->next ; p ; p = p->next) p->id--;
	    pthread_cancel(pli->thread);
//	    if (pli->d) fcp_close(pli->d);
	    if (pli->data) fclose(pli->data);
	    if (pli->mpg123) pclose(pli->mpg123);
	    free(pli->name); free(pli->uri); free(pli);
    	    pl_update();
	    pthread_mutex_unlock(&mutex);
	    pthread_cond_broadcast(&cond);
	    last_del = time(NULL);
	    break;
	}
	last = pli;
	pli = pli->next;
	i++;
    }
    pthread_mutex_unlock(&mutex);
}

void
key_add_all ()
{
    int i;
    if (!ls) return;
    for (i = 0 ; ls[i].name ; i++)
	if (ls[i].type == GT_FILE)
	    pl_add(ls[i].name, ls[i].data);
}

void
key_pause ()
{
    pthread_mutex_lock(&mutex);
    if (paused) paused = 0; else paused = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
}

void
key_quit ()
{
    clear();
    endwin();
    exit(0);
}

void
pl_update ()
{
    int i = 0;
    char line[512];
    struct pl_item *pli = head;
    werase(pad_right);
    while (pli) {
	sprintf(line, "%s", pli->name);
	if (focus == RIGHT && i == curline) wattron(pad_right, A_REVERSE);
	mvwaddstr(pad_right, i, 0, line);
	if (focus == RIGHT && i == curline) wattroff(pad_right, A_REVERSE);
	pli = pli->next;
	i++;
    }
    refresh_right();
}

void *
pl_thread (void *args)
{
    struct pl_item *pli = (struct pl_item *) args;
    char buf[1024];
    int n, length;    
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while (head->id < pli->id - buffer) my_wait();
    while (time(NULL) < last_del + 3) sleep(1);
    pli->d = fcp_document_new();
    length = fcp_request(NULL, pli->d, pli->uri, htl, threads);
    pthread_mutex_lock(&mutex);
    while (length == FCP_CONNECT_FAILED) {
	dialog("Connection to local Freenet node failed. Will retry.");
	length = fcp_request(NULL, pli->d, pli->uri, htl, threads);
    }
    pthread_mutex_unlock(&mutex);
    while (pli != head) my_wait();
    pli->mpg123 = popen("mpg123 - &>/dev/null", "w");
    if (!pli->mpg123) length = 0;
    while (length > 0) {
	while (paused) my_wait();
	length -= (n = fcp_read(pli->d, buf, 1024));
	if (!n) break;
	if (fwrite(buf, 1, n, pli->mpg123) != n) break;
    }
    pthread_mutex_lock(&mutex);
    fcp_close(pli->d);
    if (pli->mpg123) pclose(pli->mpg123);
    pl_len--;
    if (focus == RIGHT && curline > 0) curline--;
    head = pli->next;
    free(pli->name); free(pli->uri); free(pli);
    pl_update();
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
    pthread_exit(NULL);
}

void
my_wait ()
{
    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);
}
