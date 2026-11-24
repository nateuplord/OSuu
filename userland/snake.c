#include "kernel_api.h"
#include "string.h"
#include <stdbool.h>
#include <stdint.h>

#define SNAKE_COLS 30u
#define SNAKE_ROWS 24u
#define SNAKE_CELL 16u
#define SNAKE_BOARD_X 80u
#define SNAKE_BOARD_Y 72u
#define SNAKE_BOARD_WIDTH (SNAKE_COLS * SNAKE_CELL)
#define SNAKE_BOARD_HEIGHT (SNAKE_ROWS * SNAKE_CELL)
#define SNAKE_MAX_LENGTH 200u
#define SNAKE_MOVES_PER_SECOND 10u
#define SNAKE_MOVE_INTERVAL_TICKS (PIT_HZ / SNAKE_MOVES_PER_SECOND)

#define SNAKE_SOUND_START_HZ 660u
#define SNAKE_SOUND_START_MS 80u
#define SNAKE_SOUND_KEY_HZ 1200u
#define SNAKE_SOUND_KEY_MS 40u
#define SNAKE_SOUND_UP_HZ 523u
#define SNAKE_SOUND_RIGHT_HZ 587u
#define SNAKE_SOUND_DOWN_HZ 659u
#define SNAKE_SOUND_LEFT_HZ 784u
#define SNAKE_SOUND_FOOD_HZ 880u
#define SNAKE_SOUND_FOOD_MS 80u
#define SNAKE_SOUND_GAME_OVER_HZ 220u
#define SNAKE_SOUND_GAME_OVER_MS 300u

#define SNAKE_DIR_UP 0u
#define SNAKE_DIR_RIGHT 1u
#define SNAKE_DIR_DOWN 2u
#define SNAKE_DIR_LEFT 3u

#define SNAKE_COLOR_BG 0x00u
#define SNAKE_COLOR_FIELD 0x01u
#define SNAKE_COLOR_BORDER 0x0Bu
#define SNAKE_COLOR_BODY 0x02u
#define SNAKE_COLOR_HEAD 0x0Au
#define SNAKE_COLOR_EYE 0x0Fu
#define SNAKE_COLOR_FOOD 0x0Cu
#define SNAKE_COLOR_FOOD_HI 0x0Eu
#define SNAKE_COLOR_TITLE 0x0Eu
#define SNAKE_COLOR_TEXT 0x0Fu

static uint8_t snake_x[SNAKE_MAX_LENGTH];
static uint8_t snake_y[SNAKE_MAX_LENGTH];
static uint8_t snake_length;
static uint8_t snake_dir = SNAKE_DIR_RIGHT;
static uint8_t snake_pending_dir = SNAKE_DIR_RIGHT;
static uint8_t snake_food_x;
static uint8_t snake_food_y;
static uint32_t snake_score;
static uint32_t snake_rng;

static void snake_write_text(
    uint8_t row,
    uint8_t col,
    const char *text,
    uint8_t color
) {
    while (*text != '\0') {
        framebuffer_write(row, col, *text, color, SNAKE_COLOR_BG);
        ++col;
        ++text;
    }
}

static void snake_write_uint(
    uint32_t value,
    uint8_t col
) {
    char digits[10];
    uint8_t length = 0;
    if (value == 0) {
        digits[length++] = '0';
    }
    while (value != 0) {
        digits[length++] = (char) ('0' + value % 10u);
        value /= 10u;
    }

    for (uint8_t i = 0; i < length; ++i) {
        framebuffer_write(
            2, (uint8_t) (col + i), digits[length - 1 - i], SNAKE_COLOR_TEXT,
            SNAKE_COLOR_BG
        );
    }
}

static void snake_draw_hud(void) {
    gui_fill_rect(0, 0, 640, 40, SNAKE_COLOR_BG);
    snake_write_text(1, 1, "SNAKE", SNAKE_COLOR_TITLE);
    snake_write_text(2, 1, "SCORE ", SNAKE_COLOR_TEXT);
    snake_write_uint(snake_score, 7);
    snake_write_text(3, 1, "WASD / ARROWS MOVE   Q QUIT", SNAKE_COLOR_TEXT);
}

static void snake_draw_cell(
    uint8_t x,
    uint8_t y,
    uint8_t color
) {
    gui_fill_rect(
        (uint16_t) (SNAKE_BOARD_X + x * SNAKE_CELL),
        (uint16_t) (SNAKE_BOARD_Y + y * SNAKE_CELL), SNAKE_CELL - 1u,
        SNAKE_CELL - 1u, color
    );
}

static void snake_draw_board(void) {
    gui_fill_rect(
        SNAKE_BOARD_X - 4u, SNAKE_BOARD_Y - 4u, SNAKE_BOARD_WIDTH + 8u,
        SNAKE_BOARD_HEIGHT + 8u, SNAKE_COLOR_BORDER
    );
    gui_fill_rect(
        SNAKE_BOARD_X, SNAKE_BOARD_Y, SNAKE_BOARD_WIDTH, SNAKE_BOARD_HEIGHT,
        SNAKE_COLOR_FIELD
    );
}

static void snake_draw_food(void) {
    uint16_t x = (uint16_t) (SNAKE_BOARD_X + snake_food_x * SNAKE_CELL);
    uint16_t y = (uint16_t) (SNAKE_BOARD_Y + snake_food_y * SNAKE_CELL);

    gui_fill_rect(x, y, SNAKE_CELL - 1u, SNAKE_CELL - 1u, SNAKE_COLOR_FOOD);
    gui_fill_rect(x + 6u, y + 6u, 4u, 4u, SNAKE_COLOR_FOOD_HI);
}

static void snake_draw_head(void) {
    uint16_t base_x = (uint16_t) (SNAKE_BOARD_X + snake_x[0] * SNAKE_CELL);
    uint16_t base_y = (uint16_t) (SNAKE_BOARD_Y + snake_y[0] * SNAKE_CELL);
    uint16_t eye1_x;
    uint16_t eye1_y;
    uint16_t eye2_x;
    uint16_t eye2_y;

    switch (snake_dir) {
        case SNAKE_DIR_UP:
            eye1_x = base_x + 3u;
            eye1_y = base_y + 3u;
            eye2_x = base_x + 10u;
            eye2_y = base_y + 3u;
            break;
        case SNAKE_DIR_DOWN:
            eye1_x = base_x + 3u;
            eye1_y = base_y + 10u;
            eye2_x = base_x + 10u;
            eye2_y = base_y + 10u;
            break;
        case SNAKE_DIR_LEFT:
            eye1_x = base_x + 3u;
            eye1_y = base_y + 3u;
            eye2_x = base_x + 3u;
            eye2_y = base_y + 10u;
            break;
        default:
            eye1_x = base_x + 10u;
            eye1_y = base_y + 3u;
            eye2_x = base_x + 10u;
            eye2_y = base_y + 10u;
            break;
    }

    snake_draw_cell(snake_x[0], snake_y[0], SNAKE_COLOR_HEAD);
    gui_fill_rect(eye1_x, eye1_y, 2u, 2u, SNAKE_COLOR_EYE);
    gui_fill_rect(eye2_x, eye2_y, 2u, 2u, SNAKE_COLOR_EYE);
}

static void snake_draw_snake(void) {
    for (uint8_t i = 1; i < snake_length; ++i) {
        snake_draw_cell(snake_x[i], snake_y[i], SNAKE_COLOR_BODY);
    }
    snake_draw_head();
}

static void snake_redraw(void) {
    snake_draw_board();
    snake_draw_food();
    snake_draw_snake();
    snake_draw_hud();
}

static uint32_t snake_next_random(void) {
    snake_rng = snake_rng * 1103515245u + 12345u;
    return snake_rng;
}

static bool snake_cell_has_snake(
    uint8_t x,
    uint8_t y
) {
    for (uint8_t i = 0; i < snake_length; ++i) {
        if (snake_x[i] == x && snake_y[i] == y) {
            return true;
        }
    }
    return false;
}

static void snake_place_food(void) {
    for (uint32_t attempt = 0; attempt < 1024u; ++attempt) {
        uint32_t value = snake_next_random();
        uint8_t x = (uint8_t) (value % SNAKE_COLS);
        uint8_t y = (uint8_t) ((value / SNAKE_COLS) % SNAKE_ROWS);
        if (!snake_cell_has_snake(x, y)) {
            snake_food_x = x;
            snake_food_y = y;
            return;
        }
    }

    snake_food_x = 1;
    snake_food_y = 1;
}

static void snake_return_to_shell(void) {
    char shell_name[] = "shell";
    struct BlockBuffer directory;
    struct EXT2Inode root_node;
    get_node(&root_node, 1);
    read_blocks(directory.buf, root_node.i_block[0], 1);

    uint32_t shell_inode = 0;
    uint32_t offset = get_dir_first_child_offset(directory.buf);
    while (offset < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *entry =
            get_directory_entry(directory.buf, offset);
        if (entry->inode == 0 || entry->rec_len == 0 ||
            entry->rec_len > BLOCK_SIZE - offset) {
            break;
        }
        if (entry->name_len == 5 &&
            memcmp(get_entry_name(entry), shell_name, 5) == 0) {
            shell_inode = entry->inode;
            break;
        }
        offset += entry->rec_len;
    }
    if (shell_inode == 0) {
        exit_process();
    }

    struct EXT2Inode shell_node;
    get_node(&shell_node, shell_inode);

    struct EXT2DriverRequest exec_request = {
        .buf = NULL,
        .name = shell_name,
        .name_len = 5,
        .parent_inode = 1,
        .buffer_size = shell_node.i_size,
        .is_directory = false,
    };

    audio_stop();
    framebuffer_clear();
    exec_process(exec_request);
    exit_process();
}

static void snake_game_over(void) {
    audio_tone(SNAKE_SOUND_GAME_OVER_HZ, SNAKE_SOUND_GAME_OVER_MS);

    gui_fill_rect(176, 192, 288, 96, SNAKE_COLOR_BORDER);
    gui_fill_rect(180, 196, 280, 88, SNAKE_COLOR_BG);
    snake_write_text(25, 33, "GAME OVER", SNAKE_COLOR_TITLE);
    snake_write_text(27, 27, "PRESS ANY KEY TO EXIT", SNAKE_COLOR_TEXT);

    char key = 0;
    while (key == 0) {
        poll_keyboard_buffer(&key);
        if (key == 0) {
            yield();
        }
    }
    snake_return_to_shell();
}

static uint32_t snake_key_sound_hz(char key) {
    switch (key) {
        case KEYBOARD_KEY_UP:
        case 'w':
            return SNAKE_SOUND_UP_HZ;
        case KEYBOARD_KEY_RIGHT:
        case 'd':
            return SNAKE_SOUND_RIGHT_HZ;
        case KEYBOARD_KEY_DOWN:
        case 's':
            return SNAKE_SOUND_DOWN_HZ;
        case KEYBOARD_KEY_LEFT:
        case 'a':
            return SNAKE_SOUND_LEFT_HZ;
        default:
            return SNAKE_SOUND_KEY_HZ;
    }
}

static void snake_read_input(void) {
    char key = 0;
    poll_keyboard_buffer(&key);

    if (key == 0) {
        return;
    }
    audio_tone(snake_key_sound_hz(key), SNAKE_SOUND_KEY_MS);

    if (key == 'q' || key == '\x1B') {
        snake_game_over();
    }

    switch (key) {
        case KEYBOARD_KEY_UP:
        case 'w':
            if (snake_dir != SNAKE_DIR_DOWN) {
                snake_pending_dir = SNAKE_DIR_UP;
            }
            break;
        case KEYBOARD_KEY_RIGHT:
        case 'd':
            if (snake_dir != SNAKE_DIR_LEFT) {
                snake_pending_dir = SNAKE_DIR_RIGHT;
            }
            break;
        case KEYBOARD_KEY_DOWN:
        case 's':
            if (snake_dir != SNAKE_DIR_UP) {
                snake_pending_dir = SNAKE_DIR_DOWN;
            }
            break;
        case KEYBOARD_KEY_LEFT:
        case 'a':
            if (snake_dir != SNAKE_DIR_RIGHT) {
                snake_pending_dir = SNAKE_DIR_LEFT;
            }
            break;
        default:
            break;
    }
}

static bool snake_hits_self(
    uint8_t x,
    uint8_t y,
    bool growing
) {
    uint8_t start = growing ? 0 : (uint8_t) (snake_length - 1u);
    for (uint8_t i = start; i < snake_length; ++i) {
        if (snake_x[i] == x && snake_y[i] == y) {
            return true;
        }
    }
    return false;
}

static void snake_step(void) {
    snake_dir = snake_pending_dir;

    uint8_t old_head_x = snake_x[0];
    uint8_t old_head_y = snake_y[0];
    uint8_t head_x = old_head_x;
    uint8_t head_y = old_head_y;
    switch (snake_dir) {
        case SNAKE_DIR_UP:
            --head_y;
            break;
        case SNAKE_DIR_RIGHT:
            ++head_x;
            break;
        case SNAKE_DIR_DOWN:
            ++head_y;
            break;
        case SNAKE_DIR_LEFT:
            --head_x;
            break;
        default:
            break;
    }

    bool eating = head_x == snake_food_x && head_y == snake_food_y;
    if (head_x >= SNAKE_COLS || head_y >= SNAKE_ROWS ||
        snake_hits_self(head_x, head_y, eating)) {
        snake_game_over();
    }

    bool growing = eating && snake_length < SNAKE_MAX_LENGTH;
    uint8_t old_tail_x = snake_x[snake_length - 1u];
    uint8_t old_tail_y = snake_y[snake_length - 1u];

    for (uint8_t i = (uint8_t) (snake_length - 1u); i > 0; --i) {
        snake_x[i] = snake_x[i - 1u];
        snake_y[i] = snake_y[i - 1u];
    }
    snake_x[0] = head_x;
    snake_y[0] = head_y;

    if (growing) {
        snake_x[snake_length] = old_tail_x;
        snake_y[snake_length] = old_tail_y;
        ++snake_length;
    } else {
        snake_draw_cell(old_tail_x, old_tail_y, SNAKE_COLOR_FIELD);
    }

    // Redraw only the cells that changed this tick. Repainting the whole
    // board every frame made the screen visibly flash between fills.
    snake_draw_cell(old_head_x, old_head_y, SNAKE_COLOR_BODY);
    snake_draw_head();

    if (eating) {
        ++snake_score;
        audio_tone(SNAKE_SOUND_FOOD_HZ, SNAKE_SOUND_FOOD_MS);
        snake_place_food();
        snake_draw_food();
        snake_draw_hud();
    }
}

int main(void) {
    keyboard_state_activate();
    framebuffer_clear();

    snake_length = 4;
    uint8_t center_x = (uint8_t) (SNAKE_COLS / 2u);
    uint8_t center_y = (uint8_t) (SNAKE_ROWS / 2u);
    for (uint8_t i = 0; i < snake_length; ++i) {
        snake_x[i] = (uint8_t) (center_x - i);
        snake_y[i] = center_y;
    }

    snake_rng = pit_get_ticks() ^ 0xA5A5A5A5u;
    snake_place_food();
    snake_redraw();
    audio_tone(SNAKE_SOUND_START_HZ, SNAKE_SOUND_START_MS);

    uint32_t last_tick = pit_get_ticks();
    while (true) {
        snake_read_input();
        uint32_t now = pit_get_ticks();
        if (now - last_tick >= SNAKE_MOVE_INTERVAL_TICKS) {
            snake_step();
            last_tick = now;
        } else {
            yield();
        }
    }

    return 0;
}
