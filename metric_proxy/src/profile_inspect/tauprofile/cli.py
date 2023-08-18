#!/bin/env python3

import os
import sys
import glob
import json
import argparse
import hashlib
from ctypes import *
from enum import Enum
import re
import time
import tempfile
import subprocess
from datetime import datetime

#from rich import print

def remove_subsecond_ts(event_array):
   ret = []

   # First keep largest sample per second
   # as prometheus is not subsecond
   prev_ts = 0
   last_ev = None
   for e in event_array:
      if not prev_ts:
         prev_ts = int(e["ts"])
      elif int(e["ts"]) != prev_ts:
         if last_ev:
            last_ev["ts"] = int(last_ev["ts"])
            ret.append(last_ev)
         prev_ts = int(e["ts"])
      else:
         last_ev = e

   return ret

def rewrite_ts(per_key_events):
   mins = []
   maxs = []

   for v in per_key_events.values():
      if len(v):
         mins.append(v[0]["ts"])
         maxs.append(v[-1]["ts"])
         #print("lMin : {} lMax : {}".format(datetime.fromtimestamp(v[0]["ts"]), datetime.fromtimestamp(v[-1]["ts"])))


   m = min(mins)
   ma = max(maxs)

   print("Min : {} Max : {}".format(datetime.fromtimestamp(m), datetime.fromtimestamp(ma)))

   now_ts = int(time.time())
   shift = now_ts - m - (ma - m)


   for v in per_key_events.values():
      for e in v:
         #print("From : {} To : {}".format(datetime.fromtimestamp( e["ts"] ), datetime.fromtimestamp(e["ts"] + shift)))
         e["ts"] = e["ts"] + shift


def snapshot_centroid(snap, key):
   summation = 0
   tss=[]
   for v in snap:
      if v["name"].startswith(key):
         summation += v["value"]
         tss.append(v["ts"])

   avg_ts = sum(tss)/len(tss) if len(tss) else 0

   return summation, avg_ts


class InputFile():

   def __init__(self, path):
      self.path = path
      self.data = None
      self.hostname="host"
      self._extract_hostname()
      self._load()


   def _extract_hostname(self):
      filename = os.path.basename(self.path)
      ret = re.findall(r"(.*)-[0-9]+.json", filename)
      if not ret:
         raise Exception("Bad trace name format, expected HOSTNAME-PID.json")
      self.hostname = ret[0]
      print("Loaded a trace from {}".format(self.hostname), file=sys.stderr)

   def _load(self):
      with open(self.path, "r") as f:
         data = f.read().replace("\n","")

      if data.endswith("],"):
         data = data[:-1] + "]"
      elif data.endswith("},"):
         data = data[:-1] + "]]"

      self.data = json.loads(data)

   def contextualize(self, events):
      ret = []
      for x in events:
         x["name"] = x["name"].replace("{", "{job=\"" + self.hostname + "\", ")
         ret.append(x)
      return ret

   def gather_times(self):
      ret = []
      for snap in self.data:
         syscall_time, scall_time_ts = snapshot_centroid(snap, "strace_time_total")
         mpi_time, mpi_time_ts = snapshot_centroid(snap, "tau_mpi_total{metric=\"time\"}")
         ret.append({"mpi" : mpi_time, "mpi_ts" : mpi_time_ts, "syscall_time" : syscall_time, "scall_time_ts" : scall_time_ts, "ts" : (mpi_time_ts + scall_time_ts) / 2})
      return ret


   def keys(self):
      ret = set()
      for snap in self.data:
         for entries in snap:
            ret.add(entries["name"])
      return ret

   def extract(self, entry):
      ret = []
      for snap in self.data:
         r = [x for x in snap if x["name"] == entry]
         if r:
            ret = ret + r
      return self.contextualize(ret)

   def extract_job_keys(self):
      perkey = {}
      for snapshot in self.data:
         csnap = self.contextualize(snapshot)
         for entry in csnap:
            n = entry["name"]
            if "{" in n:
               n = n.split("{")[0]
            if n not in perkey:
               perkey[n] = []
            perkey[n].append(entry)

      # Now make each counter snapshot unique
      for k, v in perkey.items():
         perkey[k] = remove_subsecond_ts(v)

      rewrite_ts(perkey)

      return perkey

def merge_per_key(a, b):
   ret = {}
   for ak,av in a.items():
      ret[ak] = av
      if ak in b.keys():
         ret[ak] = ret[ak] + b[ak]
         ret[ak].sort(key=lambda x: x["ts"])
   return ret


class TauJob():

   def __init__(self, dest):
      candi = glob.glob(os.path.join(dest, "*.json"))
      if not candi:
         raise Exception("No candiate *.json file found in {}".format(dest))
      self.inputs = [ InputFile(x) for x in candi]



   def keys(self):
      ret = set()
      for i in self.inputs:
         ret.update(i.keys())
      return ret

   def delta_time(self):
      for i in self.inputs:
         print(i.gather_times())

   def extract(self, entry):
      ret = []
      for i in self.inputs:
         r = i.extract(entry)
         if r:
            ret = ret  + r

      return sorted(ret, key=lambda c : c["ts"])


   def extract_job_keys(self):
      job_data = []
      for i in self.inputs:
         job_data.append(i.extract_job_keys())

      while len(job_data) > 1:
         tmp = merge_per_key(job_data[0], job_data[1])
         job_data = job_data[1:]
         job_data[0] = tmp

      return job_data[0]


   def to_openmetrics(self, prompath):
      data = self.extract_job_keys()

      clean_data = {}
      # We need to remove duplicate timestamps to fit in prometheus
      for k,v in data.items():
         last_ts = -1
         l = len(v)
         clean_data[k] = []
         for i in range(0,l):
            lts = v[i]["ts"]
            if lts != last_ts or last_ts < 0:
               last_ts = lts
               clean_data[k].append(v[i])

      data = clean_data


      ret = ""

      for k,v in data.items():
         ret += "# HELP {} {}\n".format(k,k)
         ret += "# TYPE {} counter\n".format(k)
         for e in v:
            ret += "{} {} {}\n".format(e["name"].replace(", ", ","), e["value"], int(e["ts"]))
      ret += "# EOF\n"

      tmp = tempfile.NamedTemporaryFile(suffix=".yml", prefix="tau_prometheus", delete=False)
      tmp.write(ret.encode("utf-8"))
      print(tmp.name)
      subprocess.call(["promtool", "tsdb", "create-blocks-from", "openmetrics", tmp.name, prompath])


def listkeys(job):
   print("Loading keys ... ", end="")
   sys.stdout.flush()
   keys = job.keys()
   print("[green]OK[/green]")
   print("\n".join([ " - {}".format(x) for x in keys]))




# Define the client C API

METRIC_STRING_SIZE=300

class metric_type(Enum):
    TAU_METRIC_NULL = 0
    TAU_METRIC_COUNTER = 1
    TAU_METRIC_GAUGE = 2

class tau_metric_job_descriptor_t(Structure):
    _fields_ = [("jobid", c_char*64),
                ("command", c_char*512),
                ("size", c_int),
                ("nodelist", c_char*128),
                ("partition", c_char*64),
                ("cluster", c_char*64),
                ("run_dir", c_char*256),
                ("start_time", c_size_t),
                ("end_time", c_size_t)]

class tau_metric_dump_t(Structure):
    _fields_ = [("metric_count", c_int), ("desc", tau_metric_job_descriptor_t)]

class tau_metric_event_t(Structure):
    _fields_ = [("name", c_char*METRIC_STRING_SIZE), ("value", c_double), ("update_ts", c_double)]

class tau_metric_snapshot_t(Structure):
    _fields_ = [("type", c_int), ("doc", c_char*METRIC_STRING_SIZE), ("event", tau_metric_event_t), ("canary", c_int)]

# Done with the C API

class SingleProfile():

    def __init__(self, path_to_prof):
        self.path = path_to_prof
        self.dump = tau_metric_dump_t()
        if not os.path.isfile(path_to_prof):
            raise Exception("Cannot locate profile {}".format(path_to_prof))
        with open(path_to_prof, "rb") as f:
            f.readinto(self.dump)
        self.desc = self.json_desc()

    def size(self):
        return self.desc["size"]

    def job_id(self):
        return self.desc["jobid"]

    def print_desc(self):
        desc = self.json_desc()
        print("==================")
        print("METRICS   : {}".format(desc["metric_count"]))
        print("START TS  : {}".format(desc["start_time"]))
        print("END TS    : {}".format(desc["end_time"]))
        print("JOBID     : {}".format(desc["jobid"]))
        print("SIZE      : {}".format(desc["size"]))
        print("COMMAND   : {}".format(desc["command"]))
        print("NODES     : {}".format(desc["nodelist"]))
        print("PARTITION : {}".format(desc["partition"]))
        print("CLUSTER   : {}".format(desc["cluster"]))
        print("RUNDIR    : {}".format(desc["rundir"]))
        print("==================")

    def json_desc(self):
        return {
            "metric_count" : self.dump.metric_count,
            "start_time" : self.dump.desc.start_time,
            "end_time" : self.dump.desc.end_time,
            "jobid" : self.dump.desc.jobid.decode("ascii"),
            "size" : self.dump.desc.size,
            "command" : self.dump.desc.command.decode("ascii"),
            "nodelist" : self.dump.desc.nodelist.decode("ascii"),
            "partition" : self.dump.desc.partition.decode("ascii"),
            "cluster" : self.dump.desc.cluster.decode("ascii"),
            "rundir" : self.dump.desc.run_dir.decode("ascii")
        }

    def get_values(self):
        values = {}
        dump = tau_metric_dump_t()
        with open(self.path, "rb") as f:
            f.readinto(dump)
            for i in range(0, dump.metric_count):
                s = tau_metric_snapshot_t()
                f.readinto(s)
                if s.event.value == 0:
                    continue
                name = s.event.name.decode("ascii")
                values[name] = {
                    "doc" : s.doc.decode("ascii"),
                    "type" : metric_type(s.type).name,
                    "value" : s.event.value
                }
        return values



class Profiles():

    def __init__(self, path_to_prof):
        if not os.path.isdir(path_to_prof):
            raise Exception("Could not find profile directory {}".format(path_to_prof))

        self.prof_files = glob.glob("{}/**/*.profile".format(path_to_prof), recursive=True)
        self.profiles = [ SingleProfile(x) for x in self.prof_files ]
        self.jobids = {}
        for p in self.profiles:
            self.jobids[p.job_id()] = p

    def do_list(self):
        for x in self.profiles:
            x.print_desc();

    def id_list(self):
        return [ x.job_id() for x in self.profiles]

    def select(self, jobid):
        if jobid not in self.jobids.keys():
            raise Exception("No such jobid {}".format(jobid))
        return self.jobids[jobid]

    def groups_by_cmd(self):
        ret = {}
        for p in self.profiles:
            cmd = p.desc["command"]
            key = hashlib.md5(cmd.encode("ascii")).hexdigest()
            if key not in ret:
                ret[key] = []
            ret[key].append(p.json_desc())

        for k, v in ret.items():
            ret[k] = sorted(v, key=lambda d: d['size'])

        return ret

    def get_by_id(self, id):
        for p in self.profiles:
            if id == p.desc["jobid"]:
                return p
        return None

    def get_values(self, id_list):
        ret = {}
        for jid in id_list:
            if jid not in self.id_list():
                raise Exception("No such job ID {}".format(jid))
            p = self.get_by_id(jid)
            ret[p.job_id()] = p.get_values()
        return ret


    def format_extrap(self, size, k, v):
        v = v if isinstance(v, int) else  v["value"]
        try:
            start = k.find("{")
            if "_time_" in k[:start]:
                metric = "Time (s)"
            elif "_size_" in k[:start]:
                metric = "Size (B)"
            elif "_hits_" in k[:start]:
                metric = "Hits"
            else:
                metric = "other"

            fstart   = k.find("=")
            callpath = k[:start] + "->" + k[start+1:fstart] + "->" + k[fstart+1:-1]
            datum    = { "params": { "Processes": size}, "callpath": callpath, "metric" : metric, "value": v }

        except Exception as e: 
            print("A Error occured in parssing:\n >>" + e)
            datum = { "params": { "Processes": size}, "metric" : k, "value": v }

        return datum

    # def compute_to_io_ratio(self, size, k, v):
    #     ratios[str(size)] = 

    def extrap(self, jobs, extrapsort=False):
        jobs_per_id = { x.job_id():x for x in jobs}
        vals = self.get_values([x.job_id() for x in jobs])

        ret=""

        # We do not want to generate models on crashed applications
        # we therefore remove the jobs with the nonzero flag
        kept_group = []

        for id in jobs_per_id.keys():
            prof = jobs_per_id[id]
            do_keep = 1
            label = "tau_metric_proxy_run_total{operation=\"badreturn\"}"
            if label in vals.keys():
                if prof[label]["value"]:
                    print("INFO JOB {} did crash ignoring for Extra-P Model".format(id))
                    do_keep = 0
            if do_keep:
                kept_group.append(prof.job_id())

        # First make sure all metrics are present in ALL measures
        # otherwise extra-p is a bit puzzled
        # Therefore we remove what does not match the scan
        metric_set = set()

        for id in kept_group:
            prof = jobs_per_id[id]
            # Make a full list of keys and pad with zeroes
            metric_set.update(set(vals[prof.job_id()].keys()))


        for id in kept_group:
            prof = jobs_per_id[id]
            try:
                data = dict(sorted(vals[prof.job_id()].items()))
            except: 
                data = vals[prof.job_id()]
            size = prof.size()
            if extrapsort:
                for k,v in data.items():
                    datum=self.format_extrap(size, k, v)
                    ret += "{}\n".format(json.dumps(datum))
                local_set = set(vals[prof.job_id()].keys())
                for missing in metric_set - local_set:
                    print("Warning profile {} does not contain {}".format(id, missing))
                    datum=self.format_extrap(size, missing, 0)
                    ret += "{}\n".format(json.dumps(datum))
            else:
                for k,v in vals[prof.job_id()].items():
                    datum = { "params": { "ranks": size}, "metric" : k, "value": v["value"] }
                    ret += "{}\n".format(json.dumps(datum))
                local_set = set(vals[prof.job_id()].keys())
                for missing in metric_set - local_set:
                    print("Warning profile {} does not contain {}".format(id, missing))
                    datum = { "params": { "ranks": size}, "metric" : missing, "value": 0.0 }
                    ret += "{}\n".format(json.dumps(datum))
        return ret



def run():

    #
    # Argument parsing
    #

    parser = argparse.ArgumentParser(description='tau_metric_proxy client')

    # Profile Support
    parser.add_argument('-p', '--profiles', type=str, help="PROFILE Path to the profile storage directory")
    parser.add_argument('-l', "--list",  action='store_true', help="PROFILE List profile descriptions")
    parser.add_argument('-v', "--values",  action='store_true', help="PROFILE List values from the selected profile")
    parser.add_argument('-g', "--group",  action='store_true', help="PROFILE Group profiles by commands")
    parser.add_argument('-G', "--selectgroup",  type=str,  help="PROFILE Select a group of commands")
    parser.add_argument('-s', '--select', type=str, nargs="*", action='append', help="PROFILE Select one profile from its jobid to print it")
    parser.add_argument('-E', "--extrap", type=str,  help="PROFILE Path where to store the extra-p data")
    parser.add_argument('-c', "--extrapsort", action='store_true',  help="Sorts metrics in a callpath structure for extra-P")

    # Trace Support
    parser.add_argument('-f', '--fromdir', type=str, help="TRACE Directory from where to load the .json output files")
    parser.add_argument('-L', "--listprofiles",  action='store_true', help="TRACE List keys in the trace")
    parser.add_argument('-e', "--extracttrace",  type=str, help="TRACE Extract given events from the trace")
    parser.add_argument('-o', '--out', type=str, help="TRACE Output file")
    parser.add_argument('-P', "--prometheus",  type=str, help="TRACE Path to Promeheus data directory")


    args = parser.parse_args(sys.argv[1:])

    ## Handling of trace support

    if args.fromdir:
        j = TauJob(args.fromdir)

        # Listing all keys in trace
        if args.listprofiles:
            listkeys(j)
            sys.exit(0)

        out = sys.stdout

        if args.out:
            out = open(args.out, "w")

        if args.extracttrace:
            json.dump(j.extract(args.extracttrace), out, indent=4)

        if args.out:
            out.close()

        if args.prometheus:
            j.to_openmetrics(args.prometheus)


        sys.exit(0)


    ## Handling of profile support


    prof_dir = os.path.expanduser("~/.tauproxy/profiles")

    if args.profiles:
        prof_dir = os.path.expanduser(args.profiles)

    prof = Profiles(prof_dir)

    if args.list:
        prof.do_list()
        return 0

    output=None

    profile_list = []
    groups = {}


    if args.select:
        profile_list = [prof.select(x[0]) for x in args.select]


    if args.group:
        groups = prof.groups_by_cmd()
        output = groups

    if args.selectgroup:
        if "," in args.selectgroup:
            # The JOB ID case
            profile_list = [prof.select(x) for x in args.selectgroup.split(",") if x]
        else:
            groups = prof.groups_by_cmd()
            if args.selectgroup not in groups.keys():
                raise Exception("No such run group {}".format(args.selectgroup))
            jid = [ x["jobid"] for x in groups[args.selectgroup]]
            profile_list = [prof.select(x) for x in jid]


    if args.extrap:
        if not args.selectgroup:
            raise Exception("You must select a profiles with -G")
            

        ret = prof.extrap(profile_list,args.extrapsort)

        with open(args.extrap, "w") as f:
            f.write(ret)

        return 0



    if args.values:
        if not profile_list:
            raise Exception("You must select profiles with -s/-G")
        output = prof.get_values([p.job_id() for p in profile_list])





    if output is not None:
        print(json.dumps(output, indent=4))
    else:
        print("No operations selected, see --help")

    return 1