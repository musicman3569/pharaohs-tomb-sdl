#!/usr/bin/env python3
"""Dump every function's disassembly from the running GhidraMCP backend to a text file.

Usage: python ghidra_dump.py OUTPUT.txt [--url http://127.0.0.1:8080/]

The output is one block per function ("== name @ address"), one instruction per line, so the
whole program can be searched with grep (for example for INT, IN and OUT instructions).
Read-only: it never changes the Ghidra database.
"""

import argparse

import requests


def get(session, url, endpoint, **params):
    r = session.get(url + endpoint, params=params, timeout=120)
    r.raise_for_status()
    body = r.json()
    if not body.get("success"):
        raise RuntimeError(f"{endpoint}: {body.get('error', body)}")
    return body["data"]


def paged(session, url, endpoint, **params):
    offset = 0
    while True:
        page = get(session, url, endpoint, offset=offset, limit=500, **params)
        yield from page["items"]
        if not page.get("has_more"):
            break
        offset += len(page["items"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("output")
    ap.add_argument("--url", default="http://127.0.0.1:8080/")
    args = ap.parse_args()
    url = args.url if args.url.endswith("/") else args.url + "/"

    s = requests.Session()
    funcs = list(paged(s, url, "functions"))
    with open(args.output, "w", encoding="utf-8") as out:
        for f in funcs:
            out.write(f"== {f['name']} @ {f['address']}\n")
            for ins in paged(s, url, "disassemble_function", address=f["address"]):
                comment = f"  ; {ins['comment']}" if ins.get("comment") else ""
                out.write(f"{ins['address']}  {ins['instruction']}{comment}\n")
    print(f"{len(funcs)} functions -> {args.output}")


if __name__ == "__main__":
    main()
