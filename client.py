import socket
import threading
import json
import uuid as uuidlib
from typing import List, Optional


class TypingClient:

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.sock: Optional[socket.socket] = None
        self.rfile = None
        self.reader_thread: Optional[threading.Thread] = None
        self.stop_event = threading.Event()
        self.words: List[str] = []
        self.name: str = ''
        self.uuid = ''


    # SOCKET COMMUNICATION
    def connect(self, timeout=0.5):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((self.host, self.port))
        s.settimeout(None)
        self.sock = s

        self.rfile = s.makefile('r', encoding='utf-8', newline='\n')
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()

    
    def send_json(self, obj: dict):
        if not self.sock:
            return
        
        data = json.dumps(obj, separators=(',', ':')) + '\n'
        try:
            self.sock.sendall(data.encode('utf-8'))
        except BrokenPipeError:
            pass


    def close(self):
        self.stop_event.set()
        try:
            if self.rfile:
                self.rfile.close()
        except Exception:
            pass

        try:
            if self.sock:
                self.sock.close()
        except Exception:
            pass


    def handshake(self, uuid: str, name: str):
        self.uuid = uuid
        self.name = name
        self.send_json({
            'uuid': uuid,
            'name': name
        })


    def send_word(self, word: str):
        self.send_json({
            'word': word
        })


    def disconnect(self):
        self.send_json({
            'type': 'disconnect'
        })


    # MESSAGE HANDLING
    def _reader_loop(self):
        while not self.stop_event.is_set():
            line = self.rfile.readline()
            if not line:
                break

            line = line.strip()
            if not line:
                continue

            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue

            self._handle_message(msg)


    def _handle_message(self, msg: dict):
        mtype = msg.get('type')
        player = msg.get('player')
        data = msg.get('data') or {}
        message = msg.get('message')

        if mtype == "error":
            print(f"[ERROR] {message}")
        elif mtype == "lobby":
            print(f"[LOBBY] {message} (player={player})")
        elif mtype == "countdown":
            print(f"[COUNTDOWN] {data.get('value')}")
        elif mtype == "words":
            self.words = data.get("words", [])
            print(f"[WORDS] received {len(self.words)} words")

            # THREAD FOR AUTOPLAY
            # threading.Thread(target=self._demo_autoplay, daemon=True).start()
        elif mtype == "wpm":
            print(f"[WPM] {player}: {data.get('value')}")
        elif mtype == "completed":
            print(f"[COMPLETED] {message}")
        elif mtype == "timeout_warning":
            print(f"[TIMEOUT WARNING] remaining={data.get('remaining')}")
        elif mtype == "timeout":
            print(f"[TIMEOUT] {message}")
            self.close()
        elif mtype == "session_end":
            print(f"[SESSION END] {message}")
            self.close()
        elif mtype == "inactive_timeout":
            print(f"[KICKED] {message}")
            self.close()
        elif mtype == "bye":
            print(f"[BYE] {message}")
            self.close()
        else:
            print(f"[UNKNOWN] {msg}")

    
    def _demo_autoplay(self):
        import time
        for w in self.words:
            self.send_word(w)
            time.sleep(1)


def main():
    host = '127.0.0.1'
    port = 9000

    client = TypingClient(host, port)
    client.connect()

    uuid = str(uuidlib.uuid4())
    name = 'sborra'
    client.handshake(uuid, name)

    try:
        while client.reader_thread and client.reader_thread.is_alive():
            client.reader_thread.join(timeout=0.25)
    except KeyboardInterrupt:
        client.disconnect()
        client.close()

    
if __name__ == '__main__':
    main()
