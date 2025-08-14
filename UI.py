import locale
locale.setlocale(locale.LC_ALL, '')  

import sys
import curses
import json
import queue
import threading
import re  
from typing import Dict, List, Tuple, Optional, NamedTuple

from client import TypingClient  

WORDS_LINE_GAP = 1
AFTER_SCOREBOARD_GAP = 1
USERNAME_MAX_LEN = 16


class PlayerState(NamedTuple):
    uuid: str
    name: str
    wpm: int = 0
    finished_rank: Optional[int] = None  
    progress: int = 0


class GameState:
    def __init__(self):
        self.players: Dict[str, PlayerState] = {}
        self.me_uuid: str = ""
        self.me_name: str = ""
        self.words: List[str] = []
        
        self.curr_idx: int = 0           
        self.line_start_idx: int = 0     
        self.current_typed: str = ""

        self.rank_order: List[str] = []
        
        self.last_message: str = ""
        self.session_active: bool = False
        self.session_ended: bool = False

    def me_progress(self) -> int:
        return self.curr_idx

    def current_target(self) -> str:
        if 0 <= self.curr_idx < len(self.words):
            return self.words[self.curr_idx]
        return ""


def fill_line(words: List[str], start_idx: int, width: int, padding: int = 1) -> Tuple[List[Tuple[int, str]], int]:
    line: List[Tuple[int, str]] = []
    if width <= 0:
        return line, start_idx

    idx = start_idx
    x = 0
    while idx < len(words):
        w = words[idx]
        need = len(w) if not line else len(w) + padding
        if x + need > max(0, width - 2):  
            break
        if line:
            x += padding
        line.append((idx, w))
        x += len(w)
        idx += 1
    return line, idx


def two_lines(words: List[str], line_start_idx: int, width: int, padding: int = 1):
    top, next_idx = fill_line(words, line_start_idx, width, padding)
    bottom, next2 = fill_line(words, next_idx, width, padding)
    return top, next_idx, bottom, next2


class CursesUI:
    GOLD = 1
    SILVER = 2
    BRONZE = 3
    GREEN = 4
    RED = 5
    DIM = 6
    TITLE = 7

    def __init__(self, stdscr, client: TypingClient, name: str):
        self.stdscr = stdscr
        self.client = client
        self.state = GameState()
        self.state.me_name = name
        self.state.me_uuid = client.uuid or ""

        self.event_q: "queue.Queue[dict]" = queue.Queue()
        self.ascii_borders = False  
        self._hard_exit = False
        self._hard_exit_message = ""

        original_handle = client._handle_message

        def hooked_handle(msg: dict):
            try:
                self.event_q.put_nowait(msg)
            except queue.Full:
                pass
            
        client._handle_message = hooked_handle
    
    def run(self):
        self.stdscr.nodelay(True)
        self.stdscr.timeout(50)

        curses.curs_set(0)
        self.stdscr.leaveok(True)

        curses.start_color()
        curses.use_default_colors()
        
        for i, fg in enumerate(
            [
                curses.COLOR_YELLOW,  
                curses.COLOR_WHITE,   
                curses.COLOR_MAGENTA, 
                curses.COLOR_GREEN,   
                curses.COLOR_RED,     
                curses.COLOR_CYAN,    
                curses.COLOR_CYAN,    
            ],
            start=1,
        ):
            try:
                curses.init_pair(i, fg, -1)
            except curses.error:
                pass

        while True:
            self._drain_events()
            self._render()

            if self._hard_exit:
                self.state.last_message = self._hard_exit_message or self.state.last_message
                self._render()
                break

            ch = self.stdscr.getch()
            if ch == -1:
                continue
            
            if ch in (ord('q'), 27):
                self.client.disconnect()
                break
            if ch == 4:  
                self.client.disconnect()
                self.state.last_message = "Disconnected"
                break
            if ch == 14:  
                self.client.request_new_lobby()
                self.state.last_message = "Requested new lobby (accepted once game has started)"
                continue

            if not self.state.words:
                continue

            if ch in (curses.KEY_BACKSPACE, 127, 8, 263):
                if self.state.current_typed:
                    self.state.current_typed = self.state.current_typed[:-1]
                continue

            if ch == ord(' '):  
                self._submit_word_and_maybe_advance_line()
                continue

            if 32 <= ch <= 126:
                self.state.current_typed += chr(ch)

        self.client.close()
        curses.curs_set(0)
    
    def _submit_word_and_maybe_advance_line(self):
        target = self.state.current_target()
        typed = self.state.current_typed
        if not target:
            return

        if typed == target:
            self.client.send_word(target)
            self.state.curr_idx += 1
            self.state.current_typed = ""

            rows, cols = self.stdscr.getmaxyx()
            my, mx = 2, 4
            inner_width = max(10, (cols - 2 * mx - 1) - 4)  
            top_line, next_idx_after_top, _, _ = two_lines(
                self.state.words, self.state.line_start_idx, inner_width
            )
            if top_line:
                last_idx_in_top = top_line[-1][0]
                if self.state.curr_idx > last_idx_in_top:
                    self.state.line_start_idx = next_idx_after_top
        else:
            self.state.current_typed = ""
    
    def _drain_events(self):
        try:
            while True:
                msg = self.event_q.get_nowait()
                self._apply_msg(msg)
        except queue.Empty:
            pass

    def _msg_uuid(self, msg: dict) -> Optional[str]:
        d = msg.get("data") or {}
        return d.get("uuid") or msg.get("uuid") or msg.get("player_uuid")

    def _apply_msg(self, msg: dict):
        t = msg.get("type")
        if t == "lobby":
            data = msg.get("data") or {}
            players = data.get("players") or []

            me_uuid = self.state.me_uuid
            me_name = self.state.me_name
            
            keep_me = {}
            if me_uuid:
                keep_me[me_uuid] = self.state.players.get(
                    me_uuid,
                    PlayerState(uuid=me_uuid, name=me_name, wpm=0, progress=0)
                )
            self.state.players = keep_me
            self.state.words = []
            self.state.curr_idx = 0
            self.state.line_start_idx = 0
            self.state.current_typed = ""
            self.state.rank_order = []

            for p in players:
                uid = p.get("uuid") or ""
                name = p.get("name") or ""
                if uid:
                    self.state.players[uid] = PlayerState(uuid=uid, name=name, wpm=0, progress=0)

            self.state.last_message = msg.get("message") or self.state.last_message
            return

        elif t == "info":
            message = msg.get("message") or ""

            if "change_lobby request accepted" in message:
                me_uuid = self.state.me_uuid
                me_name = self.state.me_name
                self.state = GameState()
                self.state.me_uuid = me_uuid
                self.state.me_name = me_name
                if me_uuid:
                    self.state.players[me_uuid] = PlayerState(uuid=me_uuid, name=me_name, wpm=0, progress=0)
                self.state.last_message = message  
                return

            uid = self._msg_uuid(msg)
            name = msg.get("player") or (msg.get("data") or {}).get("name") or ""
            if uid and "joined the lobby" in message:
                if uid not in self.state.players:
                    self.state.players[uid] = PlayerState(uuid=uid, name=name, wpm=0, progress=0)
            
            m = re.search(r"player\s+([0-9a-fA-F\-]{8,})\s+has\s+disconnected", message)
            if m:
                duid = m.group(1)
                if duid in self.state.players:
                    self.state.players.pop(duid, None)
                    if duid in self.state.rank_order:
                        self.state.rank_order = [x for x in self.state.rank_order if x != duid]
            return

        elif t == "countdown":
            val = (msg.get("data") or {}).get("value")
            self.state.last_message = f"Countdown: {val}"
            return

        elif t == "words":
            data = msg.get("data") or {}
            self.state.words = list(data.get("words") or [])
            self.state.curr_idx = 0
            self.state.line_start_idx = 0
            self.state.current_typed = ""
            for uid, ps in list(self.state.players.items()):
                self.state.players[uid] = PlayerState(
                    uuid=ps.uuid, name=ps.name, wpm=ps.wpm, finished_rank=None, progress=0
                )
            self.state.session_ended = False
            self.state.last_message = ""     
            return

        elif t == "wpm":
            data = msg.get("data") or {}
            uid = data.get("uuid")
            new_wpm = int(data.get("value") or 0)
            if not uid:
                return

            ps = self.state.players.get(uid)
            if not ps:
                
                ps = PlayerState(uuid=uid, name="", wpm=0, progress=0)

            
            new_progress = ps.progress
            if uid != self.state.me_uuid and self.state.words:
                new_progress = min(len(self.state.words), ps.progress + 1)

            self.state.players[uid] = PlayerState(
                uuid=uid,
                name=ps.name,
                wpm=new_wpm,
                finished_rank=ps.finished_rank,
                progress=new_progress,
            )
            return

        elif t == "completed":
            uid = self._msg_uuid(msg) or self.state.me_uuid
            if uid not in self.state.rank_order:
                self.state.rank_order.append(uid)
            rank = self.state.rank_order.index(uid) + 1

            ps = self.state.players.get(uid)
            if not ps:
                ps = PlayerState(uuid=uid, name=msg.get("player") or "", wpm=0, progress=0)

            final_prog = len(self.state.words)
            self.state.players[uid] = PlayerState(
                uuid=ps.uuid,
                name=ps.name or (msg.get("player") or ""),
                wpm=ps.wpm,
                finished_rank=rank,
                progress=(ps.progress if uid == self.state.me_uuid else final_prog),
            )
            self.state.last_message = msg.get("message") or self.state.last_message
            return

        elif t == "timeout_warning":
            data = msg.get("data") or {}
            remaining = data.get("remaining")
            self.state.last_message = f"Timeout in {remaining}s"
            return

        elif t in ("timeout", "inactive_timeout"):
            self.state.last_message = msg.get("message") or t
            self._hard_exit = True
            self._hard_exit_message = self.state.last_message
            return

        elif t in ("session_end", "bye"):
            self.state.session_ended = True
            self.state.last_message = msg.get("message") or t
            return

        else:
            if msg.get("message"):
                self.state.last_message = msg["message"]
    
    def _render(self):
        if curses.is_term_resized(*self.stdscr.getmaxyx()):
            curses.resize_term(0, 0)

        self.stdscr.erase()
        rows, cols = self.stdscr.getmaxyx()
        if rows < 12 or cols < 50:
            self._text(0, 0, "Enlarge the terminal (>= 50x12)")
            self.stdscr.refresh()
            return

        my, mx = 2, 4
        top, left = my, mx
        bottom, right = rows - my - 1, cols - mx - 1
        self._draw_box(self.stdscr, top, left, bottom, right)

        self._center_text(top, " typeL ", attr=curses.color_pair(self.TITLE) | curses.A_BOLD)
        
        inner_top, inner_left = top + 2, left + 2
        inner_bottom, inner_right = bottom - 2, right - 2
        ih = max(6, inner_bottom - inner_top + 1)
        iw = max(20, inner_right - inner_left + 1)
        win = self.stdscr.derwin(ih, iw, inner_top, inner_left)
        win.leaveok(False)
        
        y = 1
        x1, x2 = 1, iw - 2
        y = self._draw_scoreboard_in(win, y, x1, x2)
        
        y += AFTER_SCOREBOARD_GAP
        cursor_y, cursor_x, cursor_visible = self._draw_words_area_in(win, y, x1, x2)
        
        footer = "Ctrl+D: disconnect    Ctrl+N: change lobby    link to repo: https://github.com/SalvatoreBia/typeL"
        self._addn(win, ih - 2, 1, self.state.last_message or "", iw - 2, curses.A_DIM)
        self._addn(win, ih - 1, 1, footer, iw - 2, curses.color_pair(self.DIM))
        
        if cursor_visible:
            try:
                win.move(cursor_y, cursor_x)
                curses.curs_set(1)
            except curses.error:
                curses.curs_set(0)
        else:
            curses.curs_set(0)
            rows, cols = self.stdscr.getmaxyx()
            try:
                self.stdscr.move(rows - 1, cols - 1)
            except curses.error:
                pass

        self.stdscr.noutrefresh()
        win.noutrefresh()
        curses.doupdate()
    
    def _draw_scoreboard_in(self, win, y, x1, x2):
        players = list(self.state.players.values())
        players.sort(key=lambda p: (p.finished_rank is None, p.finished_rank or 0, -p.wpm, p.name))

        total_cols  = max(0, x2 - x1 + 1)
        max_lines   = max(1, min(5, len(players)))
        total_words = len(self.state.words)  

        for i, p in enumerate(players[:max_lines]):
            
            rank_attr = curses.A_NORMAL
            if p.finished_rank == 1:
                rank_attr = curses.color_pair(self.GOLD) | curses.A_BOLD

            name_txt = p.name or p.uuid[:8]
            self._addn(win, y + i, x1, name_txt, total_cols, rank_attr)

            if total_words == 0:
                continue
            
            prog_words = self.state.me_progress() if p.uuid == self.state.me_uuid else p.progress
            prog_words = max(0, min(prog_words, total_words))
            
            bar_start_x = x1 + len(name_txt) + 1
            cursor_x = bar_start_x
            
            bar_full = "*" * prog_words + "-" * (total_words - prog_words)
            
            space_left = max(0, x2 - cursor_x + 1)
            visible_bar = bar_full[:space_left]
            if visible_bar:
                self._addn(win, y + i, cursor_x, visible_bar, len(visible_bar))
                cursor_x += len(visible_bar)
            
            if cursor_x <= x2:
                wpm_txt = f"{p.wpm} WPM"
                if cursor_x < x2:
                    self._addn(win, y + i, cursor_x, " ", 1)
                    cursor_x += 1
                space_left = max(0, x2 - cursor_x + 1)
                if space_left > 0:
                    self._addn(win, y + i, cursor_x, wpm_txt, space_left)

        return y + max_lines

    def _draw_words_area_in(self, win, top, x1, x2):
        ih, iw = win.getmaxyx()
        width = max(0, x2 - x1 + 1)

        top_line, next_idx_after_top, bottom_line, _ = two_lines(
            self.state.words, self.state.line_start_idx, width
        )

        y = top
        x = x1

        cursor_y, cursor_x, cursor_visible = y, x, False

        for idx, w in top_line:
            if idx < self.state.curr_idx:
                self._addn(win, y, x, w, width - (x - x1), curses.color_pair(self.GREEN) | curses.A_BOLD)
                x += len(w) + 1
                continue

            if idx == self.state.curr_idx:
                typed = self.state.current_typed

                caret_x = x + min(len(typed), len(w))
                cursor_y, cursor_x = y, caret_x
                cursor_visible = True

                common = 0
                for a, b in zip(typed, w):
                    if a == b:
                        common += 1
                    else:
                        break
                if common:
                    self._addn(win, y, x, w[:common], width - (x - x1),
                               curses.color_pair(self.GREEN) | curses.A_BOLD)
                    x += common

                if len(typed) > common:
                    wrong = typed[common:len(typed)]
                    self._addn(win, y, x, wrong, width - (x - x1),
                               curses.color_pair(self.RED) | curses.A_BOLD)
                    x += len(wrong)

                rest = w[len(typed):]
                if rest:
                    self._addn(win, y, x, rest, width - (x - x1))
                    x += len(rest)

                self._addn(win, y, x, " ", 1)
                x += 1
                continue

            self._addn(win, y, x, w, width - (x - x1))
            x += len(w) + 1

        y2 = top + WORDS_LINE_GAP
        x = x1
        for _, w in bottom_line:
            self._addn(win, y2, x, w, width - (x - x1), curses.A_DIM)
            x += len(w) + 1

        return cursor_y, cursor_x, cursor_visible


    
    def _draw_box(self, win, top, left, bottom, right):
        try:
            if self.ascii_borders:
                for x in range(left, right + 1):
                    win.addch(top, x, ord('-'))
                    win.addch(bottom, x, ord('-'))
                for y in range(top, bottom + 1):
                    win.addch(y, left, ord('|'))
                    win.addch(y, right, ord('|'))
                win.addch(top, left, ord('+'));    win.addch(top, right, ord('+'))
                win.addch(bottom, left, ord('+')); win.addch(bottom, right, ord('+'))
            else:
                win.hline(top, left, curses.ACS_HLINE, right - left)
                win.hline(bottom, left, curses.ACS_HLINE, right - left)
                win.vline(top, left, curses.ACS_VLINE, bottom - top)
                win.vline(top, right, curses.ACS_VLINE, bottom - top)
                win.addch(top, left, curses.ACS_ULCORNER)
                win.addch(top, right, curses.ACS_URCORNER)
                win.addch(bottom, left, curses.ACS_LLCORNER)
                win.addch(bottom, right, curses.ACS_LRCORNER)
        except curses.error:
            pass

    def _center_text(self, y, text, attr=0):
        _, cols = self.stdscr.getmaxyx()
        x = max(0, (cols - len(text)) // 2)
        self._text(y, x, text, attr)

    def _text(self, y, x, s, attr=0):
        try:
            self.stdscr.addstr(y, x, s, attr)
        except curses.error:
            pass

    def _addn(self, win, y, x, s, n, attr=0):
        if n <= 0 or y < 0:
            return
        try:
            win.addnstr(y, x, s, n, attr)
        except curses.error:
            pass


def start_ui(host="127.0.0.1", port=9000, name="player"):
    client = TypingClient(host, port)
    client.connect()
    import uuid as uuidlib
    client.handshake(str(uuidlib.uuid4()), name)

    def _main(stdscr):
        ui = CursesUI(stdscr, client, name)
        
        ui.state.players[client.uuid] = PlayerState(uuid=client.uuid, name=name, wpm=0)
        ui.run()

    curses.wrapper(_main)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"ERROR -> command usage is <python|python3> {sys.argv[0]} <username>")
        sys.exit()

    if len(sys.argv[1]) > USERNAME_MAX_LEN:
        print(f"ERROR -> the username length should be between 1 and 16 caracters")
        sys.exit()

    start_ui(name=sys.argv[1])
