-- Standard Test Vector Identification: UUID static mock
DELETE FROM users WHERE username = 'test_integrator';

INSERT INTO users (id, username, email, password_hash)
VALUES (
    'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11', 
    'test_integrator', 
    'test@nav.local', 
    'argon2_placeholder_hash'
);

DELETE FROM tokens WHERE token_prefix = 'nav_testv';

-- Pre-Injecting established vector: 'nav_testvector_secure'
-- Hashed Value of 'nav_testvector_secure' using SHA256 is below:
INSERT INTO tokens (user_id, token_prefix, token_hash, name)
VALUES (
    'a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11',
    'nav_testv',
    '0a532c3b05b62aab2ae698138dffdf88ffe6159c1aa27e01861fc65670b7c0a4', -- Echo -n "nav_testvector_secure" | sha256sum
    'automated-test-agent'
);
