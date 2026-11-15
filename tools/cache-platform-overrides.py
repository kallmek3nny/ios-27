#!/usr/bin/env python3
"""Bounded v4 dyld-cache pointer override planning for original platform code.

Layout source: apple-oss-distributions/dyld fd8d0c4d, common/CachePatching.h.
This reads metadata and existing pointers. It never writes or executes code.
"""

from struct import Struct


PLATFORM = "/usr/lib/system/libsystem_platform.dylib"
HEADER_BYTES = 152
MAX_POOL = 128 * 1024 * 1024
MAX_PATCHES = 262144


def require(condition, message):
    if not condition:
        raise ValueError(message)


class PatchTable:
    def __init__(self, cache):
        self.cache = cache
        self.base, self.size = cache.patch_span
        require(HEADER_BYTES <= self.size <= MAX_POOL, "unsupported cache patch table extent")
        header = self.read(self.base, "II18Q")
        require(header[:2] == (4, 0), "platform overrides require v4 cache patch locations")
        names = ("images", "exports", "clients", "client_exports", "locations", "names",
                 "got_clients", "got_exports", "got_locations")
        widths = (16, 8, 12, 12, 8, 1, 8, 12, 16)
        self.tables = {}
        spans = [(self.base, self.base + HEADER_BYTES)]
        for i, (name, width) in enumerate(zip(names, widths)):
            address, count = header[2 + 2 * i:4 + 2 * i]
            self.tables[name] = (address, count, width)
            if count:
                self.bounds(address, count * width)
                require(width == 1 or address % 4 == 0, "unaligned cache patch array")
                spans.append((address, address + count * width))
        spans.sort()
        require(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), "overlapping cache patch arrays")
        require(self.tables["images"][1] == len(cache.image_addresses)
                and self.tables["got_clients"][1] == len(cache.image_addresses),
                "cache patch image count mismatch")

    def bounds(self, address, size):
        require(address >= self.base and address - self.base <= self.size
                and 0 <= size <= self.size - (address - self.base), "cache patch metadata outside pool")

    def read(self, address, fmt):
        layout = Struct("<" + fmt)
        self.bounds(address, layout.size)
        data, offset = self.cache.locate(address, layout.size)
        return layout.unpack_from(data, offset)

    def item(self, table, index, fmt):
        address, count, width = self.tables[table]
        require(0 <= index < count and Struct("<" + fmt).size == width,
                "cache patch index outside array")
        return self.read(address + index * width, fmt)

    def indices(self, table, start, count):
        limit = self.tables[table][1]
        require(0 <= start <= limit and 0 <= count <= limit - start,
                "cache patch slice outside array")
        return range(start, start + count)

    def export_name(self, offset):
        start, size, _ = self.tables["names"]
        require(offset < size, "cache patch export name outside pool")
        length = min(4096, size - offset)
        data, at = self.cache.locate(start + offset, length)
        raw = data[at:at + length]
        end = raw.find(b"\0")
        require(end > 0, "invalid cache patch export name")
        return raw[:end].decode("ascii")


def plan(cache, targets):
    """targets maps exact old platform addresses to verified replacement PCs.

    Only unauthenticated, untagged, zero-addend pointer slots are supported.
    The current slot must still contain the old export address. All output is
    staged until every selected client/GOT reference has been validated.
    """
    require(targets, "no cache platform replacement targets")
    require(PLATFORM in cache.paths, "platform library absent from cache")
    base = cache.paths[PLATFORM]
    indices = [i for i, address in enumerate(cache.image_addresses) if address == base]
    require(len(indices) == 1, "ambiguous platform cache image index")
    image_index = indices[0]
    image = cache.image(base)
    for old, new in targets.items():
        require(0 < new < 1 << 64 and new % 4 == 0, "invalid platform replacement address")
        require(image.export_is_host_placeholder(0, old - base), "target is not a platform placeholder")
    table = PatchTable(cache)
    client_start, client_count, export_start, export_count = table.item("images", image_index, "4I")
    export_indices = table.indices("exports", export_start, export_count)
    selected = {}
    for index in export_indices:
        offset, flags_name = table.item("exports", index, "2I")
        old = base + offset
        if old not in targets:
            continue
        require(flags_name >> 28 == 0, "non-regular platform cache export")
        selected[index] = (old, targets[old], table.export_name(flags_name & 0xFFFFFFF))
    require({old for old, _, _ in selected.values()} == set(targets),
            "platform export absent from cache patch table")
    patches = {}

    def location(address, metadata, export, kind, padding=0):
        require(metadata & ~0x100 == 0 and padding == 0,
                "authenticated, tagged or interior cache pointer is unsupported")
        require(address % 8 == 0, "unaligned cache pointer location")
        require(address + 8 <= table.base or address >= table.base + table.size,
                "cache pointer override overlaps patch metadata")
        require(any(m.address <= address and address + 8 <= m.address + m.size
                    and m.protection in (1, 3) for m in cache.mappings),
                "cache override location is not non-executable data")
        old, new, symbol = export
        data, at = cache.locate(address, 8)
        require(Struct("<Q").unpack_from(data, at)[0] == old, "cache pointer differs from its declared export")
        patch = {"address": address, "original": old, "target": new, "symbol": symbol, "kind": kind}
        require(address not in patches or patches[address]["target"] == new, "conflicting cache pointer overrides")
        patches[address] = patch
        require(len(patches) <= MAX_PATCHES, "too many cache platform pointer overrides")

    for client in table.indices("clients", client_start, client_count):
        client_index, first, count = table.item("clients", client, "3I")
        require(client_index < len(cache.image_addresses), "invalid cache patch client image")
        client_base = cache.image_addresses[client_index]
        for export in table.indices("client_exports", first, count):
            index, loc_first, loc_count = table.item("client_exports", export, "3I")
            require(index in export_indices, "cache client export belongs to another image")
            if index not in selected:
                continue
            client_image = cache.image(client_base)
            for loc in table.indices("locations", loc_first, loc_count):
                offset, flags = table.item("locations", loc, "2I")
                address = client_base + offset
                require(any(start <= address and address + 8 <= start + size and protection in (1, 3)
                            for start, size, _, _, protection in client_image.segments.values()),
                        "cache patch is outside client data segments")
                location(address, flags, selected[index], "client")
    first, count = table.item("got_clients", image_index, "2I")
    for export in table.indices("got_exports", first, count):
        index, loc_first, loc_count = table.item("got_exports", export, "3I")
        require(index in export_indices, "cache GOT export belongs to another image")
        if index not in selected:
            continue
        for loc in table.indices("got_locations", loc_first, loc_count):
            offset, flags, padding = table.item("got_locations", loc, "Q2I")
            location(cache.region[0] + offset, flags, selected[index], "got", padding)
    return sorted(patches.values(), key=lambda row: row["address"])
