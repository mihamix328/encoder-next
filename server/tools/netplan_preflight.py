"""Conservative, read-only scope check before ISOLATED Netplan staging.

Linux + PyYAML required. No commands, configuration writes or service access.
This is NOT authorization to apply settings: migration, WPA2 enforcement,
Ethernet reachability, journal/watchdog and candidate validation remain required.
The root directory is trusted operator input, never supplied by a TLS client.
"""
import argparse
import os
import re
import stat
from pathlib import Path

import yaml

MANAGED = "90-encoder-wifi.yaml"
MARKER = b"# encoder managed wifi v1\n"


class Refused(Exception):
    """Fixed, credential-free reason suitable for a diagnostic message."""


class BoundedLoader(yaml.BaseLoader):
    def __init__(self, stream):
        super().__init__(stream)
        self.depth = 0
        self.nodes = 0

    def compose_node(self, parent, index):
        if self.check_event(yaml.AliasEvent):
            raise Refused("YAML aliases are unsupported for automatic migration")
        self.depth += 1
        self.nodes += 1
        try:
            if self.depth > 20 or self.nodes > 2048:
                raise Refused("YAML structure exceeds the supported limits")
            node = super().compose_node(parent, index)
            if node.tag not in {"tag:yaml.org,2002:str", "tag:yaml.org,2002:seq", "tag:yaml.org,2002:map"}:
                raise Refused("Explicit YAML types are unsupported")
            return node
        finally:
            self.depth -= 1

    def construct_mapping(self, node, deep=False):
        result = {}
        for key_node, value_node in node.value:
            if not isinstance(key_node, yaml.ScalarNode):
                raise Refused("Complex YAML keys are unsupported")
            key = self.construct_object(key_node, deep=deep)
            if key == "<<":
                raise Refused("YAML merge keys require manual review")
            if key in result:
                raise Refused("Duplicate YAML keys require manual review")
            result[key] = self.construct_object(value_node, deep=deep)
        return result


def parse(data):
    try:
        document = yaml.load(data, Loader=BoundedLoader)
    except (yaml.YAMLError, UnicodeError, RecursionError):
        # Parser exceptions include source lines, potentially containing keys.
        raise Refused("Invalid Netplan YAML") from None
    if not isinstance(document, dict) or set(document) != {"network"}:
        raise Refused("Unsupported Netplan document structure")
    network = document["network"]
    if not isinstance(network, dict) or network.get("version") != "2":
        raise Refused("Unsupported Netplan version or network structure")
    return network


def validate_scope(data, managed):
    network = parse(data)
    if managed:
        if not data.startswith(MARKER) or set(network) != {"version", "wifis"}:
            raise Refused("Managed Wi-Fi file has unexpected ownership marker or scope")
        wifis = network["wifis"]
        if not isinstance(wifis, dict) or set(wifis) != {"wlan0"}:
            raise Refused("Managed Wi-Fi file must describe only wlan0")
        wifi = wifis["wlan0"]
        if not isinstance(wifi, dict) or set(wifi) != {"renderer", "dhcp4", "access-points"} or wifi["renderer"] != "networkd" or wifi["dhcp4"] != "true":
            raise Refused("Managed wlan0 settings are not a supported generated profile")
        points = wifi["access-points"]
        if not isinstance(points, dict) or len(points) != 1:
            raise Refused("Managed Wi-Fi profile must contain exactly one access point")
        name, point = next(iter(points.items()))
        if not isinstance(name, str):
            raise Refused("Managed Wi-Fi profile has an unsupported network name")
        try:
            valid_name = 1 <= len(name.encode("utf-8")) <= 32
        except UnicodeError:
            valid_name = False
        for character in name:
            codepoint = ord(character)
            if codepoint < 32 or 0x7f <= codepoint <= 0x9f or codepoint in (0x2028, 0x2029) or 0xfdd0 <= codepoint <= 0xfdef or (codepoint & 0xffff) >= 0xfffe:
                valid_name = False
        if not valid_name:
            raise Refused("Managed Wi-Fi profile has an unsupported network name")
        if not isinstance(point, dict) or set(point) != {"auth"}:
            raise Refused("Managed access point contains unexpected settings")
        auth = point["auth"]
        if not isinstance(auth, dict) or set(auth) != {"key-management", "password"} or auth["key-management"] != "psk":
            raise Refused("Managed Wi-Fi authentication settings are unsupported")
        if not isinstance(auth["password"], str) or not re.fullmatch(r"[0-9a-f]{64}", auth["password"]):
            raise Refused("Managed Wi-Fi key has an unsupported representation")
        return
    if set(network) - {"version", "renderer", "ethernets", "wifis"}:
        raise Refused("Additional network device types require manual scope review")
    if network.get("renderer", "networkd") != "networkd":
        raise Refused("Non-networkd renderer requires manual migration")
    if "wifis" in network and network["wifis"] != {}:
        raise Refused("Existing unmanaged Wi-Fi configuration requires explicit migration")
    ethernets = network.get("ethernets", {})
    if not isinstance(ethernets, dict) or set(ethernets) - {"end1"}:
        raise Refused("Only the fixed end1 recovery interface is supported automatically")
    for settings in ethernets.values():
        if not isinstance(settings, dict) or "match" in settings or "set-name" in settings:
            raise Refused("Interface matching or renaming requires manual review")
        if settings.get("renderer", "networkd") != "networkd":
            raise Refused("Recovery Ethernet renderer is unsupported")


def preflight(root):
    count = 0
    total = 0
    for relative in ("lib/netplan", "etc/netplan", "run/netplan"):
        try:
            directory = os.open(root / relative, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
        except FileNotFoundError:
            continue
        except OSError:
            raise Refused("Cannot safely open a Netplan configuration directory") from None
        try:
            info = os.fstat(directory)
            if info.st_uid != os.geteuid() or info.st_mode & 0o022:
                raise Refused("Netplan directory ownership or permissions are unsafe")
            # Conservative: also inspect shadowed files, rather than silently
            # making ownership decisions using filename precedence alone.
            for name in sorted(os.listdir(directory)):
                if name.startswith(".") or not name.endswith(".yaml"):
                    continue
                count += 1
                if count > 32:
                    raise Refused("Too many Netplan configuration files")
                managed = name == MANAGED
                if managed and relative != "etc/netplan":
                    raise Refused("Managed filename is shadowed outside etc/netplan")
                try:
                    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=directory)
                except OSError:
                    raise Refused("Cannot safely open a Netplan configuration file") from None
                try:
                    info = os.fstat(fd)
                    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.geteuid() or info.st_mode & 0o022:
                        raise Refused("Netplan file ownership, type or permissions are unsafe")
                    if managed and info.st_mode & 0o077:
                        raise Refused("Managed Wi-Fi credentials must have private permissions")
                    if not 0 < info.st_size <= 65536:
                        raise Refused("Netplan file size is outside the supported bounds")
                    total += info.st_size
                    if total > 262144:
                        raise Refused("Netplan configuration total size exceeds the limit")
                    with os.fdopen(fd, "rb", closefd=False) as stream:
                        data = stream.read(65537)
                    after = os.fstat(fd)
                    if len(data) != info.st_size or (after.st_mtime_ns, after.st_size) != (info.st_mtime_ns, info.st_size):
                        raise Refused("Netplan configuration changed while reading")
                    validate_scope(data, managed)
                finally:
                    os.close(fd)
        finally:
            os.close(directory)
    return count


def main():
    parser = argparse.ArgumentParser(description="Read-only Netplan scope preflight; does not apply networking")
    parser.add_argument("--root-dir", type=Path, default=Path("/"))
    args = parser.parse_args()
    try:
        preflight(args.root_dir)
    except Refused as error:
        print("REFUSED: " + str(error))
        return 1
    except (OSError, ValueError, UnicodeError):
        print("REFUSED: configuration could not be inspected safely")
        return 1
    print("OK: scope permits isolated staging only; applying networking remains disabled")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
