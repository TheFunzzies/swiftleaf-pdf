"""Local RFC 3161/CMS interoperability check. Requires OpenSSL and a debug exe."""
import argparse
import http.server
import os
from pathlib import Path
import subprocess
import tempfile
import threading


def tlv(data, offset=0):
    tag, size = data[offset:offset + 2]
    start = offset + 2
    if size & 128:
        count = size & 127
        size = int.from_bytes(data[start:start + count], 'big')
        start += count
    return tag, start, start + size


def children(data, offset=0):
    _, start, end = tlv(data, offset)
    while start < end:
        yield start
        start = tlv(data, start)[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--openssl', default='openssl')
    parser.add_argument('--exe', default='out/dbg64/Swiftleaf.exe')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='swiftleaf-tsa-') as tmp:
        root = Path(tmp)

        def ssl(*parts, **kwargs):
            return subprocess.run([args.openssl, *parts], cwd=root, check=True, **kwargs)

        ssl('req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '2',
            '-subj', '/CN=SwiftLeaf test TSA', '-addext', 'extendedKeyUsage=critical,timeStamping',
            '-addext', 'keyUsage=critical,digitalSignature', '-keyout', 'tsa.key', '-out', 'tsa.pem')
        (root / 'serial').write_text('01\n')
        (root / 'tsa.cnf').write_text('''[tsa]
default_tsa = local
[local]
serial = serial
signer_cert = tsa.pem
signer_key = tsa.key
signer_digest = sha256
default_policy = 1.2.3.4.1
digests = sha256
accuracy = secs:1
ordering = yes
tsa_name = yes
ess_cert_id_chain = no
''')

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                (root / 'request.der').write_bytes(self.rfile.read(int(self.headers['Content-Length'])))
                ssl('ts', '-reply', '-config', 'tsa.cnf', '-queryfile', 'request.der', '-out', 'response.der')
                response = (root / 'response.der').read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', 'application/timestamp-reply')
                self.send_header('Content-Length', str(len(response)))
                self.end_headers()
                self.wfile.write(response)

        server = http.server.HTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        env = dict(os.environ, SWIFTLEAF_SIGNATURE_TEST_ONLY='1',
                   SWIFTLEAF_TEST_TSA_URL=f'http://127.0.0.1:{server.server_port}/')
        try:
            subprocess.run([str((repo / args.exe).resolve()), '-unit-tests', '-for-ai', '-for-testing'],
                           cwd=repo, env=env, check=True, timeout=120)
        finally:
            server.shutdown()
        cms_path = repo / 'out/signature-test.der'
        data = cms_path.read_bytes()
        (root / 'content.bin').write_bytes(b'test')
        ssl('cms', '-verify', '-binary', '-inform', 'DER', '-in', str(cms_path), '-content', 'content.bin',
            '-noverify', '-out', 'verified.bin')
        explicit = list(children(data))[1]
        signed_data = next(children(data, explicit))
        signer_set = list(children(data, signed_data))[-1]
        signer = next(children(data, signer_set))
        parts = list(children(data, signer))
        signature = next(p for p in parts[3:] if data[p] == 4)
        _, start, end = tlv(data, signature)
        (root / 'signature.bin').write_bytes(data[start:end])
        unsigned = next(p for p in parts if data[p] == 0xa1)
        attr = next(children(data, unsigned))
        values = list(children(data, attr))[1]
        token = next(children(data, values))
        (root / 'token.der').write_bytes(data[token:tlv(data, token)[2]])
        ssl('ts', '-verify', '-token_in', '-in', 'token.der', '-data', 'signature.bin', '-CAfile', 'tsa.pem')
        (root / 'signature.bin').write_bytes(b'wrong signature')
        result = subprocess.run([args.openssl, 'ts', '-verify', '-token_in', '-in', 'token.der',
                                 '-data', 'signature.bin', '-CAfile', 'tsa.pem'], cwd=root)
        assert result.returncode != 0, 'timestamp accepted a different signature'
        print('PASS: CMS signature, embedded timestamp imprint, mismatched imprint rejection')


if __name__ == '__main__':
    main()
