"""Check the current prototype's request loop without modifying any records."""

import socket
import sys


def main():
    request = b"add 0\n"
    try:
        with socket.create_connection(("127.0.0.1", 7777), timeout=2) as connection:
            connection.settimeout(2)
            connection.sendall(request)
            response = b""
            while len(response) < len(request):
                chunk = connection.recv(len(request) - len(response))
                if not chunk:
                    break
                response += chunk
            if response != request:
                raise ValueError(f"Unexpected health response: {response!r}")
    except (OSError, ValueError) as error:
        print(f"Littlebear health check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
