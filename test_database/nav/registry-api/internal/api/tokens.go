package api

import (
	"github.com/gofiber/fiber/v2"
	"github.com/navrobotec/nav/registry-api/internal/auth"
	"github.com/navrobotec/nav/registry-api/internal/database"
)

type TokenMetadata struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	TokenPrefix string `json:"prefix"`
	CreatedAt   string `json:"created_at"`
}

func listTokens(c *fiber.Ctx) error {
	userID := c.Locals("user_id").(string)

	rows, err := database.DB.Query(
		`SELECT id, COALESCE(name, 'Unnamed'), token_prefix, created_at 
		 FROM tokens 
		 WHERE user_id = $1 
		 AND name NOT IN ('System Created Initial Key', 'Web Session Entry') 
		 ORDER BY created_at DESC`, 
		userID,
	)
	if err != nil {
		return c.Status(500).JSON(fiber.Map{"error": "Relational token retrieval fault"})
	}
	defer rows.Close()

	var tokenList []TokenMetadata
	for rows.Next() {
		var t TokenMetadata
		if err := rows.Scan(&t.ID, &t.Name, &t.TokenPrefix, &t.CreatedAt); err == nil {
			tokenList = append(tokenList, t)
		}
	}

	return c.JSON(fiber.Map{"tokens": tokenList})
}

type CreateTokenRequest struct {
	Name string `json:"name"`
}

func createToken(c *fiber.Ctx) error {
	userID := c.Locals("user_id").(string)
	
	var req CreateTokenRequest
	if err := c.BodyParser(&req); err != nil || req.Name == "" {
		req.Name = "Dynamic Access Node" // Fallback if unnamed
	}

	rawToken, prefix, err := auth.GenerateTokenForUser(userID, req.Name)
	if err != nil {
		return c.Status(500).JSON(fiber.Map{"error": "Token extraction loop failure"})
	}

	return c.Status(201).JSON(fiber.Map{
		"message": "High-clearance access identifier provisioned.",
		"token":   rawToken,
		"prefix":  prefix,
	})
}

func revokeToken(c *fiber.Ctx) error {
	userID := c.Locals("user_id").(string)
	tokenID := c.Params("id")

	result, err := database.DB.Exec("DELETE FROM tokens WHERE id = $1 AND user_id = $2", tokenID, userID)
	if err != nil {
		return c.Status(500).JSON(fiber.Map{"error": "Operation fault during revocation vector"})
	}

	rows, _ := result.RowsAffected()
	if rows == 0 {
		return c.Status(404).JSON(fiber.Map{"error": "Target identifier not found or already deprecated"})
	}

	return c.JSON(fiber.Map{"status": "deprecated", "message": "Vector forcefully decoupled."})
}
