#!/usr/bin/env python3
"""Turns a profile written by the Wii player (autoplay "profile stop") into
per-function CPU time, using the symbols of the matching boot.elf.

    python wii_profile.py boot.elf level1.prof [--top 40] [--phase draw]
        [--thread main] [--longest 5] [--callers NAME] [--callees NAME]

"self" is time spent in a function's own code, "total" includes what it
calls. Phases are set by the player: logic (CKContext::Process), draw
(Render up to the GPU wait), gpu and vsync (waiting), load and other.
--longest picks the frames in which the game's thread worked longest, to
see what causes hitches.
"""

import argparse
import bisect
import collections
import os
import shutil
import struct
import subprocess
import sys

PHASES = ["other", "logic", "draw", "gpu", "vsync", "load"]


def find_nm():
    for name in ("powerpc-eabi-nm", "powerpc-eabi-nm.exe"):
        path = shutil.which(name)
        if path:
            return path
    devkitppc = os.environ.get("DEVKITPPC") or os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "devkitPPC")
    for candidate in (os.path.join(devkitppc, "bin", "powerpc-eabi-nm"),
                      os.path.join(devkitppc, "bin", "powerpc-eabi-nm.exe"),
                      r"C:\devkitPro\devkitPPC\bin\powerpc-eabi-nm.exe"):
        if os.path.exists(candidate):
            return candidate
    sys.exit("powerpc-eabi-nm was not found; add devkitPPC's bin folder to PATH")


def load_symbols(elf):
    out = subprocess.run([find_nm(), "-n", "-C", "--defined-only", elf], capture_output=True, text=True,
                         check=True).stdout
    addresses, names = [], []
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3 or parts[1] not in "TtWw":
            continue
        addresses.append(int(parts[0], 16))
        names.append(parts[2])
    return addresses, names


def load_samples(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, count, words, interval = struct.unpack_from(">5I", data, 0)
    if magic != 0x57505246 or version not in (1, 2):
        sys.exit("%s is not a Wii profile" % path)
    samples = []
    offset = 20
    for _ in range(count):
        sample = list(struct.unpack_from(">%dI" % words, data, offset))
        offset += words * 4
        # Version 2 keeps the frame number above the phase: thread, phase,
        # frame, pc, lr, return addresses.
        frame = sample[1] >> 8 if version >= 2 else 0
        sample[1] &= 0xFF
        sample.insert(2, frame)
        samples.append(sample)
    return samples, interval


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("elf")
    parser.add_argument("profile")
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--phase", choices=PHASES, action="append", help="only samples in this phase")
    parser.add_argument("--thread", help="'main' (the busiest thread) or a thread address in hex")
    parser.add_argument("--longest", type=int, help="only the game thread's N longest frames")
    parser.add_argument("--callers", help="show who calls functions whose name contains this")
    parser.add_argument("--callees", help="show what functions whose name contains this call")
    args = parser.parse_args()

    addresses, names = load_symbols(args.elf)
    samples, interval = load_samples(args.profile)

    cache = {}

    def function(address):
        name = cache.get(address)
        if name is None:
            i = bisect.bisect_right(addresses, address) - 1
            name = names[i] if i >= 0 else "0x%08x" % address
            cache[address] = name
        return name

    threads = collections.Counter(s[0] for s in samples)
    # The game's thread is the one doing the logic and drawing; while it waits
    # for the GPU or the TV another thread (the idle loop) runs.
    busy_phases = (1, 2, 5)
    busy = collections.Counter(s[0] for s in samples if s[1] in busy_phases)
    main_thread = (busy or threads).most_common(1)[0][0] if threads else 0
    print("%d samples every %d us (%.1f s); threads: %s" % (
        len(samples), interval, len(samples) * interval / 1e6,
        ", ".join("%08x %.1f%%" % (t, 100.0 * n / len(samples)) for t, n in threads.most_common())))
    phases = collections.Counter(s[1] for s in samples)
    print("time by phase: " + ", ".join(
        "%s %.1f%%" % (PHASES[p] if p < len(PHASES) else p, 100.0 * n / len(samples)) for p, n in phases.most_common()))

    selected = samples
    if args.thread:
        thread = main_thread if args.thread == "main" else int(args.thread, 16)
        selected = [s for s in selected if s[0] == thread]
    if args.phase:
        wanted = {PHASES.index(p) for p in args.phase}
        selected = [s for s in selected if s[1] in wanted]
    if args.longest:
        work = collections.Counter(s[2] for s in samples if s[0] == main_thread and s[1] in busy_phases)
        frames = work.most_common(args.longest)
        print("longest frames: " + ", ".join("#%d %.1f ms" % (f, n * interval / 1000.0) for f, n in frames))
        keep = {f for f, _ in frames}
        selected = [s for s in selected if s[2] in keep]
    total = len(selected)
    if not total:
        sys.exit("no samples selected")
    print("%d samples selected\n" % total)

    self_counts = collections.Counter()
    total_counts = collections.Counter()
    callers = collections.Counter()
    callees = collections.Counter()
    for s in selected:
        pc, lr = s[3], s[4]
        chain = [a for a in s[5:] if a]
        here = function(pc)
        stack = [here]
        first = function(chain[0]) if chain else None
        # A leaf function has no frame yet: its caller is only in the link register.
        if lr and function(lr) not in (here, first):
            stack.append(function(lr))
        stack.extend(function(a) for a in chain)
        self_counts[here] += 1
        for name in set(stack):
            total_counts[name] += 1
        for i, name in enumerate(stack):
            if args.callers and args.callers in name and i + 1 < len(stack):
                callers[stack[i + 1]] += 1
            if args.callees and args.callees in name and i > 0:
                callees[stack[i - 1]] += 1

    def table(title, counter):
        print(title)
        for name, n in counter.most_common(args.top):
            print("  %6.2f%%  %s" % (100.0 * n / total, name))
        print()

    table("Self", self_counts)
    table("Total", total_counts)
    if args.callers:
        table("Callers of *%s*" % args.callers, callers)
    if args.callees:
        table("Called by *%s*" % args.callees, callees)


if __name__ == "__main__":
    main()
