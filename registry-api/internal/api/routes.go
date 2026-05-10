package api

import (
	"context"
	"fmt"
	"time"

	"github.com/gofiber/fiber/v2"
	"github.com/navrobotec/nav/registry-api/internal/auth"
	"github.com/navrobotec/nav/registry-api/internal/storage"
)

// RegisterRoutes hooks up specific application endpoints
func RegisterRoutes(app *fiber.App) {
	v1 := app.Group("/api/v1")

	v1.Post("/publish/init", auth.RequireToken(), initPublish)
	v1.Post("/publish", auth.RequireToken(), publishPackage)
	v1.Get("/search", searchPackages)
	v1.Get("/package/:name", getPackageMetadata)
	v1.Post("/auth/login", authenticate)
}

type PublishInitRequest struct {
	Namespace string `json:"namespace"`
	Name      string `json:"name"`
	Version   string `json:"version"`
}

func initPublish(c *fiber.Ctx) error {
	var req PublishInitRequest
	if err := c.BodyParser(&req); err != nil {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{
			"error": "Invalid JSON payload format",
		})
	}

	if req.Namespace == "" || req.Name == "" || req.Version == "" {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{
			"error": "Violated protocol: 'namespace', 'name', and 'version' are all mandated",
		})
	}

	// Construct deterministic canonical storage matrix path
	objectName := fmt.Sprintf("%s/%s/%s/%s-%s.tar.zst", req.Namespace, req.Name, req.Version, req.Name, req.Version)
	
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// Generate a standard short-lived 15-minute secure uplink lease
	uploadURL, err := storage.Storage.GeneratePresignedUploadURL(ctx, "nav-packages", objectName, 15*time.Minute)
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{
			"error": fmt.Sprintf("Storage subsystem negotiation failure: %v", err),
		})
	}

	return c.Status(fiber.StatusAccepted).JSON(fiber.Map{
		"status":      "initiated",
		"upload_url":  uploadURL,
		"object_path": objectName,
		"expires_in":  "15m",
	})
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
