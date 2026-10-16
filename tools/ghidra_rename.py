#!/usr/bin/env python3
"""Apply function names from a text file to the program open in Ghidra (via GhidraMCP).

Usage: python ghidra_rename.py NAMES.txt [--url http://127.0.0.1:8080/]

NAMES.txt has one "address name" pair per line; '#' starts a comment. Keeping the names in a
file in the repo means the labels survive a re-import and can be applied to the other volumes.
"""

import argparse

import requests


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("names")
    ap.add_argument("--url", default="http://127.0.0.1:8080/")
    args = ap.parse_args()
    url = args.url if args.url.endswith("/") else args.url + "/"

    s = requests.Session()
    ok = failed = 0
    for line in open(args.names, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        addr, name = line.split()
        r = s.post(url + "rename_function_by_address",
                   data={"function_address": addr, "new_name": name}, timeout=60)
        body = r.json() if r.ok else {"success": False, "error": r.text}
        if body.get("success"):
            ok += 1
        else:
            failed += 1
            print(f"{addr} {name}: {body.get('error')}")
    print(f"renamed {ok}, failed {failed}")


if __name__ == "__main__":
    main()
