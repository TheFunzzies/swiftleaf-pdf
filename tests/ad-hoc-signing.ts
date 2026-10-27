// Local RFC 3161 interoperability and signature-policy regression checks.
// Run: bun tests/ad-hoc-signing.ts [--no-build]. Needs OpenSSL, no public TSA.
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, withControlledApp } from "./control.ts";
import { EXE, runStandalone, tmpPath } from "./util.ts";

const openssl = Bun.which("openssl") ?? "C:/Program Files/Git/usr/bin/openssl.exe";
const dir = tmpPath("signing-rfc3161");
function run(...args: string[]): string {
  const r = Bun.spawnSync([openssl, ...args], { cwd: dir });
  const text = r.stdout.toString() + r.stderr.toString();
  if (r.exitCode) throw new Error(`openssl ${args.join(" ")}: ${text}`);
  return text;
}
function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(message);
}
function fixture(path: string, oldSignature = false): void {
  const objects = [
    "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [6 0 R 7 0 R 8 0 R] /SigFlags 3 >> >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Resources << >> /Contents 4 0 R /Annots [6 0 R 7 0 R 8 0 R] >>",
    "<< /Length 0 >>\nstream\n\nendstream",
    "null",
    "<< /Type /Annot /Subtype /Widget /FT /Sig /T (First) /P 3 0 R /Rect [10 10 100 40] >>",
    "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Text) /P 3 0 R /Rect [10 50 100 70] /V (before) >>",
    "<< /Type /Annot /Subtype /Widget /FT /Sig /T (Second) /P 3 0 R /Rect [10 80 100 110] >>",
  ];
  if (oldSignature) {
    objects[5] = objects[5].replace(" >>", " /V 9 0 R >>");
    objects.push(
      `<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /M (D:20200601000000Z) /ByteRange [0000000000 0000000000 0000000000 0000000000] /Contents <${"0".repeat(32768)}> >>`,
    );
  }
  let pdf = "%PDF-1.7\n";
  const offsets = [0];
  objects.forEach((object, i) => {
    offsets.push(pdf.length);
    pdf += `${i + 1} 0 obj\n${object}\nendobj\n`;
  });
  const xref = pdf.length;
  pdf += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`;
  for (const offset of offsets.slice(1)) pdf += `${String(offset).padStart(10, "0")} 00000 n \n`;
  pdf += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
  writeFileSync(path, pdf, "latin1");
}
function oldSignature(path: string): void {
  fixture(path, true);
  let pdf = readFileSync(path).toString("latin1");
  const hole = /<0{32768}>/.exec(pdf)!;
  const end = hole.index + hole[0].length;
  const ranges = [0, hole.index, end, pdf.length - end];
  pdf = pdf.replace(
    "0000000000 0000000000 0000000000 0000000000",
    ranges.map((v) => String(v).padStart(10, "0")).join(" "),
  );
  writeFileSync(join(dir, "old-content.bin"), Buffer.from(pdf.slice(0, hole.index) + pdf.slice(end), "latin1"));
  run(
    "cms",
    "-sign",
    "-binary",
    "-in",
    "old-content.bin",
    "-signer",
    "expired.pem",
    "-inkey",
    "key.pem",
    "-outform",
    "DER",
    "-out",
    "old-cms.der",
  );
  const cms = readFileSync(join(dir, "old-cms.der")).toString("hex");
  check(cms.length <= 32768, "old CMS too large");
  pdf = pdf.slice(0, hole.index + 1) + cms.padEnd(32768, "0") + pdf.slice(end - 1);
  writeFileSync(path, pdf, "latin1");
}
function update(source: string, dest: string, objects: Record<number, string>): void {
  let pdf = readFileSync(source).toString("latin1");
  const previous = [...pdf.matchAll(/startxref\s+(\d+)/g)].at(-1)![1];
  const size = Number([...pdf.matchAll(/\/Size\s+(\d+)/g)].at(-1)![1]);
  const offsets: [number, number][] = [];
  for (const [number, object] of Object.entries(objects)) {
    offsets.push([Number(number), pdf.length]);
    pdf += `${number} 0 obj\n${object}\nendobj\n`;
  }
  const xref = pdf.length;
  pdf += "xref\n";
  for (const [number, offset] of offsets) pdf += `${number} 1\n${String(offset).padStart(10, "0")} 00000 n \n`;
  pdf += `trailer\n<< /Size ${Math.max(size, ...offsets.map(([n]) => n + 1))} /Root 1 0 R /Prev ${previous} >>\nstartxref\n${xref}\n%%EOF\n`;
  writeFileSync(dest, pdf, "latin1");
}
async function validation(pdf: string): Promise<string> {
  return withControlledApp(
    EXE,
    async (client) => {
      for (let i = 0; i < 100; i++) {
        const r = await client.request(ControlCommand.TestDocumentSignatures);
        if (r[0] === 0) return String(r[1]);
        check(r[0] === 2, String(r[1]));
        await Bun.sleep(100);
      }
      throw new Error("signature validation timed out");
    },
    [pdf],
  );
}
function der(buffer: Buffer, offset = 0): { tag: number; start: number; end: number; body: number } {
  const start = offset;
  const tag = buffer[offset++];
  let length = buffer[offset++];
  if (length & 128) {
    const count = length & 127;
    check(count > 0 && count <= 4, "invalid DER length");
    length = 0;
    for (let i = 0; i < count; i++) length = length * 256 + buffer[offset++];
  }
  check(offset + length <= buffer.length, "truncated DER");
  return { tag, start, body: offset, end: offset + length };
}
function children(buffer: Buffer, node = der(buffer)): ReturnType<typeof der>[] {
  const result = [];
  for (let at = node.body; at < node.end;) {
    const next = der(buffer, at);
    result.push(next);
    at = next.end;
  }
  return result;
}
function verifyOutput(path: string): void {
  const bytes = readFileSync(path);
  const text = bytes.toString("latin1");
  const ranges = /\/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\]/.exec(text)!;
  check(ranges, "missing ByteRange");
  const [a, b, c, d] = ranges.slice(1).map(Number);
  const cmsHex = /<([0-9a-fA-F]+)>/.exec(text.slice(a + b, c))![1];
  const padded = Buffer.from(cmsHex, "hex");
  const cms = padded.subarray(0, der(padded).end);
  writeFileSync(join(dir, "cms.der"), cms);
  writeFileSync(join(dir, "content.bin"), Buffer.concat([bytes.subarray(a, a + b), bytes.subarray(c, c + d)]));
  run(
    "cms",
    "-verify",
    "-binary",
    "-inform",
    "DER",
    "-in",
    "cms.der",
    "-content",
    "content.bin",
    "-noverify",
    "-out",
    "verified.bin",
  );
  const signed = children(cms, children(cms)[1])[0];
  const signers = children(cms, children(cms, signed).at(-1)!);
  const signer = children(cms, signers[0]);
  const signature = signer.find((n) => n.tag === 4)!;
  check(signature, "missing CMS signature value");
  writeFileSync(join(dir, "signature.bin"), cms.subarray(signature.body, signature.end));
  const unsigned = signer.find((n) => n.tag === 0xa1)!;
  check(unsigned, "missing CMS unsigned timestamp attribute");
  const oid = Buffer.from("2a864886f70d010910020e", "hex");
  const attribute = children(cms, unsigned).find((n) => {
    const id = children(cms, n)[0];
    return cms.subarray(id.body, id.end).equals(oid);
  })!;
  check(attribute, "missing RFC 3161 attribute");
  const token = children(cms, children(cms, attribute)[1])[0];
  writeFileSync(join(dir, "token.der"), cms.subarray(token.start, token.end));
  run("ts", "-verify", "-token_in", "-in", "token.der", "-data", "signature.bin", "-CAfile", "tsa.pem");
}
export async function testit(): Promise<void> {
  if (!existsSync(openssl)) {
    console.log("SKIP: OpenSSL unavailable");
    return;
  }
  const { mkdirSync } = await import("node:fs");
  mkdirSync(dir, { recursive: true });
  run(
    "req",
    "-x509",
    "-newkey",
    "rsa:2048",
    "-nodes",
    "-keyout",
    "key.pem",
    "-out",
    "tsa.pem",
    "-days",
    "2",
    "-subj",
    "/CN=SwiftLeaf local TSA",
    "-addext",
    "extendedKeyUsage=critical,timeStamping",
    "-addext",
    "keyUsage=critical,digitalSignature",
    "-addext",
    "basicConstraints=critical,CA:false",
  );
  run("pkcs12", "-export", "-inkey", "key.pem", "-in", "tsa.pem", "-out", "signer.pfx", "-passout", "pass:test");
  writeFileSync(join(dir, "serial"), "01\n");
  writeFileSync(
    join(dir, "tsa.cnf"),
    `[tsa]\ndefault_tsa=local\n[local]\nserial=serial\nsigner_cert=tsa.pem\nsigner_key=key.pem\nsigner_digest=sha256\ndefault_policy=1.2.3.4.1\ndigests=sha256\naccuracy=secs:1\nordering=yes\ntsa_name=yes\ness_cert_id_chain=no\n`,
  );
  run("req", "-new", "-key", "key.pem", "-out", "expired.csr", "-subj", "/CN=Expired signer");
  writeFileSync(join(dir, "index.txt"), "");
  writeFileSync(
    join(dir, "ca.cnf"),
    "[ca]\ndefault_ca=local\n[local]\ndatabase=index.txt\nnew_certs_dir=.\nserial=serial\ncertificate=tsa.pem\nprivate_key=key.pem\ndefault_md=sha256\npolicy=names\n[names]\ncommonName=supplied\n",
  );
  run(
    "ca",
    "-batch",
    "-config",
    "ca.cnf",
    "-in",
    "expired.csr",
    "-startdate",
    "20200101000000Z",
    "-enddate",
    "20210101000000Z",
    "-out",
    "expired.pem",
  );
  writeFileSync(join(dir, "large-chain.pem"), readFileSync(join(dir, "tsa.pem"), "utf8").repeat(100));
  let mode = "good";
  const server = Bun.serve({
    hostname: "127.0.0.1",
    port: 0,
    async fetch(req) {
      if (mode === "fail") return new Response("unavailable", { status: 503 });
      writeFileSync(join(dir, "request.tsq"), Buffer.from(await req.arrayBuffer()));
      if (mode === "wrong") {
        writeFileSync(join(dir, "wrong.bin"), "wrong imprint");
        run("ts", "-query", "-data", "wrong.bin", "-sha256", "-cert", "-out", "request.tsq");
      }
      run(
        "ts",
        "-reply",
        "-config",
        "tsa.cnf",
        "-queryfile",
        "request.tsq",
        "-out",
        "reply.tsr",
        ...(mode === "large" ? ["-chain", "large-chain.pem"] : []),
      );
      if (mode === "large") check(readFileSync(join(dir, "reply.tsr")).length > 65536, "oversize fixture too small");
      return new Response(readFileSync(join(dir, "reply.tsr")), {
        headers: { "Content-Type": "application/timestamp-reply" },
      });
    },
  });
  const input = join(dir, "input.pdf");
  fixture(input);
  try {
    await withControlledApp(EXE, async (client) => {
      const sign = async (source: string, dest: string, tsa: string, permission: number, success = true) => {
        const r = await client.request(ControlCommand.TestSignDocument, [
          source,
          dest,
          "",
          join(dir, "signer.pfx"),
          "test",
          "",
          -1,
          tsa,
          permission,
        ]);
        check((r[0] === 0) === success, `sign ${mode}: ${r[1]}`);
      };
      const timestamped = join(dir, "timestamped.pdf");
      await sign(input, timestamped, server.url.toString(), 2);
      verifyOutput(timestamped);
      console.log("CMS digest and RFC 3161 signature-value imprint: OpenSSL verified");
      for (mode of ["wrong", "fail", "large"]) {
        const dest = join(dir, `${mode}.pdf`);
        writeFileSync(dest, "existing destination");
        await sign(input, dest, server.url.toString(), 0, false);
        check(readFileSync(dest, "utf8") === "existing destination", "TSA failure replaced destination");
      }
      for (const permission of [1, 2, 3]) {
        await sign(input, join(dir, `p${permission}.pdf`), "", permission);
      }
      await sign(join(dir, "p2.pdf"), join(dir, "twice.pdf"), "", 0);
    });
    const old = join(dir, "old.pdf");
    oldSignature(old);
    const historic = await validation(old);
    check(
      !/validation status=(1|3) /.test(historic),
      `certificate valid at claimed signing time was rejected: ${historic}`,
    );
    check(/signing time is a signer claim/.test(historic), "historic claim presented as proven");
    const stamped = await validation(join(dir, "timestamped.pdf"));
    check(/Timestamp present but unverified or untrusted/.test(stamped), "self-signed TSA incorrectly trusted");
    const twice = await validation(join(dir, "twice.pdf"));
    check(
      /certified=1 permission=2/.test(twice) && /certified=0 permission=0/.test(twice),
      "approval signature mislabeled certified",
    );
    check(!/validation status=1 /.test(twice), `second signature invalidated first: ${twice}`);
    for (const permission of [1, 2, 3]) {
      const source = join(dir, `p${permission}.pdf`);
      const base = await validation(source);
      check(!/validation status=1 /.test(base), `unchanged P${permission} invalid`);
      check(/signing time is a signer claim/.test(base), "missing unproven-time warning");
      const filled = join(dir, `p${permission}-filled.pdf`);
      update(source, filled, {
        7: "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Text) /P 3 0 R /Rect [10 50 100 70] /V (after) >>",
      });
      const form = await validation(filled);
      check(/validation status=1 /.test(form) === (permission === 1), `P${permission} form policy: ${form}`);
      const changed = join(dir, `p${permission}-content.pdf`);
      update(source, changed, { 4: "<< /Length 4 >>\nstream\nq Q\nendstream" });
      check(/validation status=1 /.test(await validation(changed)), `P${permission} content tampering accepted`);
    }
    console.log("DocMDP P1/P2/P3, later approval, content tampering, untrusted TSA: passed");
  } finally {
    server.stop(true);
  }
}
if (import.meta.main) await runStandalone(testit);
