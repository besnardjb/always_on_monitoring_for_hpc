#!/bin/env python3

import os
import re
import sys
import argparse
from ctypes import *
from enum import Enum
import socket
import json
import yaml
import signal
import time
from rich.console import Console
from rich.markdown import Markdown


# Define the client C API

METRIC_STRING_SIZE=300

class metric_type(Enum):
    TAU_METRIC_NULL = 0
    TAU_METRIC_COUNTER = 1
    TAU_METRIC_GAUGE = 2

class tau_metric_descriptor(Structure):
    _fields_ = [("name", c_char*METRIC_STRING_SIZE),
                ("doc", c_char*METRIC_STRING_SIZE),
                ("type", c_int) ]

class tau_metric_event_t(Structure):
    _fields_ = [("name", c_char*METRIC_STRING_SIZE), ("value", c_double), ("update_ts", c_double)]

class metric_msg_type(Enum):
    # Send data
    TAU_METRIC_MSG_DESC=0
    TAU_METRIC_MSG_VAL=1
    # Receive data
    TAU_METRIC_MSG_LIST_ALL=2
    TAU_METRIC_MSG_GET_ALL=3
    TAU_METRIC_MSG_GET_ONE=4
    # Count
    TAU_METRIC_MSG_COUNT=5

class msg_payload(Union):
    _fields_ = ("desc", tau_metric_descriptor), ("event", tau_metric_event_t)

class metric_msg(Structure):
    _fields_ = [("type", c_int), ("payload", msg_payload), ("canary", c_char) ]

# Done with the C API

def list_shell_escape(name_list):
    # Fight with shell escape SADNESS
    knre = re.compile(".*{.*=([^\s]+)}")

    for k in range(0, len(name_list)):
        print(name_list[k])
        m = knre.match(name_list[k])
        if m:
            match = m[1]
            if "\"" not in match:
                name_list[k] = name_list[k].replace(match, "\"{}\"".format(match))

class ProxyClient():

    def __init__(self, server_socket):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(server_socket)

    def _do_count_request(self, operation):
        m = metric_msg(canary=0x7, type=operation.value)
        self.sock.sendall(m)
        c_number_of_entries = c_int()
        self.sock.recv_into(c_number_of_entries)
        return c_number_of_entries.value

    def list_metrics(self):
        number_of_entries = self._do_count_request(metric_msg_type.TAU_METRIC_MSG_LIST_ALL)

        metric_desc = tau_metric_descriptor()

        ret = []

        for _ in range(0, number_of_entries):
            self.sock.recv_into(metric_desc)
            ret.append({"name": metric_desc.name.decode('utf-8'),
                        "doc": metric_desc.doc.decode('utf-8'),
                        "type": metric_type(metric_desc.type).name})

        return ret

    def _parse_metric_event(self, metric_event):
        return {"name": metric_event.name.decode('utf-8'),
                        "ts" : metric_event.update_ts ,
                        "value": metric_event.value}


    def get_all(self):
        number_of_entries = self._do_count_request(metric_msg_type.TAU_METRIC_MSG_GET_ALL)

        metric_event = tau_metric_event_t()

        ret = []

        for _ in range(0, number_of_entries):
            self.sock.recv_into(metric_event)
            ret.append(self._parse_metric_event(metric_event))

        return ret

    def get_one(self, name=""):
        d = tau_metric_descriptor(name=bytes(name, encoding='utf-8'))
        p = msg_payload(desc=d)
        m = metric_msg(canary=0x7,
                       type=metric_msg_type.TAU_METRIC_MSG_GET_ONE.value, payload=p)
        self.sock.sendall(m)

        metric_event = tau_metric_event_t()
        self.sock.recv_into(metric_event)
        return self._parse_metric_event(metric_event)

    def get_list(self, name_list):
        ret = [self.get_one(x) for x in name_list]
        return [ x for x in ret if x["name"]]


    def track(self, metric_list=None, freq=0.1, out="./log.js"):

        period = 1.0 / freq

        if not out:
            out = "./log.js"

        if metric_list:
            metric_list = metric_list.split(",")
            list_shell_escape(metric_list)
            def cb():
                return self.get_list(metric_list)
        else:
            def cb():
                return [ x for x in self.get_all() if x["value"] ]

        last_ts = {}
        to_pop = []


        prev_elem = 0

        with open(out, "w") as f:

            f.write("[")

            print("Logging in {} interrupt with CTRL + C when done... ".format(out), end="", flush=True)

            # Track loop
            signal.pthread_sigmask(signal.SIG_BLOCK, set(signal.Signals))

            while True:
                time.sleep(period)

                data = cb()
                new_data = []
                for i in range(0, len(data)):
                    e = data[i]
                    if e["name"] in last_ts:
                        if last_ts[e["name"]] != e["ts"]:
                            new_data.append(e)
                    else:
                        new_data.append(e)

                if new_data:
                    if prev_elem:
                        f.write(",")
                    data = json.dumps(new_data)

                    f.write(data)

                    prev_elem = 1

                if signal.sigpending():
                    break

                for e in new_data:
                    last_ts[e["name"]] = e["ts"]

            f.write("]\n")
            f.flush()
            f.close()

        signal.pthread_sigmask(signal.SIG_UNBLOCK, set(signal.Signals))

        return 0



def _show_md(data):
    console = Console()
    md = Markdown(data)
    console.print(md)

def _show_data(data, fmt="json"):
    if fmt == "json":
        print(json.dumps(data, indent=4))
    elif fmt == "yaml":
        print(yaml.dump(data))
    elif fmt == "txt":
        if not data:
            return
        if isinstance(data, list):
            keys = data[0].keys()
            print("# " + " ".join(keys))
            for d in data:
                for k in keys:
                    if isinstance(d[k], str):
                        if " " in d[k]:
                            print("\"{}\" ".format(d[k]), end="")
                            continue
                    print("{} ".format(d[k]), end="")
                print("")
        elif isinstance(data, dict):
            keys = data.keys()
            print("# " + " ".join(keys))
            for k in keys:
                if isinstance(data[k], str):
                    if " " in data[k]:
                        print("\"{}\" ".format(data[k]), end="")
                        continue
                print("{} ".format(data[k]), end="")
            print("")

def _do_list(client, fmt="md"):
    metrics = client.list_metrics()

    if fmt == "md":
        md = "# List of tau_metric_exporter Metrics\n\n" + "\n".join(["* **{}** ({}) : {}".format(x["name"], x["type"], x["doc"]) for x in metrics])
        _show_md(md)
    else:
        _show_data(metrics, fmt=fmt)

    return 0

def __show_value_list(values, fmt="md"):
    if fmt == "md":
        md = "# List of tau_metric_exporter Values\n\n" + "\n".join(["* {} = **{}** @ {}".format(x["name"], x["value"], x["ts"]) for x in values])
        _show_md(md)
    else:
        _show_data(values, fmt=fmt)

    return 0

def _do_values(client, fmt="md"):
    values = [ x for x in client.get_all() if x["value"] > 0 ]
    return __show_value_list(values, fmt)

def _do_get_list(client, argument, fmt="md"):
    name_list = argument.split(",")

    list_shell_escape(name_list)

    values = client.get_list(name_list)
    return __show_value_list(values, fmt)




def run():

    #
    # Argument parsing
    #

    parser = argparse.ArgumentParser(description='tau_metric_proxy client')

    parser.add_argument('-o', '--output', type=str, help="File to store the result to")
    parser.add_argument('-u', '--unix', type=str, help="Path to the tau_metric_proxy UNIX socket")

    parser.add_argument('-F', '--freq', type=float, default=10, help="Sampling frequency (for data retrieval)")


    parser.add_argument('-f', '--format', choices=['md', 'json', 'yaml', 'txt'], help="Path to the tau_metric_proxy UNIX socket")

    parser.add_argument('-l', "--list",  action='store_true', help="List metrics in the tau_metric_proxy")
    parser.add_argument('-v', "--values",  action='store_true', help="List all values in the tau_metric_proxy")

    parser.add_argument('-g', '--get', type=str, help="Get values by name (comma separated)")

    parser.add_argument('-t', "--track",  action='store_true', help="Track all non-null counters over time")
    parser.add_argument('-T', '--track-list', type=str, help="Track a given list of values over time")


    args = parser.parse_args(sys.argv[1:])

    if not args.format:
        args.format = "md"

    server_sock = "/tmp/tau_metric_proxy.{}.unix".format(os.getuid())

    if args.unix:
        server_sock = args.unix

    client = ProxyClient(server_sock)

    if args.list:
        return _do_list(client, fmt=args.format)

    if args.values:
        return _do_values(client, fmt=args.format)

    if args.get:
        return _do_get_list(client, args.get, args.format)

    if args.track:
        if not args.output:
            print("Default output goes to log.js consider altering with -o")
        return client.track(None, args.freq, args.output)

    if args.track_list:
        if not args.output:
            print("Default output goes to log.js consider altering with -o")
        return client.track(args.track_list, args.freq, args.output)

    parser.print_help()