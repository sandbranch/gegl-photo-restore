#!/usr/bin/env python3
# Downloads the test photos listed in manifest.json from Wikimedia
# Commons into this folder, as 2560 pixel wide renditions (the one marked
# "full_size" as the original, for the timing at 24 megapixels), and checks each
# against the SHA-256 in the manifest. All are CC0 or public domain: the
# licence was read on each file's Commons page (manifest.json names the
# page, author, licence and the licence template found there). They stay
# out of git to keep the repository small; this script makes the same
# set anywhere.
#
#   tests/images/fetch.py           fetch what is missing, check all
#   tests/images/fetch.py --update  write the SHA-256 of files that have
#                                   none in the manifest yet
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import os
import sys
import subprocess
import time
import urllib.parse

here = os.path.dirname(os.path.abspath(__file__))
manifest_path = os.path.join(here, 'manifest.json')
# a generic client name, no contact details
UA = 'photo-test-fetch/0.1 (test image fetcher)'


def get(url, timeout=300):
    """the body of url, through curl (Wikimedia's media servers answer
    Python's own HTTP client with 429, curl with the same User-Agent
    works); waits and tries again when asked to slow down"""
    for attempt in range(10):
        r = subprocess.run(['curl', '-sS', '-L', '--max-time', str(timeout), '-A', UA,
                            '-w', '\n%{http_code}', url], capture_output=True, check=True)
        body, _, code = r.stdout.rpartition(b'\n')
        if code == b'200':
            return body
        if code not in (b'429', b'503') or attempt == 9:
            sys.exit('%s: HTTP %s' % (url, code.decode()))
        print('  Commons asks to wait, %d s' % (60 * (attempt + 1)))
        time.sleep(60 * (attempt + 1))


def file_url(photo):
    """the original (with "full_size") or Commons' 2560 pixel wide rendition
    (Commons serves standard thumbnail widths; the originals of large
    files are rate limited for clients without contact details)"""
    params = dict(action='query', format='json', titles=photo['title'], prop='imageinfo', iiprop='url')
    if not photo.get('full_size'):
        params['iiurlwidth'] = 2560
    data = json.loads(get('https://commons.wikimedia.org/w/api.php?' + urllib.parse.urlencode(params), 60))
    info = next(iter(data['query']['pages'].values()))['imageinfo'][0]
    return info.get('thumburl', info['url'])


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    update = '--update' in sys.argv
    photos = json.load(open(manifest_path))
    bad = 0
    for photo in photos:
        path = os.path.join(here, photo['file'])
        if not os.path.exists(path):
            data = get(file_url(photo))
            with open(path + '.part', 'wb') as f:
                f.write(data)
            os.rename(path + '.part', path)
            print('%-22s %6d kB  %s (%s)' % (photo['file'], len(data) // 1024,
                                             photo['author'][:40], photo['license']))
            time.sleep(2)
        digest = sha256(path)
        if not photo.get('sha256'):
            if update:
                photo['sha256'] = digest
            else:
                print('%-22s no SHA-256 in the manifest (run with --update)' % photo['file'])
                bad += 1
        elif digest != photo['sha256']:
            print('%-22s SHA-256 differs from the manifest: %s' % (photo['file'], digest))
            bad += 1
    if update:
        with open(manifest_path, 'w') as f:
            json.dump(photos, f, indent=1, ensure_ascii=False)
            f.write('\n')
    print('%d photos, %s' % (len(photos), 'all checked' if not bad else '%d not right' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
