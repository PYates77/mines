#include "curses.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <argp.h>

#define MINE_GEN_RATIO 6 // num mines = (width*height)/MINE_GEN_RATIO

enum cell_state {
    COVERED,
    UNCOVERED,
    FLAGGED,
    EXPLODED,
};

/*
 * Labels for curses color pairs
 * The colors for numbers are mapped so we can index them by integer value and an offset
 */
enum cell_schemes {
    COLOR_ONE = 1,
    COLOR_TWO = 2,
    COLOR_THREE = 3,
    COLOR_FOUR = 4,
    COLOR_FIVE = 5,
    COLOR_SIX = 6,
    COLOR_SEVEN = 7,
    COLOR_EIGHT = 8,
    COLOR_ONE_SELECTED = 9,
    COLOR_TWO_SELECTED = 10,
    COLOR_THREE_SELECTED = 11,
    COLOR_FOUR_SELECTED = 12,
    COLOR_FIVE_SELECTED = 13,
    COLOR_SIX_SELECTED = 14,
    COLOR_SEVEN_SELECTED = 15,
    COLOR_EIGHT_SELECTED = 16,
    COLOR_UNSELECTED,
    COLOR_SELECTED,
    COLOR_EXPLODED,
    COLOR_FLAGGED,
    COLOR_FLAGGED_SELECTED,
};

struct cell {
    bool is_mine;
    enum cell_state state;
    int neighbors;
};

static int height = 20;
static int width = 20;
static int num_mines = 60;
static int cursor_x = 0;
static int cursor_y = 0;
static bool game_active = true;
static bool new_game = false; // flag to reset all cells in the middle of a game

// don't calculate an appropriate # of mines based on game size
static bool custom_num_mines = false; 

static char covered = '#';
static char flagged = 'F';
static char exploded = '*';

struct cell *cells;


/* In an effort to make more legit apps, I might as well practice using argp */
const char *argp_program_version = "Version 1.1\nAuthor: Paul Yates, github.com/pyates77\n";
static char doc[] = "A simple in-terminal minesweeper game\n"
                    "Uncover all the tiles that don't have a mine under them!\n"
                    "Don't uncover a mine or it's game over!\n"
                    "Uncovering a safe square reveals the number of adjacent mines!\n\n"
                    "Controls: \n"
                    "\tArrow keys (or vim directions): move the cursor around\n"
                    "\tSpace or Z: uncover a minesweeper tile\n"
                    "\tF or X: put a flag on a minesweeper tile\n"
                    "\tN: new game\n"
                    "\tQ: quit the game\n"
                    "\nIf you do not specify number of mines, one sixth "
                    "of the tiles will contain mines. The first tile you uncover will "
                    "never be a mine.\n";

static char args_doc[] = "-h height -w width -m mines";
static struct argp_option options[] = {
    {"height", 'h', "HEIGHT", 0, "height of the game board in tiles"},
    {"width", 'w', "WIDTH", 0, "width of the game board in tiles"},
    {"mines", 'm', "MINES", 0, "number of mines on the game board"},
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments = state->input;
    errno = 0;
    switch (key) {
    case 'h':
        if (sscanf(arg, "%i", &height) != 1) {
            printf("'%s' is not a valid integer height\n", arg);
            game_active = false;
        }
        break;
    case 'w':
        if (sscanf(arg, "%i", &width) != 1) {
            printf("'%s' is not a valid integer width\n", arg);
            game_active = false;
        }
        break;
    case 'm':
        if (sscanf(arg, "%i", &num_mines) != 1) {
            printf("'%s' is not a valid integer number of mines\n", arg);
            game_active = false;
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

void uncover(int x, int y)
{
    //printf("Uncovering (%d, %d)\r\n", x, y);
    struct cell *c = &cells[y*width+x];
    if (c->state == COVERED) {
        if (c->is_mine) {
            c->state = EXPLODED;
        } else {
            c->state = UNCOVERED;
            // uncovering a zero causes all neighbors to be uncovered
            if (c->neighbors == 0) {
                for (int j=y-1; j<=y+1; ++j) {
                    for (int i=x-1; i<=x+1; ++i) {
                        if (i >= 0 && i < width && j >= 0 && j < height) {
                            if (!(i == x && j == y)) {
                                uncover(i,j);
                            }
                        }
                    }
                }
            }
        }
    } else if (c->state == UNCOVERED) {
        // attempting to uncover an uncovered square with n neighbors,
        // if there are n adjacent flags, uncover all unflagged neighbors
        int flags_remaining = c->neighbors;
        for (int j=y-1; j<=y+1; ++j) {
            for (int i=x-1; i<=x+1; ++i) {
                if (i >= 0 && i < width && j >= 0 && j < height) {
                    if (!(i == x && j == y)) {
                        if (cells[j*width+i].state == FLAGGED) {
                            --flags_remaining;
                        }
                    }
                }
            }
        }

        if (flags_remaining == 0) {
            for (int j=y-1; j<=y+1; ++j) {
                for (int i=x-1; i<=x+1; ++i) {
                    if (i >= 0 && i < width && j >= 0 && j < height) {
                        if (!(i == x && j == y)) {
                            if (cells[j*width+i].state == COVERED) {
                                uncover(i,j);
                            }
                        }
                    }
                }
            }
        }
    }
}

void flag(int x, int y)
{
    struct cell *c = &cells[y*width+x];
    if (c->state == COVERED) {
        c->state = FLAGGED;
    } else if (c->state == FLAGGED) {
        c->state = COVERED;
    }
}

void generate_mines(int start_x, int start_y)
{
    int i=0;

    while (i < num_mines) {
        int x = rand()%width;
        int y = rand()%height;
        // it is impossible to pick a mine on the first click
        if (!(x == start_x && y == start_y)) {
            struct cell *c = &cells[width*y+x];
            if (!c->is_mine) {
                c->is_mine = true;
                ++i;
            }
        }
    }
}

void calculate_neighbors()
{
    for (int y=0; y<height; ++y) {
        for (int x=0; x<width; ++x) {
            int num_neighbors = 0;
            for (int j=y-1; j<=y+1; ++j) {
                for (int i=x-1; i<=x+1; ++i) {
                    if (i >= 0 && i < width && j >= 0 && j < height) {
                        if (!(i == x && j == y)) {
                            if (cells[j*width+i].is_mine) {
                                ++num_neighbors;
                            }
                        }
                    }
                }
            }
            cells[y*width+x].neighbors = num_neighbors;
        }
    }
}

void draw()
{
    bool gameover = false;
    int unflagged_mines = num_mines;
    int uncovers_remaining = height * width - num_mines;
    for (int y=0; y<height; ++y) {
        for (int x=0; x<width; ++x) {
            //printf("Drawing square (%d, %d) with cursor at (%d, %d)\r\n", x, y, cursor_x, cursor_y);
            struct cell *c = &cells[width*y+x];
            bool selected = false;
            if (cursor_x == x && cursor_y == y) {
                attron(COLOR_PAIR(COLOR_SELECTED));
                selected = true;
            } else {
                attron(COLOR_PAIR(COLOR_UNSELECTED));
            }

            char symbol = ' ';
            switch (c->state) {
            case COVERED:
                symbol = covered;
                break;
            case UNCOVERED:
                if (c->neighbors == 0) {
                    symbol = ' ';
                } else {
                    symbol = '0' + c->neighbors;
                    if (selected) {
                        // clever enum usage allows us to do this indexing
                        attron(COLOR_PAIR(c->neighbors + 8));
                    } else {
                        attron(COLOR_PAIR(c->neighbors));
                    }
                }
                --uncovers_remaining;
                break;
            case FLAGGED:
                if (selected) {
                    attron(COLOR_PAIR(COLOR_FLAGGED_SELECTED));
                } else {
                    attron(COLOR_PAIR(COLOR_FLAGGED));
                }
                symbol = flagged;
                --unflagged_mines;
                break;
            case EXPLODED:
                attron(COLOR_PAIR(COLOR_EXPLODED));
                symbol = exploded;
                gameover = true;
                break;
            }

            mvaddch(y, 2*x+1, symbol);
            attron(COLOR_PAIR(COLOR_UNSELECTED));
            mvaddch(y, 2*x, ' '); // looks nicer if we color in the spaces between squares

            // clear the status line of previous text
            move(height+1, 0);
            clrtoeol();
            if (gameover) {
                // loss condition
                mvaddstr(height+1, 0, "Game Over");
            } else {
                if (uncovers_remaining == 0) {
                    // win condition
                    mvaddstr(height+0, 0, "You Win!");
                } else {
                    // game in progress condition
                    char buf[50];
                    snprintf(buf, sizeof(buf), "Unflagged Mines: %d", unflagged_mines);
                    mvaddstr(height+1, 0, buf);
                }
            }
        }
    }
};

int main(int argc, char **argv)
{
    struct arguments *arguments;

    argp_parse(&argp, argc, argv, 0, 0, &arguments);
    // I'm not a huge fan of dynamic memory allocation, but I might as well practice sometimes
    cells = malloc(height*width*sizeof(struct cell));
    memset(cells, 0, height*width*sizeof(struct cell));

    if (!custom_num_mines) {
        num_mines = (width*height)/MINE_GEN_RATIO;
    }

    srand(time(NULL));

    // set up curses
    initscr();
    timeout(-1); //we want getch to block
    start_color();
    use_default_colors();
    noecho();
    keypad(stdscr,TRUE);
    curs_set(0);
    nodelay(stdscr, TRUE);
    leaveok(stdscr, TRUE);
    scrollok(stdscr, FALSE);
    init_pair(COLOR_UNSELECTED, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_SELECTED, COLOR_GREEN, COLOR_WHITE);
    init_pair(COLOR_EXPLODED, COLOR_WHITE, COLOR_RED);
    init_pair(COLOR_FLAGGED, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_FLAGGED_SELECTED, COLOR_RED, COLOR_WHITE);
    init_pair(COLOR_ONE, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_TWO, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_THREE, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_FOUR, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_FIVE, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_SIX, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_SEVEN, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_EIGHT, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_ONE_SELECTED, COLOR_BLUE, COLOR_WHITE);
    init_pair(COLOR_TWO_SELECTED, COLOR_GREEN, COLOR_WHITE);
    init_pair(COLOR_THREE_SELECTED, COLOR_RED, COLOR_WHITE);
    init_pair(COLOR_FOUR_SELECTED, COLOR_BLUE, COLOR_WHITE);
    init_pair(COLOR_FIVE_SELECTED, COLOR_MAGENTA, COLOR_WHITE);
    init_pair(COLOR_SIX_SELECTED, COLOR_CYAN, COLOR_WHITE);
    init_pair(COLOR_SEVEN_SELECTED, COLOR_YELLOW, COLOR_WHITE);
    init_pair(COLOR_EIGHT_SELECTED, COLOR_RED, COLOR_WHITE);

    bool generated = false;
    while(game_active) {
        if (new_game) {
            memset(cells, 0, height*width*sizeof(struct cell));
            generated = false;
            new_game = false;
        }

        // I'm also gonna allow vim bindings instead of arrow keys because why not
        // getch is a blocking call because the screen doesn't need to refresh unless input recvd
        int ch = getch();
        switch(ch) {
        case KEY_RIGHT:
        case 'l':
            if (cursor_x < width-1) {
                ++cursor_x;
            }
            break;
        case KEY_LEFT:
        case 'h':
            if (cursor_x > 0) {
                --cursor_x;
            }
            break;
        case KEY_UP:
        case 'k':
            if (cursor_y > 0) {
                --cursor_y;
            }
            break;
        case KEY_DOWN:
        case 'j':
            if (cursor_y < height-1) {
                ++cursor_y;
            }
            break;
        case ' ':
        case 'z':
            if (!generated) {
                generate_mines(cursor_x, cursor_y);
                calculate_neighbors();
                generated = true;
            }

            uncover(cursor_x, cursor_y);
            break;
        case 'x':
        case 'f':
            flag(cursor_x, cursor_y);
            break;
        case 'n':
            new_game = true;
            break;
        case 'q':
            game_active = false;
            break;
        }

        draw();
        refresh();
    }

    endwin();
    free(cells);
    return 0;
}
