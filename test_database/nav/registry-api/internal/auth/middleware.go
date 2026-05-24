package auth

import (
	"strings"

	"github.com/gofiber/fiber/v2"
	"github.com/navrobotec/nav/registry-api/internal/database"
)

// RequireToken enforces presence and base formatting for inbound Authorization headers
func RequireToken() fiber.Handler {
	return func(c *fiber.Ctx) error {
		authHeader := c.Get("Authorization")
		
		// Extract bearer prefix payload
		if authHeader == "" || !strings.HasPrefix(authHeader, "Bearer ") {
			return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{
				"error": "Missing/Invalid authorization credential header.",
			})
		}

		token := strings.TrimPrefix(authHeader, "Bearer ")

		// Mandatory format validator: Token must include standard "nav_" namespace identifier
		if !strings.HasPrefix(token, "nav_") {
			return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{
				"error": "Cryptographic anchor mismatch. Re-run 'nav login'.",
			})
		}

		// Transform inbound string to one-way validation hash
		hashed := HashToken(token)

		var userID string
		query := "SELECT user_id FROM tokens WHERE token_hash = $1 AND (expires_at IS NULL OR expires_at > NOW())"
		
		err := database.DB.QueryRow(query, hashed).Scan(&userID)
		if err != nil {
			// Missing match implies revoked, expired, or non-existent record
			return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{
				"error": "Invalid token session context. Access terminated.",
			})
		}

		// Push resolving user vector into context stream for subsequent downstream consumption
		c.Locals("user_id", userID)
		
		return c.Next()
	}
}
