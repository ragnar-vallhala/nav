-- Seed a handful of packages so the CLI <-> registry index loop has real data.
-- Each package_versions.manifest holds the IndexVersion JSON the CLI consumes
-- via /api/v1/index/:name. checksum_sha256 / archive_url duplicate the
-- library "source" download for the NOT NULL columns.

-- Owner identity for seeded packages.
INSERT INTO users (id, username, email, password_hash, email_verified)
VALUES ('b1000000-0000-4000-8000-000000000001', 'nav_seed', 'seed@nav.local', 'seed_placeholder_hash', TRUE)
ON CONFLICT (username) DO NOTHING;

-- Clean prior seed of these names so the script is idempotent.
DELETE FROM packages WHERE name IN ('nav-hal', 'cmsis', 'arm-none-eabi-gcc');

-- ---- nav-hal (library, depends on cmsis) -------------------------------------
INSERT INTO packages (id, namespace, name, description, owner_id, total_downloads)
VALUES ('c1000000-0000-4000-8000-000000000001', 'navrobotec', 'nav-hal',
        'NavHAL hardware abstraction layer', 'b1000000-0000-4000-8000-000000000001', 1280);

INSERT INTO package_versions (package_id, version, description, checksum_sha256, archive_url, manifest, published_by)
VALUES
('c1000000-0000-4000-8000-000000000001', '0.5.0', 'NavHAL 0.5.0',
 'deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef',
 'http://localhost:9000/nav-packages/nav-hal-0.5.0.tar.gz',
 '{"version":"0.5.0","kind":"library","downloads":{"source":{"url":"http://localhost:9000/nav-packages/nav-hal-0.5.0.tar.gz","sha256":"deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"}},"dependencies":{"cmsis":"^6.0.0"}}'::jsonb,
 'b1000000-0000-4000-8000-000000000001'),
('c1000000-0000-4000-8000-000000000001', '0.4.2', 'NavHAL 0.4.2',
 'cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe',
 'http://localhost:9000/nav-packages/nav-hal-0.4.2.tar.gz',
 '{"version":"0.4.2","kind":"library","downloads":{"source":{"url":"http://localhost:9000/nav-packages/nav-hal-0.4.2.tar.gz","sha256":"cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe"}},"dependencies":{"cmsis":"^6.0.0"}}'::jsonb,
 'b1000000-0000-4000-8000-000000000001');

-- ---- cmsis (library, no deps) ------------------------------------------------
INSERT INTO packages (id, namespace, name, description, owner_id, total_downloads)
VALUES ('c1000000-0000-4000-8000-000000000002', 'arm', 'cmsis',
        'ARM CMSIS core headers', 'b1000000-0000-4000-8000-000000000001', 4096);

INSERT INTO package_versions (package_id, version, description, checksum_sha256, archive_url, manifest, published_by)
VALUES
('c1000000-0000-4000-8000-000000000002', '6.0.0', 'CMSIS 6.0.0',
 '1111111111111111111111111111111111111111111111111111111111111111',
 'http://localhost:9000/nav-packages/cmsis-6.0.0.tar.gz',
 '{"version":"6.0.0","kind":"library","downloads":{"source":{"url":"http://localhost:9000/nav-packages/cmsis-6.0.0.tar.gz","sha256":"1111111111111111111111111111111111111111111111111111111111111111"}}}'::jsonb,
 'b1000000-0000-4000-8000-000000000001'),
('c1000000-0000-4000-8000-000000000002', '6.1.0', 'CMSIS 6.1.0',
 '2222222222222222222222222222222222222222222222222222222222222222',
 'http://localhost:9000/nav-packages/cmsis-6.1.0.tar.gz',
 '{"version":"6.1.0","kind":"library","downloads":{"source":{"url":"http://localhost:9000/nav-packages/cmsis-6.1.0.tar.gz","sha256":"2222222222222222222222222222222222222222222222222222222222222222"}}}'::jsonb,
 'b1000000-0000-4000-8000-000000000001');

-- ---- arm-none-eabi-gcc (toolchain, multi-platform) --------------------------
INSERT INTO packages (id, namespace, name, description, owner_id, total_downloads)
VALUES ('c1000000-0000-4000-8000-000000000003', 'arm', 'arm-none-eabi-gcc',
        'ARM GNU bare-metal toolchain', 'b1000000-0000-4000-8000-000000000001', 8800);

INSERT INTO package_versions (package_id, version, description, checksum_sha256, archive_url, manifest, published_by)
VALUES
('c1000000-0000-4000-8000-000000000003', '13.2.0', 'Arm GNU Toolchain 13.2',
 '3333333333333333333333333333333333333333333333333333333333333333',
 'http://localhost:9000/nav-packages/arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz',
 '{"version":"13.2.0","kind":"toolchain","toolchain_binaries":["arm-none-eabi-gcc","arm-none-eabi-g++","arm-none-eabi-objcopy","arm-none-eabi-size"],"downloads":{"linux_x86_64":{"url":"http://localhost:9000/nav-packages/arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz","sha256":"3333333333333333333333333333333333333333333333333333333333333333"},"darwin_arm64":{"url":"http://localhost:9000/nav-packages/arm-none-eabi-gcc-13.2.0-darwin_arm64.tar.xz","sha256":"4444444444444444444444444444444444444444444444444444444444444444"}}}'::jsonb,
 'b1000000-0000-4000-8000-000000000001');
