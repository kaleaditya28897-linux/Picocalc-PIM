#!/usr/bin/env python3
"""Turn this computer's SSH aliases into the PicoCalc's saved connections.

    tools/pim-hosts-import.py                       # show what would be written
    tools/pim-hosts-import.py --out /mnt/sd/PIM/hosts.cfg --merge
    tools/pim-hosts-import.py --resolve-tailnet --relay 192.168.1.9 --print-forwards

It reads ~/.ssh/config, asks `ssh -G` what each alias actually resolves to (so
Include, Match and defaults are handled by ssh itself), and writes the
tab-separated records that src/core/hosts.c parses. Names, ports and key names
are clipped and sanitised to the limits in include/pim/hosts.h, so the device
cannot reject what comes out.

The PicoCalc cannot resolve MagicDNS names and, until the Tailscale client
lands, cannot reach a tailnet at all. --resolve-tailnet writes the 100.x address
instead of the name, and --relay routes each host through a machine on the LAN
that forwards to it; --print-forwards then prints the commands to run there.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ALIAS_MAX = 20
ADDRESS_MAX = 64
USER_MAX = 24
KEY_MAX = 24

TAILNET_SUFFIXES = (".ts.net",)


def warn(message):
    print(f"pim-hosts-import: {message}", file=sys.stderr)


def private_dir():
    """Where generated host lists and copied keys belong: outside the repository,
    and somewhere you can exclude from whatever syncs your home directory.
    Override with PICODECK_PRIVATE_DIR."""
    override = os.environ.get("PICODECK_PRIVATE_DIR")
    if override:
        return os.path.expanduser(override)
    base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "picodeck")


def git_tree_above(path):
    """The work tree a path sits in, or None. Host names, logins and private keys
    have no business inside one, however gitignored the directory looks today."""
    current = os.path.abspath(path)
    while True:
        if os.path.exists(os.path.join(current, ".git")):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            return None
        current = parent


def refuse_if_tracked(path, kind, allowed):
    tree = git_tree_above(os.path.dirname(os.path.abspath(path)) or ".")
    if tree is None or allowed:
        return False
    warn(f"refusing to write {kind} to {path}: that is inside the git work tree at "
         f"{tree}. Leave --out off to preview, use --private, or pass --allow-in-repo "
         f"if you really mean it.")
    return True


def make_private(path, mode=0o700):
    os.makedirs(path, mode=mode, exist_ok=True)
    try:
        os.chmod(path, mode)
    except OSError:
        pass
    return path


# ---------------------------------------------------------------- ssh config

def read_config_text(path, seen=None):
    """Reads a config, following Include directives the way ssh does."""
    seen = seen or set()
    real = os.path.realpath(os.path.expanduser(path))
    if real in seen or not os.path.isfile(real):
        return ""
    seen.add(real)
    lines = []
    with open(real, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            stripped = line.strip()
            if stripped.lower().startswith("include "):
                for pattern in stripped.split(None, 1)[1].split():
                    pattern = os.path.expanduser(pattern)
                    if not os.path.isabs(pattern):
                        pattern = os.path.join(os.path.dirname(real), pattern)
                    import glob
                    for included in sorted(glob.glob(pattern)):
                        lines.append(read_config_text(included, seen))
                continue
            lines.append(line)
    return "".join(lines)


def aliases_from_config(text):
    """Every Host pattern that names one real host: no wildcards, no negations."""
    found = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = re.split(r"[\s=]+", line, maxsplit=1)
        if len(parts) != 2 or parts[0].lower() != "host":
            continue
        for pattern in parts[1].split():
            if any(c in pattern for c in "*?!") or pattern in found:
                continue
            found.append(pattern)
    return found


def resolve_with_ssh(alias):
    """`ssh -G` gives the effective settings, which is what ssh itself would use."""
    try:
        result = subprocess.run(["ssh", "-G", alias], capture_output=True, text=True,
                                timeout=15, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        warn(f"{alias}: could not run ssh -G ({error})")
        return None
    if result.returncode != 0:
        warn(f"{alias}: ssh -G failed")
        return None
    settings = {}
    for line in result.stdout.splitlines():
        key, _, value = line.partition(" ")
        key = key.lower()
        if key == "identityfile":
            settings.setdefault("identityfile", []).append(value)
        elif key not in settings:
            settings[key] = value
    return settings


def parse_blocks(text):
    """Fallback parser for when ssh -G is unavailable: first match wins, as ssh does."""
    blocks, current = [], None
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = re.split(r"[\s=]+", line, maxsplit=1)
        if len(parts) != 2:
            continue
        keyword, value = parts[0].lower(), parts[1].strip()
        if keyword == "host":
            current = {"patterns": value.split(), "settings": {}}
            blocks.append(current)
        elif keyword == "match":
            current = None
        elif current is not None:
            if keyword == "identityfile":
                current["settings"].setdefault("identityfile", []).append(value)
            else:
                current["settings"].setdefault(keyword, value)
    return blocks


def resolve_from_blocks(alias, blocks):
    import fnmatch
    settings = {}
    for block in blocks:
        if not any(fnmatch.fnmatch(alias, p) for p in block["patterns"] if not p.startswith("!")):
            continue
        for key, value in block["settings"].items():
            if key == "identityfile":
                settings.setdefault("identityfile", []).extend(value)
            else:
                settings.setdefault(key, value)
    settings.setdefault("hostname", alias)
    return settings


# ------------------------------------------------------------------ tailnet

def tailnet_addresses():
    """Maps tailnet names to their 100.x address using the local tailscale CLI."""
    if shutil.which("tailscale") is None:
        warn("tailscale CLI not found; names will be written as they are")
        return {}
    try:
        import json
        result = subprocess.run(["tailscale", "status", "--json"], capture_output=True,
                                text=True, timeout=20, check=False)
        if result.returncode != 0:
            warn("tailscale status failed; is this machine logged in?")
            return {}
        status = json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        warn(f"could not read tailscale status ({error})")
        return {}
    table = {}
    peers = list((status.get("Peer") or {}).values())
    if status.get("Self"):
        peers.append(status["Self"])
    for peer in peers:
        addresses = [a for a in (peer.get("TailscaleIPs") or []) if ":" not in a]
        if not addresses:
            continue
        name = (peer.get("DNSName") or "").rstrip(".")
        if name:
            table[name.lower()] = addresses[0]
            table[name.split(".", 1)[0].lower()] = addresses[0]
        if peer.get("HostName"):
            table.setdefault(peer["HostName"].lower(), addresses[0])
    return table


def is_tailnet_name(name):
    return any(name.lower().endswith(suffix) for suffix in TAILNET_SUFFIXES)


# ------------------------------------------------------------------- output

def sanitise_alias(name, taken):
    cleaned = re.sub(r"[^a-z0-9_-]+", "-", name.lower()).strip("-") or "host"
    cleaned = cleaned[:ALIAS_MAX]
    candidate, index = cleaned, 2
    while candidate in taken:
        suffix = str(index)
        candidate = cleaned[: ALIAS_MAX - len(suffix)] + suffix
        index += 1
    taken.add(candidate)
    return candidate


def pick_identity(paths):
    """ssh -G always lists the default identity files whether they exist or not,
    so the one to record is the first that is really on this machine."""
    for path in paths or []:
        expanded = os.path.expanduser(path)
        if os.path.isfile(expanded) or os.path.isfile(expanded + ".pub"):
            return sanitise_key(expanded)
    return ""


def sanitise_key(path):
    """A key name becomes a file name on the card: lower case, plain, short."""
    if not path:
        return ""
    stem = os.path.basename(os.path.expanduser(path))
    if stem.endswith(".pub"):
        stem = stem[:-4]
    stem = re.sub(r"[^a-z0-9_-]+", "-", stem.lower()).strip("-")
    return stem[:KEY_MAX]


def printable(value, limit):
    value = "".join(c for c in value if 0x20 <= ord(c) < 0x7f and c != "\t")
    return value[:limit]


def record(entry):
    return "host={alias}\t{kind}\t{address}\t{port}\t{user}\t{key}\t{relay}\t{relay_port}".format(**entry)


def load_existing(path):
    entries = []
    if not path or not os.path.isfile(path):
        return entries
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\r\n")
            if not line.startswith("host="):
                continue
            fields = line[5:].split("\t")
            if len(fields) != 8:
                continue
            entries.append({
                "alias": fields[0], "kind": fields[1], "address": fields[2],
                "port": fields[3], "user": fields[4], "key": fields[5],
                "relay": fields[6], "relay_port": fields[7],
            })
    return entries


def build_entries(args, aliases, resolver):
    taken, entries, forwards = set(), [], []
    addresses = tailnet_addresses() if args.resolve_tailnet else {}
    next_port = args.relay_port_base
    for alias in aliases:
        settings = resolver(alias)
        if settings is None:
            continue
        hostname = settings.get("hostname") or alias
        if args.tailnet and not hostname.lower().endswith(args.tailnet.lower()):
            continue
        if settings.get("proxyjump", "none").lower() not in ("", "none"):
            warn(f"{alias}: ProxyJump is not something the device can do; "
                 "give it a relay instead")
        address = hostname
        if args.resolve_tailnet and is_tailnet_name(hostname):
            resolved = addresses.get(hostname.lower()) or \
                addresses.get(hostname.split(".", 1)[0].lower())
            if resolved:
                address = resolved
            else:
                warn(f"{alias}: {hostname} is not in this machine's tailnet status")
        try:
            port = int(settings.get("port") or 22)
        except ValueError:
            port = 22
        if not 1 <= port <= 65535:
            port = 22
        identity = args.key or pick_identity(settings.get("identityfile"))
        if not identity:
            warn(f"{alias}: no key file found on this machine; set one with --key")
        entry = {
            "alias": sanitise_alias(alias, taken),
            "kind": args.kind,
            "address": printable(address, ADDRESS_MAX),
            "port": port,
            "user": printable(settings.get("user") or "", USER_MAX),
            "key": identity,
            "relay": "",
            "relay_port": 0,
        }
        if args.relay:
            if hostname.startswith("127.") or hostname in ("localhost", "::1"):
                warn(f"{alias}: {hostname} is this machine's own loopback, so the "
                     "relay has to be this machine for that forward to mean anything")
            entry["relay"] = printable(args.relay, ADDRESS_MAX)
            entry["relay_port"] = next_port
            jump = settings.get("proxyjump", "none")
            forwards.append((next_port, hostname, port, None if jump in ("", "none") else jump))
            next_port += 1
        entries.append(entry)
    return entries, forwards


def main():
    parser = argparse.ArgumentParser(
        description="Write the PicoCalc's hosts.cfg from this machine's SSH aliases.")
    parser.add_argument("--config", default="~/.ssh/config", help="ssh config to read")
    parser.add_argument("--out", help="file to write (default: stdout)")
    parser.add_argument("--private", action="store_true",
                        help=f"write the host list into {private_dir()}, outside this repository")
    parser.add_argument("--allow-in-repo", action="store_true",
                        help="permit writing inside a git work tree (off by default)")
    parser.add_argument("--merge", action="store_true",
                        help="keep records already in --out, replacing ones with the same name")
    parser.add_argument("--kind", choices=("ssh", "telnet"), default="ssh")
    parser.add_argument("--only", action="append", default=[],
                        help="import only aliases matching this glob (repeatable)")
    parser.add_argument("--tailnet", nargs="?", const=".ts.net",
                        help="import only hosts whose name ends with this suffix")
    parser.add_argument("--resolve-tailnet", action="store_true",
                        help="write the 100.x address instead of the MagicDNS name")
    parser.add_argument("--relay", help="LAN machine that forwards to these hosts")
    parser.add_argument("--relay-port-base", type=int, default=2200,
                        help="first port to assign on the relay (default 2200)")
    parser.add_argument("--print-forwards", action="store_true",
                        help="print the forwarding commands to run on the relay")
    parser.add_argument("--key", help="SSH key name to attach to every record")
    parser.add_argument("--copy-keys", metavar="DIR", nargs="?", const="",
                        help="copy each key pair into DIR, such as the card's /PIM/ssh; "
                             f"with no DIR it uses {os.path.join(private_dir(), 'ssh')}")
    parser.add_argument("--no-ssh-g", action="store_true",
                        help="parse the config directly instead of asking ssh -G")
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if args.private:
        root = make_private(private_dir())
        args.out = args.out or os.path.join(root, "hosts.cfg")
    if args.copy_keys == "":          # --copy-keys with no path means the private folder
        args.copy_keys = make_private(os.path.join(private_dir(), "ssh"))
    if args.out and refuse_if_tracked(args.out, "a host list", args.allow_in_repo):
        return 2
    if args.copy_keys and refuse_if_tracked(args.copy_keys, "private keys", args.allow_in_repo):
        return 2

    text = read_config_text(args.config)
    if not text:
        warn(f"{args.config} is empty or missing")
        return 1
    aliases = aliases_from_config(text)
    if args.only:
        import fnmatch
        aliases = [a for a in aliases
                   if any(fnmatch.fnmatch(a, p) for p in args.only)]
    if not aliases:
        warn("no usable Host entries found (wildcard-only patterns are skipped)")
        return 1

    if args.no_ssh_g or shutil.which("ssh") is None:
        blocks = parse_blocks(text)
        resolver = lambda alias: resolve_from_blocks(alias, blocks)
    else:
        resolver = resolve_with_ssh

    entries, forwards = build_entries(args, aliases, resolver)
    if not entries:
        warn("nothing matched")
        return 1

    if args.merge and args.out:
        merged = {e["alias"].lower(): e for e in load_existing(args.out)}
        for entry in entries:
            merged[entry["alias"].lower()] = entry
        entries = list(merged.values())
    if len(entries) > 12:
        warn(f"{len(entries)} records, but the device keeps 12; the rest will be dropped")

    body = "".join(record(e) + "\n" for e in entries)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(body)
        try:
            os.chmod(args.out, 0o600)    # host names and logins are nobody else's business
        except OSError:
            pass
        print(f"wrote {len(entries)} records to {args.out}")
    else:
        sys.stdout.write(body)

    if args.copy_keys:
        copy_keys(args, entries)
    if args.print_forwards and forwards:
        print("\n# Run these on " + (args.relay or "the relay") +
              ", one per host (ssh -L needs -N to just forward):", file=sys.stderr)
        for port, host, target, jump in forwards:
            # A host reached through a jump keeps that jump: the forward's far
            # end is resolved on the jump host, which is what ProxyJump did.
            through = jump if jump else "localhost"
            print(f"ssh -N -L 0.0.0.0:{port}:{host}:{target} {through}", file=sys.stderr)
    return 0


def copy_keys(args, entries):
    make_private(args.copy_keys)
    wanted = {e["key"] for e in entries if e["key"]}
    for name in sorted(wanted):
        for source in (os.path.expanduser(f"~/.ssh/{name}"), os.path.expanduser(f"~/.ssh/{name}.pub")):
            if not os.path.isfile(source):
                warn(f"{source} not found; copy the key onto the card by hand")
                continue
            target = os.path.join(args.copy_keys, os.path.basename(source))
            shutil.copyfile(source, target)
            os.chmod(target, 0o600 if not source.endswith(".pub") else 0o644)
            print(f"copied {os.path.basename(source)} to {args.copy_keys}")


# -------------------------------------------------------------- self test

SAMPLE = """\
Host *
    ServerAliveInterval 30

Host nas
    HostName nas.tail1a2b.ts.net
    User admin
    IdentityFile ~/.ssh/pim_import_fixture_key

Host A-Very-Long-Alias-Name-Indeed build.box
    HostName build.example.org
    Port 2222
    User Developer
    ProxyJump jump.example.org

Host local-bbs
    HostName bbs.example.org
    Port 23
"""


def self_test():
    failures = []

    def check(condition, label):
        if not condition:
            failures.append(label)

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "config")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(SAMPLE)
        text = read_config_text(path)
        aliases = aliases_from_config(text)
        check(aliases == ["nas", "A-Very-Long-Alias-Name-Indeed", "build.box", "local-bbs"],
              f"aliases: {aliases}")

        blocks = parse_blocks(text)
        args = argparse.Namespace(kind="ssh", only=[], tailnet=None, resolve_tailnet=False,
                                  relay=None, relay_port_base=2200, key=None)
        entries, forwards = build_entries(args, aliases, lambda a: resolve_from_blocks(a, blocks))
        by_alias = {e["alias"]: e for e in entries}
        check(len(entries) == 4, f"count: {len(entries)}")
        check(by_alias["nas"]["address"] == "nas.tail1a2b.ts.net", "nas address")
        check(by_alias["nas"]["user"] == "admin", "nas user")
        check(by_alias["nas"]["key"] == "",
              f"a key that is not on this machine is not recorded: {by_alias['nas']['key']}")
        check(by_alias["nas"]["port"] == 22, "nas default port")
        check("a-very-long-alias-na" in by_alias,
              f"long alias clipped to 20: {sorted(by_alias)}")
        check(all(len(a) <= ALIAS_MAX for a in by_alias), "every alias within the limit")
        check(by_alias["build-box"]["port"] == 2222, "build port")
        check(by_alias["build-box"]["user"] == "Developer", "build user")
        check(by_alias["local-bbs"]["port"] == 23, "bbs port")
        check(forwards == [], "no forwards without a relay")

        # a relay assigns one local port per host and keeps the real name
        args.relay, args.relay_port_base = "192.168.1.9", 2200
        entries, forwards = build_entries(args, aliases, lambda a: resolve_from_blocks(a, blocks))
        check([f[0] for f in forwards] == [2200, 2201, 2202, 2203], f"ports: {forwards}")
        check(entries[0]["relay"] == "192.168.1.9" and entries[0]["relay_port"] == 2200,
              "relay fields")
        check(entries[0]["address"] == "nas.tail1a2b.ts.net", "relay keeps the real name")

        # the record is exactly what the device parses: eight tab-separated fields
        line = record(entries[0])
        check(line.startswith("host=") and line.count("\t") == 7, f"record shape: {line}")
        check("\n" not in line, "one line per record")

        # merging keeps what is already on the card
        out = os.path.join(directory, "hosts.cfg")
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("host=keepme\ttelnet\tkeep.example\t23\t\t\t\t0\n")
            handle.write("# a comment\n")
        existing = load_existing(out)
        check(len(existing) == 1 and existing[0]["alias"] == "keepme", "existing loaded")

        # the key recorded is the first one that exists, not the first ssh lists
        real = os.path.join(directory, "id_ed25519")
        Path(real).write_text("x")
        check(pick_identity([os.path.join(directory, "id_rsa"), real]) == "id_ed25519",
              "picks the key that exists")
        check(pick_identity([os.path.join(directory, "nothing_here")]) == "", "none exist")
        check(pick_identity([]) == "", "nothing offered")
        Path(real + ".pub").write_text("x")
        os.remove(real)
        check(pick_identity([real]) == "id_ed25519", "a lone public half still counts")

        # nothing may be written inside a git work tree without being asked twice
        tree = os.path.join(directory, "repo")
        os.makedirs(os.path.join(tree, ".git"))
        os.makedirs(os.path.join(tree, "sub"))
        check(git_tree_above(os.path.join(tree, "sub")) == tree, "finds the work tree above")
        check(git_tree_above(directory) is None, "plain directory is not a work tree")
        check(refuse_if_tracked(os.path.join(tree, "sub", "hosts.cfg"), "x", False),
              "refuses a path inside the tree")
        check(not refuse_if_tracked(os.path.join(tree, "sub", "hosts.cfg"), "x", True),
              "--allow-in-repo overrides it")
        check(not refuse_if_tracked(os.path.join(directory, "hosts.cfg"), "x", False),
              "allows a path outside any tree")

        # the private folder is private, and overridable for a synced home
        os.environ["PICODECK_PRIVATE_DIR"] = os.path.join(directory, "elsewhere")
        check(private_dir() == os.path.join(directory, "elsewhere"), "override honoured")
        made = make_private(private_dir())
        check(oct(os.stat(made).st_mode & 0o777) == "0o700", "private folder is 0700")
        del os.environ["PICODECK_PRIVATE_DIR"]

        # a name that is not a file name must not become a key name
        check(sanitise_key("~/.ssh/../../etc/passwd") == "passwd", "key name is a bare name")
        check(sanitise_key("~/.ssh/id_ed25519.pub") == "id_ed25519", "pub suffix dropped")
        check(sanitise_key("") == "", "no key")
        check(printable("tab\there", 64) == "tabhere", "tabs cannot reach the file")

    if failures:
        for failure in failures:
            print(f"FAIL {failure}", file=sys.stderr)
        return 1
    print("pim-hosts-import self test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
