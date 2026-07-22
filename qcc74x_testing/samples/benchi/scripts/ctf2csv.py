# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/env python3

import argparse
import os
import re

from datetime import datetime

script_dir = os.path.realpath(os.path.dirname(__file__))

class CTFEntry():
    def __init__(self):
        self.timestamp = None
        self.timestamp_seconds = None
        self.timestamp_nanoseconds = None
        self.event_name = None
        self.arg0 = None
        self.arg1 = None
        self.delta = None

    def set_timestamp_from_str(self, timestamp):
        timestamp_pattern = re.match(r"(\d{2}:\d{2}:\d{2})\.(\d{9})", timestamp)
        precomma, nanoseconds  = timestamp_pattern.groups()
        precomma_dt = datetime.strptime(precomma, "%H:%M:%S")
        total_seconds = precomma_dt.hour * 3600 + precomma_dt.minute * 60 + precomma_dt.second
        self.timestamp = timestamp
        self.timestamp_seconds = total_seconds
        self.timestamp_nanoseconds = int(nanoseconds)

    def set_attributes_from_str(self, input_str):
        match = re.match(r'\[(.*?)\] \(\+([0-9.?]+)\) \d+ named_event: \{ name = "(.*?)", arg0 = (\d+), arg1 = (\d+) \}', input_str)
        if match:
            timestamp, delta, event_name, arg0, arg1 = match.groups()
            if event_name == "ctf_keepalive":
                return
            self.set_timestamp_from_str(timestamp)
            self.event_name = event_name
            self.arg0 = arg0
            self.arg1 = arg1

    def set_delta(self, base):
        if not base:
            self.delta = 0
            return
        delta_seconds = self.timestamp_seconds - base.timestamp_seconds
        delta_nanoseconds =  self.timestamp_nanoseconds - base.timestamp_nanoseconds
        self.delta = (1000000000 * delta_seconds + delta_nanoseconds) / 1000000000

    def get_str(self):
        return f"{self.timestamp_seconds}.{self.timestamp_nanoseconds},{self.delta:.9f},{self.event_name},{self.arg0},{self.arg1}"


def ctf2csv(input_file, output_file):
    ctf_entries = []
    with open(input_file, 'r') as infile:
        for line in infile:
            entry = CTFEntry()
            entry.set_attributes_from_str(line)
            if entry.event_name:
                ctf_entries.append(entry)

    # recalculate the delta
    last_entry = None
    for ctf_entry in ctf_entries:
        ctf_entry.set_delta(last_entry)
        last_entry = ctf_entry

    # create
    csv_header = "timestamp,delta,eventName,traceId,seqId"
    with open(output_file, 'w') as outfile:
        outfile.write(csv_header + '\n')
        for ctf_entry in ctf_entries:
            outfile.write(ctf_entry.get_str() + '\n')

    print("Successfully created: %s" % os.path.realpath(output_file) )

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Process an input file.")
    parser.add_argument("-i", type=str, help="Path to the input file")
    parser.add_argument("-o", type=str, help="Path to the output file")
    args = parser.parse_args()

    ctf2csv(args.i, args.o)
