package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"log"

	"github.com/navrobotec/nav/registry-api/internal/database"
)

// GenerateToken issues random opaque payloads prefixed with mandated identifier standard
func GenerateToken() (string, error) {
	bytes := make([]byte, 32)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	// Direct random bytes map, prepended with required 'nav_' vector
	return fmt.Sprintf("nav_%s", hex.EncodeToString(bytes)), nil
}

// HashToken isolates raw secret payloads and produces one-way verification fingerprints for permanent storage
func HashToken(raw string) string {
	h := sha256.New()
	h.Write([]byte(raw))
	return hex.EncodeToString(h.Sum(nil))
}

// CreatePersistentToken hooks valid generated token into authoritative relational storage
func CreatePersistentToken(userID string, rawToken string, tokenName string) error {
	prefix := rawToken[:12] // Secure subset: 'nav_' + first 8 hex chars
	hashed := HashToken(rawToken)

	query := `
		INSERT INTO tokens (user_id, token_prefix, token_hash, name)
		VALUES ($1, $2, $3, $4)
	`
	
	_, err := database.DB.Exec(query, userID, prefix, hashed, tokenName)
	if err != nil {
		log.Printf("Token ingestion failure in DB segment: %v", err)
		return err
	}

	return nil
}

// GenerateTokenForUser performs high-level sequencing spawning fresh identifiers and anchoring them permanently.
func GenerateTokenForUser(userID string, name string) (string, string, error) {
	raw, err := GenerateToken()
	if err != nil {
		return "", "", err
	}

	err = CreatePersistentToken(userID, raw, name)
	if err != nil {
		return "", "", err
	}

	return raw, raw[:12], nil
}

