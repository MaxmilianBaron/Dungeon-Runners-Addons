import json
import sys
from pathlib import Path


def generate(source):
    profile = json.loads((source / 'client_compatibility.json').read_text(encoding='utf-8'))
    assert profile['schema'] == 1 and 64 <= profile['minimumSize'] <= profile['maximumSize'] <= 33554432
    assert 1 <= len(profile['ranges']) <= 512
    end = 0
    rows = []
    for item in profile['ranges']:
        offset, length, hashes = item['offset'], item['length'], item['sha256']
        assert type(offset) is int and type(length) is int and end <= offset and 0 < length <= 262144
        assert offset + length <= profile['minimumSize']
        assert 1 <= len(hashes) <= 4 and all(len(h) == 64 and set(h) <= set('0123456789abcdef') for h in hashes)
        rows.append('    {' + str(offset) + ',' + str(length) + ',{' + ','.join('"' + h + '"' for h in hashes) + '}}')
        end = offset + length
    return '#pragma once\n\nstruct ClientProtectedRange { unsigned offset,length; const char* hashes[4]; };\n' + \
        'inline constexpr unsigned ClientMinimumSize = ' + str(profile['minimumSize']) + ';\n' + \
        'inline constexpr unsigned ClientMaximumSize = ' + str(profile['maximumSize']) + ';\n' + \
        'inline constexpr ClientProtectedRange ClientProtectedRanges[] = {\n' + ',\n'.join(rows) + '\n};\n'


if __name__ == '__main__':
    directory = Path(__file__).resolve().parent.parent / 'Source'
    generated = generate(directory)
    output = directory / 'client_compatibility.generated.h'
    if '--output' in sys.argv:
        output = Path(sys.argv[sys.argv.index('--output') + 1]) / output.name
    if '--check' in sys.argv:
        if output.read_text(encoding='utf-8') != generated:
            raise SystemExit('Regenerate the client compatibility header.')
    else:
        with output.open('w', encoding='utf-8', newline='\n') as stream:
            stream.write(generated)
