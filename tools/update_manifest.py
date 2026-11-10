#!/usr/bin/env python3
"""Copies the archive sums of build/native/SHA256SUMS into eskiu.toml's [native.eskqjs].

    python3 -I tools/update_manifest.py           # rewrite the sums and release URLs
    python3 -I tools/update_manifest.py --check   # exit 1 if eskiu.toml differs

Run from the package root after tools/build_native.sh has built every platform. The
URLs point at the release of the package version (vVERSION) of this repository, where
the archives are uploaded with the same names.
"""
import re
import sys

REPO = "https://github.com/eskiu-lang/quickjs"


def main():
    check = "--check" in sys.argv[1:]
    toml = open("eskiu.toml").read()
    version = re.search(r'^version = "([^"]+)"', toml, re.M).group(1)
    sums = {}
    for line in open("build/native/SHA256SUMS"):
        digest, name = line.split()
        prefix, suffix = f"eskqjs-{version}-", ".tar.gz"
        if name.startswith(prefix) and name.endswith(suffix):
            sums[name[len(prefix):-len(suffix)]] = (digest, name)

    seen = set()

    def entry(m):
        platform = m.group(1)
        if platform not in sums:
            sys.exit(f"update_manifest: no archive for {platform} in build/native/SHA256SUMS")
        seen.add(platform)
        digest, name = sums[platform]
        return (f'{platform} = {{ url = "{REPO}/releases/download/v{version}/{name}", '
                f'sha256 = "{digest}" }}')

    out = re.sub(r'^([a-z0-9_-]+) = \{ url = "[^"]*", sha256 = "[0-9a-f]*" \}', entry, toml, flags=re.M)
    missing = sorted(set(sums) - seen)
    if missing:
        sys.exit("update_manifest: eskiu.toml has no entry for " + ", ".join(missing))
    if check:
        if out != toml:
            sys.exit("update_manifest: eskiu.toml does not match build/native/SHA256SUMS")
        return
    open("eskiu.toml", "w").write(out)


main()
