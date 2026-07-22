# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/env python3

import asyncio
import websockets
import ssl
import json
import argparse
import time
import os

from datetime import datetime, timedelta

DEFAULT_CERT_PATH = "../src/webserver/certs/ca_cert.pem"

def parse_args():
    parser = argparse.ArgumentParser(description="WebSocket client script.")
    parser.add_argument("ressource", type=str, help="API ressource, e.g. 'ws_echo' (in case of myserver:80/ws_echo)")
    parser.add_argument("data", type=str, help="JSON string to send over WebSocket")
    parser.add_argument("--wss", action="store_true", help="Use wss:// instead of ws://")
    parser.add_argument("--host", type=str, default="bsh-benchi", help="WebSocket server host")
    parser.add_argument("--cert", type=str, default=DEFAULT_CERT_PATH, help="Path to CA certificate (for wss)")
    parser.add_argument("--loop", type=int, default=1, help="Number of times to send the message")
    parser.add_argument("--ctf", type=str, help="Path to ctf output file")
    return parser.parse_args()

ctf_entries = []
class CTFEntry:
    def __init__(self, msg):
        self.timestamp = datetime.now()
        self.msg = msg
        if len(ctf_entries):
            self.delta = self.timestamp - ctf_entries[-1].timestamp
        else:
            self.delta = timedelta(0)

    def get_str(self):
        timestamp_str = self.timestamp.strftime("%H:%M:%S.%f") + "000"
        delta_str = f"{self.delta.total_seconds():.6f}"
        line = f"[{timestamp_str}] (+{delta_str}) {self.msg}"
        return line

def append_ctf_line(ctf_file, msg):
    if ctf_file:
        ctf_entry = CTFEntry(msg)
        ctf_entries.append(ctf_entry)
        ctf_file.write(ctf_entry.get_str()+"\n")

async def send_messages(ws, message, loop_count, ctf):
    print("Sending websocket message '%s' %s time(s) ..." % (message, loop_count))
    ctf_file = open(ctf, "a") if ctf else None
    start_time = time.time()

    for index in range(loop_count):
        append_ctf_line(ctf_file, f"named_event: {{ name = \"send_message\", arg0 = {index+1}, arg1 = {loop_count} }}")
        await ws.send(message)
        response = await ws.recv()
        append_ctf_line(ctf_file, f"named_event: {{ name = \"reply_received\", arg0 = {index+1}, arg1 = {loop_count} }}")

    total_time = time.time() - start_time
    append_ctf_line(ctf_file, f"named_event: {{ name = \"total: {total_time:.2f}s\", arg0 = 0, arg1 = 0 }}")
    print(f"Total time: {total_time:.2f} seconds")

async def main():
    args = parse_args()
    protocol = "wss" if args.wss else "ws"
    url = f"{protocol}://{args.host}/{args.ressource}"
    ssl_context = None

    if args.ctf:
        os.makedirs(os.path.realpath(os.path.dirname(args.ctf)), exist_ok=True)

    if args.wss:
        ssl_context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
        ssl_context.load_verify_locations(args.cert)

    try:
        message = json.loads(args.data)  # Validate JSON input
        async with websockets.connect(url, ssl=ssl_context, close_timeout=1) as ws:
            await send_messages(ws, json.dumps(message, separators=(',', ':')), args.loop, args.ctf)

    except json.JSONDecodeError:
        print("Error: Data must be a valid JSON string.")
        exit(1)
    except Exception as e:
        print(f"Connection error: {e}")
        exit(1)

    if args.ctf:
        print(f"Info: CTF log see {args.ctf}")

if __name__ == "__main__":
    asyncio.run(main())

