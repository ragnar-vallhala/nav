package api

import (
	"github.com/gofiber/fiber/v2"
	"github.com/navrobotec/nav/registry-api/internal/auth"
)

// RegisterRoutes hooks up specific application endpoints
func RegisterRoutes(app *fiber.App) {
	v1 := app.Group("/api/v1")

	v1.Post("/publish", auth.RequireToken(), publishPackage)
	v1.Get("/search", searchPackages)
	v1.Get("/package/:name", getPackageMetadata)
	v1.Post("/auth/login", authenticate)
}

func publishPackage(c *fiber.Ctx) error {
	// TODO: Validate package payload, push archive to MinIO, update git index
	return c.Status(fiber.StatusNotImplemented).JSON(fiber.Map{
		"error": "Publish endpoint coming online soon",
	})
}

func searchPackages(c *fiber.Ctx) error {
	query := c.Query("q")
	return c.JSON(fiber.Map{
		"query":   query,
		"results": []string{},
	})
}

func getPackageMetadata(c *fiber.Ctx) error {
	name := c.Params("name")
	return c.JSON(fiber.Map{
		"package": name,
		"version": "0.0.0",
	})
}

func authenticate(c *fiber.Ctx) error {
	return c.JSON(fiber.Map{
		"token": "nav_dev_mock_token_12345",
	})
}
