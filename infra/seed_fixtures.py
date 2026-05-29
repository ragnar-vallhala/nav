#!/usr/bin/env python3
"""Build real package fixtures, upload them to the dev MinIO, and regenerate
infra/postgres/seed_packages.sql with checksums that actually match the
artifacts.

The committed seed used placeholder checksums (deadbeef...) and pointed at
tarballs that were never created, so `nav add` could resolve + lock but had
nothing real to download. This script closes that loop for local development:

    python3 infra/seed_fixtures.py            # build + upload + regen SQL
    python3 infra/seed_fixtures.py --apply    # also psql the seed into nav-db

It talks to the running compose stack (containers nav-minio / nav-db) via
`docker exec`, so `docker compose up -d database storage` must be running first.

Tarballs are built reproducibly (sorted entries, fixed mtime/owner, gzip -n)
so re-running yields identical checksums.
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import io

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(REPO, "build", "fixtures")
SEED_SQL = os.path.join(REPO, "infra", "postgres", "seed_packages.sql")

MINIO_CONTAINER = "nav-minio"
DB_CONTAINER = "nav-db"
BUCKET = "nav-packages"
# Object base URL the CLI will fetch from (matches the seeded archive_url host).
PUBLIC_BASE = "http://localhost:9000/" + BUCKET

FIXED_MTIME = 1577836800  # 2020-01-01T00:00:00Z, for reproducible archives


def add_file(tar, name, content):
    data = content.encode() if isinstance(content, str) else content
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mtime = FIXED_MTIME
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mode = 0o755 if name.endswith("/bin") or "/bin/" in name else 0o644
    tar.addfile(info, io.BytesIO(data))


def build_tar(path, members, compression):
    """members: list of (arcname, content). compression: 'gz' or 'xz'."""
    raw = io.BytesIO()
    mode = "w"  # write uncompressed into the buffer; compress afterwards
    with tarfile.open(fileobj=raw, mode=mode, format=tarfile.GNU_FORMAT) as tar:
        for arcname, content in sorted(members, key=lambda m: m[0]):
            add_file(tar, arcname, content)
    payload = raw.getvalue()

    if compression == "gz":
        import gzip
        # mtime=0 keeps the gzip header byte-stable (equivalent to gzip -n).
        with open(path, "wb") as f:
            f.write(gzip.compress(payload, mtime=0))
    elif compression == "xz":
        import lzma
        with open(path, "wb") as f:
            f.write(lzma.compress(payload))
    else:
        raise ValueError(compression)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def lib_members(name, version, extra=None):
    # Flat package layout (the Nav contract): include/, lib/, bin/ sit at the
    # root of the extracted package dir — no <name>-<version>/ wrapper. This is
    # what build's nav-deps.cmake generation assumes.
    guard = name.upper().replace('-', '_')
    m = [
        (f"include/{name}.h",
         f"#ifndef {guard}_H\n"
         f"#define {guard}_H\n"
         f"#define {guard}_VERSION \"{version}\"\n"
         f"#endif\n"),
        ("LICENSE", "MIT\n"),
        ("nav-package.json",
         json.dumps({"name": name, "version": version, "kind": "library"}, indent=2) + "\n"),
    ]
    if extra:
        m.extend(extra)
    return m


def toolchain_members(version, platform):
    bins = ["arm-none-eabi-gcc", "arm-none-eabi-g++",
            "arm-none-eabi-objcopy", "arm-none-eabi-size"]
    m = [(f"bin/{b}", f"#!/bin/sh\necho \"{b} (Nav fixture {platform}) {version}\"\n")
         for b in bins]
    m.append(("nav-package.json",
              json.dumps({"name": "arm-none-eabi-gcc", "version": version,
                          "kind": "toolchain", "platform": platform}, indent=2) + "\n"))
    return m


# (object filename, builder -> members, compression)
FIXTURES = {
    "cmsis-6.0.0.tar.gz": (lambda: lib_members("cmsis", "6.0.0"), "gz"),
    "cmsis-6.1.0.tar.gz": (lambda: lib_members("cmsis", "6.1.0"), "gz"),
    "nav-hal-0.4.2.tar.gz": (lambda: lib_members("nav-hal", "0.4.2"), "gz"),
    "nav-hal-0.5.0.tar.gz": (lambda: lib_members("nav-hal", "0.5.0"), "gz"),
    "arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz":
        (lambda: toolchain_members("13.2.0", "linux_x86_64"), "xz"),
    "arm-none-eabi-gcc-13.2.0-darwin_arm64.tar.xz":
        (lambda: toolchain_members("13.2.0", "darwin_arm64"), "xz"),
}


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def docker_exec(container, *args, **kw):
    return run(["docker", "exec", container, *args], **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true",
                    help="also apply the regenerated seed to nav-db via psql")
    ap.add_argument("--no-upload", action="store_true",
                    help="build + regen SQL only; skip MinIO upload")
    args = ap.parse_args()

    os.makedirs(WORK, exist_ok=True)
    shas = {}
    for fname, (builder, comp) in FIXTURES.items():
        path = os.path.join(WORK, fname)
        build_tar(path, builder(), comp)
        shas[fname] = sha256_file(path)
        print(f"built {fname}  sha256={shas[fname]}")

    if not args.no_upload:
        upload(shas.keys())

    render_seed(shas)
    print(f"wrote {SEED_SQL}")

    if args.apply:
        apply_seed()
        print("seed applied to nav-db")


def upload(fnames):
    # Configure mc inside the minio container, ensure a public-read bucket,
    # then copy each fixture in via a docker cp staging area.
    docker_exec(MINIO_CONTAINER, "mc", "alias", "set", "local",
                "http://127.0.0.1:9000", "navroot", "navpassword123")
    docker_exec(MINIO_CONTAINER, "mc", "mb", "--ignore-existing",
                f"local/{BUCKET}")
    docker_exec(MINIO_CONTAINER, "mc", "anonymous", "set", "download",
                f"local/{BUCKET}")
    for fname in fnames:
        host_path = os.path.join(WORK, fname)
        run(["docker", "cp", host_path, f"{MINIO_CONTAINER}:/tmp/{fname}"])
        docker_exec(MINIO_CONTAINER, "mc", "cp", f"/tmp/{fname}",
                    f"local/{BUCKET}/{fname}")
        print(f"uploaded {fname}")


def render_seed(shas):
    def url(f):
        return f"{PUBLIC_BASE}/{f}"

    cmsis_600 = shas["cmsis-6.0.0.tar.gz"]
    cmsis_610 = shas["cmsis-6.1.0.tar.gz"]
    hal_042 = shas["nav-hal-0.4.2.tar.gz"]
    hal_050 = shas["nav-hal-0.5.0.tar.gz"]
    gcc_linux = shas["arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz"]
    gcc_darwin = shas["arm-none-eabi-gcc-13.2.0-darwin_arm64.tar.xz"]

    def lib_manifest(name, version, sha, fname, deps=None):
        d = {"version": version, "kind": "library",
             "downloads": {"source": {"url": url(fname), "sha256": sha}}}
        if deps:
            d["dependencies"] = deps
        return json.dumps(d)

    gcc_manifest = json.dumps({
        "version": "13.2.0", "kind": "toolchain",
        "toolchain_binaries": ["arm-none-eabi-gcc", "arm-none-eabi-g++",
                               "arm-none-eabi-objcopy", "arm-none-eabi-size"],
        "downloads": {
            "linux_x86_64": {"url": url("arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz"),
                             "sha256": gcc_linux},
            "darwin_arm64": {"url": url("arm-none-eabi-gcc-13.2.0-darwin_arm64.tar.xz"),
                             "sha256": gcc_darwin},
        },
    })

    sql = f"""-- GENERATED by infra/seed_fixtures.py — do not edit by hand.
-- Seeds packages whose checksums + archive URLs match real tarballs uploaded
-- to the dev MinIO ({PUBLIC_BASE}). Regenerate with:
--   python3 infra/seed_fixtures.py
-- Each package_versions.manifest holds the IndexVersion JSON the CLI consumes
-- via /api/v1/index/:name.

INSERT INTO users (id, username, email, password_hash, email_verified)
VALUES ('b1000000-0000-4000-8000-000000000001', 'nav_seed', 'seed@nav.local', 'seed_placeholder_hash', TRUE)
ON CONFLICT (username) DO NOTHING;

DELETE FROM packages WHERE name IN ('nav-hal', 'cmsis', 'arm-none-eabi-gcc');

-- ---- nav-hal (library, depends on cmsis) -------------------------------------
INSERT INTO packages (id, namespace, name, description, owner_id, total_downloads)
VALUES ('c1000000-0000-4000-8000-000000000001', 'navrobotec', 'nav-hal',
        'NavHAL hardware abstraction layer', 'b1000000-0000-4000-8000-000000000001', 1280);

INSERT INTO package_versions (package_id, version, description, checksum_sha256, archive_url, manifest, published_by)
VALUES
('c1000000-0000-4000-8000-000000000001', '0.5.0', 'NavHAL 0.5.0',
 '{hal_050}',
 '{url("nav-hal-0.5.0.tar.gz")}',
 '{lib_manifest("nav-hal", "0.5.0", hal_050, "nav-hal-0.5.0.tar.gz", {"cmsis": "^6.0.0"})}'::jsonb,
 'b1000000-0000-4000-8000-000000000001'),
('c1000000-0000-4000-8000-000000000001', '0.4.2', 'NavHAL 0.4.2',
 '{hal_042}',
 '{url("nav-hal-0.4.2.tar.gz")}',
 '{lib_manifest("nav-hal", "0.4.2", hal_042, "nav-hal-0.4.2.tar.gz", {"cmsis": "^6.0.0"})}'::jsonb,
 'b1000000-0000-4000-8000-000000000001');

-- ---- cmsis (library, no deps) ------------------------------------------------
INSERT INTO packages (id, namespace, name, description, owner_id, total_downloads)
VALUES ('c1000000-0000-4000-8000-000000000002', 'arm', 'cmsis',
        'ARM CMSIS core headers', 'b1000000-0000-4000-8000-000000000001', 4096);

INSERT INTO package_versions (package_id, version, description, checksum_sha256, archive_url, manifest, published_by)
VALUES
('c1000000-0000-4000-8000-000000000002', '6.0.0', 'CMSIS 6.0.0',
 '{cmsis_600}',
 '{url("cmsis-6.0.0.tar.gz")}',
 '{lib_manifest("cmsis", "6.0.0", cmsis_600, "cmsis-6.0.0.tar.gz")}'::jsonb,
 'b1000000-0000-4000-8000-000000000001'),
('c1000000-0000-4000-8000-000000000002', '6.1.0', 'CMSIS 6.1.0',
 '{cmsis_610}',
 '{url("cmsis-6.1.0.tar.gz")}',
 '{lib_manifest("cmsis", "6.1.0", cmsis_610, "cmsis-6.1.0.tar.gz")}'::jsonb,
 'b1000000-0000-4000-8000-000000000001');

-- ---- arm-none-eabi-gcc (toolchain, multi-platform) --------------------------
INSERT INTO packages (id, namespace, name, description, owner_id, total_downloads)
VALUES ('c1000000-0000-4000-8000-000000000003', 'arm', 'arm-none-eabi-gcc',
        'ARM GNU bare-metal toolchain', 'b1000000-0000-4000-8000-000000000001', 8800);

INSERT INTO package_versions (package_id, version, description, checksum_sha256, archive_url, manifest, published_by)
VALUES
('c1000000-0000-4000-8000-000000000003', '13.2.0', 'Arm GNU Toolchain 13.2',
 '{gcc_linux}',
 '{url("arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz")}',
 '{gcc_manifest}'::jsonb,
 'b1000000-0000-4000-8000-000000000001');
"""
    with open(SEED_SQL, "w") as f:
        f.write(sql)


def apply_seed():
    with open(SEED_SQL, "rb") as f:
        run(["docker", "exec", "-i", DB_CONTAINER,
             "psql", "-U", "nav_admin", "-d", "nav_registry", "-v", "ON_ERROR_STOP=1"],
            input=f.read())


if __name__ == "__main__":
    main()
