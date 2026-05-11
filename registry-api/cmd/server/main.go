package main

import (
	"log"

	"github.com/gofiber/fiber/v2"
	"github.com/gofiber/fiber/v2/middleware/cors"
	"github.com/gofiber/fiber/v2/middleware/logger"
	"github.com/navrobotec/nav/registry-api/internal/api"
	"github.com/navrobotec/nav/registry-api/internal/database"
	"github.com/navrobotec/nav/registry-api/internal/storage"
)

func main() {
	// Initialize Infrastructure Matrix
	database.Connect()
	storage.Connect()

	app := fiber.New(fiber.Config{
		AppName: "Nav Registry Service v1.0",
	})

	// Global Middleware
	app.Use(logger.New())
	app.Use(cors.New())

	// Health check
	app.Get("/health", func(c *fiber.Ctx) error {
		return c.JSON(fiber.Map{"status": "healthy", "version": "1.0.0"})
	})

	// Setup Routes
	api.RegisterRoutes(app)

	// Mount Static Visualization Portal
	app.Static("/", "./web")

	log.Println("Starting Nav Registry service on :8081...")
	log.Fatal(app.Listen(":8081"))
}
