import struct
import zlib

EXPECTED_PKI_SHA1 = "09b7cc6479dba96dbc8090f65a0f5720de605329"
EXPECTED_ENTRY_COUNT = 30893

def read_c_string(table, offset):
    if offset >= len(table):
        raise ValueError(f"string offset outside table: {offset}")
    end = table.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"unterminated string at offset: {offset}")
    return table[offset:end].decode("utf-8")


def parse_index(path):
    encoded = path.read_bytes()
    if len(encoded) < 25:
        raise ValueError("PKI is shorter than the native header")
    version, index_type = struct.unpack_from("<II", encoded, 0)
    if version != 3 or index_type != 1:
        raise ValueError(f"unsupported PKI identity version={version} indexType={index_type}")
    data_version = encoded[8:24]
    decoded = zlib.decompress(encoded[24:])
    if len(decoded) < 88:
        raise ValueError("PKI decoded stream is truncated")
    counts = struct.unpack_from("<21I", decoded, 0)
    entry_count = sum(counts)
    if entry_count != EXPECTED_ENTRY_COUNT:
        raise ValueError(f"PKI entry count mismatch expected={EXPECTED_ENTRY_COUNT} actual={entry_count}")
    records_offset = 84
    records_size = entry_count * 44
    string_size_offset = records_offset + records_size
    if string_size_offset + 4 > len(decoded):
        raise ValueError("PKI file records are truncated")
    string_size = struct.unpack_from("<I", decoded, string_size_offset)[0]
    expected_length = string_size_offset + 4 + string_size
    if expected_length != len(decoded):
        raise ValueError(f"PKI decoded length mismatch expected={expected_length} actual={len(decoded)}")
    string_table = decoded[string_size_offset + 4:]
    type_code = 0
    type_ordinal = 0
    entries = []
    for entry_index in range(entry_count):
        while type_code < len(counts) and type_ordinal >= counts[type_code]:
            type_code += 1
            type_ordinal = 0
        offset = records_offset + entry_index * 44
        words = struct.unpack_from("<11I", decoded, offset)
        entries.append(
            {
                "entry_id": entry_index + 1,
                "entry_index": entry_index,
                "type_code": type_code,
                "type_ordinal": type_ordinal,
                "name": read_c_string(string_table, words[0]),
                "name_offset": words[0],
                "package_crc": words[1],
                "stored_size": words[2],
                "package_offset": words[3],
                "decoded_size": words[4],
                "flags": words[5],
                "native_words": struct.pack("<5I", *words[6:]),
            }
        )
        type_ordinal += 1
    return data_version, counts, entries
