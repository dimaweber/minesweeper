#include "ui.hxx"

#include <curses.h>

#include <string>

#include "board.hxx"

namespace {

  constexpr int COLOR_PAIR_HIDDEN    = 1;
  constexpr int COLOR_PAIR_FLAGGED   = 2;
  constexpr int COLOR_PAIR_BOOM      = 3;
  constexpr int COLOR_PAIR_REVEALED  = 4;
  constexpr int COLOR_PAIR_BORDER    = 5;
  constexpr int COLOR_PAIR_SELECTED  = 6;

  char symbol_for (const cell_t& cell) {
    switch ( cell.state ) {
      case cell_state_t::hidden:   return ' ';
      case cell_state_t::flagged:  return 'F';
      case cell_state_t::boom:     return '*';
      case cell_state_t::revealed: return cell.count == 0 ? ' ' : static_cast<char>('0' + cell.count);
    }
    return '?';
  }

  int color_for (const cell_t& cell) {
    switch ( cell.state ) {
      case cell_state_t::hidden:   return COLOR_PAIR_HIDDEN;
      case cell_state_t::flagged:  return COLOR_PAIR_FLAGGED;
      case cell_state_t::boom:     return COLOR_PAIR_BOOM;
      case cell_state_t::revealed: return COLOR_PAIR_REVEALED;
    }
    return COLOR_PAIR_HIDDEN;
  }

  // Draws the 3x3 single-line box for a cell, or a double-line box (using
  // '#'/'=' since the linked curses build has no wide-character support) when
  // the cell is the one currently selected by the cursor.
  void draw_cell_box (int base_row, int base_col, bool selected) {
    const int pair_id = selected ? COLOR_PAIR_SELECTED : COLOR_PAIR_BORDER;
    attron(COLOR_PAIR(pair_id));
    if ( selected )
      attron(A_BOLD);

    if ( selected ) {
      mvaddch(base_row, base_col, '#');
      mvaddch(base_row, base_col + 1, '=');
      mvaddch(base_row, base_col + 2, '#');
      mvaddch(base_row + 1, base_col, '#');
      mvaddch(base_row + 1, base_col + 2, '#');
      mvaddch(base_row + 2, base_col, '#');
      mvaddch(base_row + 2, base_col + 1, '=');
      mvaddch(base_row + 2, base_col + 2, '#');
    } else {
      mvaddch(base_row, base_col, ACS_ULCORNER);
      mvaddch(base_row, base_col + 1, ACS_HLINE);
      mvaddch(base_row, base_col + 2, ACS_URCORNER);
      mvaddch(base_row + 1, base_col, ACS_VLINE);
      mvaddch(base_row + 1, base_col + 2, ACS_VLINE);
      mvaddch(base_row + 2, base_col, ACS_LLCORNER);
      mvaddch(base_row + 2, base_col + 1, ACS_HLINE);
      mvaddch(base_row + 2, base_col + 2, ACS_LRCORNER);
    }

    if ( selected )
      attroff(A_BOLD);
    attroff(COLOR_PAIR(pair_id));
  }

  void draw (const board_t& board, int cursor_x, int cursor_y, int bombs_left, int bombs_total, const std::string& message) {
    erase( );

    mvprintw(0, 0, "Minesweeper -- bombs left: %d/%d", bombs_left, bombs_total);
    mvprintw(1, 0, "arrows: move  enter: reveal  space: flag  q: quit");
    if ( !message.empty( ) ) {
      mvprintw(2, 0, "%s", message.c_str( ));
    }

    const int top    = 4;
    const int width  = static_cast<int>(board.width( ));
    const int height = static_cast<int>(board.height( ));

    for ( int by = 0; by < height; ++by ) {
      for ( int bx = 0; bx < width; ++bx ) {
        const bool selected = bx == cursor_x - 1 && by == cursor_y - 1;
        const int  base_row = top + by * 3;
        const int  base_col = bx * 3;

        draw_cell_box(base_row, base_col, selected);

        const cell_t& cell = board.at(bx + 1, by + 1);
        attron(COLOR_PAIR(color_for(cell)));
        mvaddch(base_row + 1, base_col + 1, symbol_for(cell));
        attroff(COLOR_PAIR(color_for(cell)));
      }
    }

    refresh( );
  }

}// namespace

void run_game (client_api_t& api, client_id_t id, std::size_t width, std::size_t height, int bombs_total) {
  initscr( );
  cbreak( );
  noecho( );
  keypad(stdscr, TRUE);
  curs_set(0);

  if ( has_colors( ) ) {
    start_color( );
    use_default_colors( );
    init_pair(COLOR_PAIR_HIDDEN, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_PAIR_FLAGGED, COLOR_YELLOW, COLOR_BLUE);
    init_pair(COLOR_PAIR_BOOM, COLOR_WHITE, COLOR_RED);
    init_pair(COLOR_PAIR_REVEALED, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_PAIR_BORDER, COLOR_WHITE, -1);
    init_pair(COLOR_PAIR_SELECTED, COLOR_GREEN, -1);
  }

  board_t board(width, height);
  int      cursor_x   = 1;
  int      cursor_y   = 1;
  int      bombs_left = bombs_total;
  bool     game_over  = false;
  std::string message;

  const bombs_result_t bombs = api.field_bombs(id);
  if ( bombs.ok ) {
    bombs_left  = bombs.left;
    bombs_total = bombs.total;
  }

  draw(board, cursor_x, cursor_y, bombs_left, bombs_total, message);

  while ( !game_over ) {
    const int key = getch( );
    switch ( key ) {
      case KEY_UP:
        if ( cursor_y > 1 )
          --cursor_y;
        break;
      case KEY_DOWN:
        if ( cursor_y < static_cast<int>(height) )
          ++cursor_y;
        break;
      case KEY_LEFT:
        if ( cursor_x > 1 )
          --cursor_x;
        break;
      case KEY_RIGHT:
        if ( cursor_x < static_cast<int>(width) )
          ++cursor_x;
        break;
      case '\n':
      case '\r':
      case KEY_ENTER:
        {
          message.clear( );
          const reveal_result_t result = api.action_reveal(id, cursor_x, cursor_y);
          if ( !result.ok ) {
            message = "error: " + result.error;
          } else if ( result.boom ) {
            board.at(cursor_x, cursor_y).state = cell_state_t::boom;
            draw(board, cursor_x, cursor_y, bombs_left, bombs_total, "Boom! Game over. Press any key to exit.");
            getch( );
            game_over = true;
          } else {
            board.at(cursor_x, cursor_y).state = cell_state_t::revealed;
            board.at(cursor_x, cursor_y).count = result.count;
          }
          break;
        }
      case ' ':
        {
          message.clear( );
          const flag_result_t result = api.action_flag(id, cursor_x, cursor_y);
          if ( !result.ok ) {
            message = "error: " + result.error;
          } else {
            board.at(cursor_x, cursor_y).state = result.flagged ? cell_state_t::flagged : cell_state_t::hidden;
            const bombs_result_t refreshed = api.field_bombs(id);
            if ( refreshed.ok ) {
              bombs_left  = refreshed.left;
              bombs_total = refreshed.total;
            }
          }
          break;
        }
      case 'q':
      case 'Q':
      case 27:
        game_over = true;
        break;
      default:
        break;
    }

    if ( !game_over ) {
      draw(board, cursor_x, cursor_y, bombs_left, bombs_total, message);
    }
  }

  endwin( );
}
