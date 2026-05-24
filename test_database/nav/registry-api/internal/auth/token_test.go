package auth

import (
	"strings"
	"testing"
)

// TestTokenGeneration confirms standard prefix adherence and distinct output sequences
func TestTokenGeneration(t *testing.T) {
	token1, err := GenerateToken()
	if err != nil {
		t.Fatalf("Token generation critically failed: %v", err)
	}

	token2, err := GenerateToken()
	if err != nil {
		t.Fatalf("Token generation critically failed: %v", err)
	}

	// Verification 1: Must contain the mandatory identifying header namespace
	if !strings.HasPrefix(token1, "nav_") {
		t.Errorf("Violated security format constraint: token does not start with 'nav_': Got %s", token1)
	}

	// Verification 2: Sequential calls must absolutely generate discrete vectors
	if token1 == token2 {
		t.Errorf("Cryptographic collision failure: Sequential calls yielded identical identifiers!")
	}

	// Verification 3: Adequate length ensuring high entropy bounds
	if len(token1) < 40 {
		t.Errorf("Entropy below safe bounds: Token too short: Got length %d", len(token1))
	}
}

// TestHashIntegrity verifies the One-Way consistency guaranteeing storage matches
func TestHashIntegrity(t *testing.T) {
	sample := "nav_secure_test_payload_xyz"
	
	hash1 := HashToken(sample)
	hash2 := HashToken(sample)

	// Principle of Determinism
	if hash1 != hash2 {
		t.Errorf("Deterministic breakdown: Identical inputs resulted in distinct outputs!")
	}

	// Verify we aren't accidentally returning raw text
	if hash1 == sample {
		t.Errorf("Fatal security flaw: Hash routine yielded plaintext identity mask!")
	}
}
