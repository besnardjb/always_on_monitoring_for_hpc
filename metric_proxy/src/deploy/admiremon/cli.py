#!/bin/env python3

import os
import sys
import signal
import json
import time
import shutil
import argparse
import tempfile
import subprocess
import socket
from shutil import which
from threading import Thread, Lock
import random
from rich import print


SCRIPT = sys.argv[0]


class ConfigserverLoop (Thread):
    def __init__(self, socket, server):
        Thread.__init__(self)
        self.daemon = True
        self.server = server
        self.client_list = server.client_list
        self.socket = socket
        self.first = True

    def _append_state(self, state):
        if "host" not in state:
            return None

        force = False

        if "force" in state:
            force = state["force"]
            # Do not keep the key
            del (state["force"])

        if (state["host"] not in self.client_list) or force:
            if state["host"] in self.client_list:
                state["new"] = False
            else:
                state["new"] = True

            if self.first:
                state["first"] = True
                self.first = False
            else:
                state["first"] = False

            self.client_list[state["host"]] = state
            return state
        else:
            self.client_list[state["host"]]["new"] = False
            return self.client_list[state["host"]]


    def _read_state(self, conn):
        data = conn.recv(8192)
        if not data:
            return None

        # Parse incoming state
        try:
            jd = json.loads(data)
        except:
            return None

        # Send response state
        return self._append_state(jd)

    def run(self):
        while True:
            try:
                conn, _ = self.socket.accept()

                # Read initial state
                ret = self._read_state(conn)
                conn.sendall(bytes(json.dumps(ret), encoding="utf-8"))

                # Read updated state
                ret = self._read_state(conn)
                conn.sendall(bytes(json.dumps(ret), encoding="utf-8"))

                print(ret)

                self.server.increment_client_count()

                conn.close()
            except Exception as e:
                print(e)



class ADMIRE_config_server():

    def tau_exporter_list(self):
        return [ x["tau_exporter"] for x in self.client_list.values()]

    def address(self):
        return "{}:{}".format(self.host, self.port)

    def __init__(self):
        self.listensock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listensock.bind(("0.0.0.0", 0))
        self.listensock.listen()

        self.client_list = {}

        self.host = socket.gethostname()
        self.port = self.listensock.getsockname()[1]

        self.client_count_lock = Lock()
        self.client_count = 0

        loop = ConfigserverLoop(self.listensock, self)
        loop.start()

    def increment_client_count(self):
        self.client_count_lock.acquire()
        self.client_count = self.client_count + 1
        self.client_count_lock.release()

    def get_client_count(self):
        self.client_count_lock.acquire()
        ret = self.client_count
        self.client_count_lock.release()
        return ret

    def wait_for_clients(self, expected_count):
        trial_cnt = 0

        while True:
            cnt = self.get_client_count()
            print("[blue]Waiting for all clients to start ({}/{})...[/blue]".format(cnt, expected_count))
            if cnt == expected_count:
                break
            time.sleep(1)
            trial_cnt = trial_cnt + 1
            if trial_cnt == 1024:
                raise Exception("Failed to join all clients")


class ChildCommand():

    def watch(self, pid):
        self.children.append(pid)

    def run(self, cmd):

        child = os.fork()

        if child == 0:
            ret = os.execvp(cmd[0], cmd)
            sys.exit(ret)
        else:
            self.watch(child)

    def _collect(self):
        to_del = set()

        for p in self.children:
            pid = None
            try:
                pid, st = os.waitpid(-1, os.WNOHANG)
            except ChildProcessError:
                pass

            if pid:
                to_del.add(pid)

        self.children = list(set(self.children) - to_del)

    def wait(self):
        while self.children:
            self._collect()
            time.sleep(0.1)

    def __init__(self):

        self.children = []

        def _killall(a, b):
            # Start with a "gentle" signal and then escalate
            for p in self.children:
                os.kill(p, signal.SIGUSR1)

            self._collect()

            if self.children:
                time.sleep(5)

            # Are you still there, I start to become impatient...
            for p in self.children:
                os.kill(p, signal.SIGINT)

            self._collect()

            # srun may ask for double CTRL + C...
            for p in self.children:
                os.kill(p, signal.SIGINT)

            self._collect()

            # Sorry we are closing
            for p in self.children:
                os.kill(p, signal.SIGKILL)

            self._collect()

            sys.exit(1)

        signal.signal(signal.SIGINT, _killall)


class Prometheus():

    def _gen_config(self, targets):
        base = """
global:
  scrape_interval:     5s
  evaluation_interval: 5s

  external_labels:
      monitor: 'nodelevel'

scrape_configs:
  - job_name: 'prometheus'

    scrape_interval: 5s
    scrape_timeout: 5s

    static_configs:
      - targets: ['localhost:9090']
"""
        cnt = 0

        for t in targets:
            base = base + """
  - job_name: node-{}
    scrape_interval: 5s
    scrape_timeout: 5s
    static_configs:
      - targets: ['{}']
""".format(cnt, t)
            cnt=cnt+1

        tmp = tempfile.NamedTemporaryFile(suffix=".yml", prefix="tau_prometheus", delete=False)
        tmp.write(bytes(base, encoding="utf-8"))
        tmp.close()

        return tmp.name


    def _run(self):
        cmd = ["prometheus", "--config.file={}".format(self.config), "--web.listen-address=0.0.0.0:{}".format(self.port), "--storage.tsdb.path={}".format(self.tsdb_dir)]
        self.runner.run(cmd)
        print("[bold red]===========================================\n{} Prometheus running on http://{}:{}\n===========================================[/bold red]".format(self.desc, socket.gethostname(), self.port))

    def __init__(self, runner, desc, targets):
        if not which("prometheus"):
            print("[red]ERROR: prometheus cannot be started, no such command[/red]")
            return
        self.desc = desc
        self.runner = runner
        self.config = self._gen_config(targets)
        self.tsdb_dir = tempfile.mkdtemp(prefix="tau_prometheus_tsdb")
        self.port = (30000 + os.getpid()%5000)
        self._run()

    def __del__(self):
        try:
            pass
            os.unlink(self.config)
            shutil.rmtree(self.tsdb_dir)
        except:
            pass


class ADMIRE_node():

    def __init__(self, args, command_runner):
        self.runner = command_runner
        # Configure and connect socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        root_server = args.setup
        sinfo = root_server.split(":")
        if len(sinfo) != 2:
            raise Exception(
                "Root server address must be in the form HOST:PORT")
        host = sinfo[0]
        port = int(sinfo[1])

        count = 0

        while True:
            try:
                self.sock.connect((host, port))
                break
            except Exception as e:
                count = count + 1
                if 10 < count:
                    raise e
                else:
                    time.sleep(0.5)

        # Register in configuration server
        self._register_config(args)

    def _exchange_config(self, conf):
        jconf = json.dumps(conf)
        self.sock.sendall(bytes(jconf, encoding="utf-8"))
        ret = self.sock.recv(8192)
        return json.loads(ret.decode(encoding="utf-8"))

    def _run_metric_proxy(self, first=False):
        pid = os.getpid()
        random.seed(pid)
        proxy_path = "/tmp/tau_proxy_{}.unix".format(random.random())
        exporter_port = 20000 + pid % 1000

        self.config["tau_proxy"] = proxy_path
        self.config["tau_exporter"] = "{}:{}".format(
            socket.gethostname(), exporter_port)
        print("[bold red]Starting tau_metric_proxy at[/bold red] http://{}".format(
            self.config["tau_exporter"]))

        cmd = ["tau_metric_proxy"]

        if not first:
            cmd += ["-i"]

        cmd += ["-u", proxy_path, "-p", str(exporter_port)]

        self.runner.run(cmd)
        # Give it a second to start
        time.sleep(1)


    def _run_metric_client(self, path):
        # Build output file
        out_file = os.path.join(path, "{}-{}.json".format(socket.gethostname(), os.getpid()))
        cmd = ["tau_metric_client", "-u", self.config["tau_proxy"], "-t", "-o", out_file]
        self.runner.run(cmd)


    def run_instrumented(self, cmd):
        inst_cmd = ["tau_metric_proxy_run", "-u",
                    self.config["tau_proxy"], "-m", "-s", "--"]
        full_cmd = inst_cmd + cmd
        #print("[cyan]{}[/cyan]".format(" ".join(full_cmd)))
        self.runner.run(full_cmd)
        self.runner.wait()
        sys.exit(0)

    def _register_config(self, args):
        self.config = {"host": socket.gethostname()}
        self.config = self._exchange_config(self.config)

        if self.config["new"]:
            # We are the first for this process do scafold
            self._run_metric_proxy(self.config["first"])

            if args.log:
                self._run_metric_client(args.log)

            # Mark for overwrite
            self.config["force"] = True
            pass

        # Send the updated config
        self.config = self._exchange_config(self.config)

        self.sock.close()


def check_base_deps():
    if which("tau_metric_proxy_run") is None:
        raise Exception("tau_metric_proxy_run is not in PATH")


def inject_proxies(runner, conf_server, args, left_args):

    sep = left_args.index("--")
    prefix = left_args[0:sep]
    suffix = left_args[sep+1:]

    extra_logging = []

    if args.log:
        extra_logging =  extra_logging + ["-l", args.log]
        if args.freq:
            extra_logging =  extra_logging + ["-f", str(args.freq)]

    instrum = [SCRIPT, "-s", conf_server.address()] + extra_logging + ["-w", "--"]

    full_cmd = prefix + instrum + suffix

    print("[blue]Main wrapper command: {}[/blue]".format(" ".join(full_cmd)))

    runner.run(full_cmd)


def run():
    #
    # Argument parsing
    #

    parser = argparse.ArgumentParser(description='tau_metric_proxy client')

    #Run the program in wrapper mode (INTERNAL)
    parser.add_argument('-w', '--wrap', action='store_true',
                        help=argparse.SUPPRESS)

    parser.add_argument('-l', '--log', type=str,
                        help="Enable JSON performance data logging per tau_metric_exporter in LOG directory")

    parser.add_argument('-f', '--freq', type=int,
                        help="Logging frequency in Hz (maximum 10 Hz); only meaningful with --log")

    parser.add_argument('-j', '--numprocesses', type=int,
                        help="Number of processes to wait for before starting Prometheus")

    parser.add_argument('-p', '--prometheus', action='store_true',
                        help="Start central prometheus server (requires -n)")

    parser.add_argument('-o', '--output', type=str,
                        help="File to store the result to")

    parser.add_argument('-s', '--setup', type=str,
                        help="Pointer to the root node to proceed to node-level setup")

    args, left_args = parser.parse_known_args(sys.argv[1:])

    # ARG checking
    check_base_deps()

    if args.prometheus:
        if not which("prometheus"):
            raise Exception("Cannot locate 'prometheus' in path")
        if not args.numprocesses:
            raise Exception("The -p argument needs to know the number of processes to join use -j option")

    if args.log:
        if not os.path.isdir(args.log):
            os.mkdir(args.log)
    else:
        if args.freq:
            raise Exception("-f / --freq us not meaningful without -l / --log")

    # Prepare run components

    cc = ChildCommand()

    # Check for node-level setup
    # Starting the tau_metric_proxy and the optionnal prometheus
    if args.setup:
        node = ADMIRE_node(args, cc)

    if args.wrap:
        if left_args[0] == "--":
            left_args = left_args[1:]
        node.run_instrumented(left_args)

    if "--" not in left_args:
        raise Exception(
            "It is mandatory to indicate where the user command starts with '--'")

    # Here we are the root server act as one
    conf_server = ADMIRE_config_server()

    # And now we need to run the launch command for back-multiplex
    inject_proxies(cc, conf_server, args, left_args)

    if args.prometheus:
        conf_server.wait_for_clients(args.numprocesses)
        p = Prometheus(cc, "Main", conf_server.tau_exporter_list())

    # Wait for all processest to complete
    cc.wait()
    del(p)