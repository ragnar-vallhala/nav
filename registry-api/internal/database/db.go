package database

import (
	"database/sql"
	"fmt"
	"log"
	"os"

	_ "github.com/lib/pq" // Mandated native postgres driver handle
)

var DB *sql.DB

// Connect establishes canonical thread-safe persistence pool against target network vectors
func Connect() {
	host := getEnv("DB_HOST", "localhost")
	user := getEnv("DB_USER", "nav_admin")
	pass := getEnv("DB_PASSWORD", "nav_secure_password_change_me")
	dbname := getEnv("DB_NAME", "nav_registry")

	dsn := fmt.Sprintf("host=%s user=%s password=%s dbname=%s sslmode=disable", host, user, pass, dbname)

	var err error
	DB, err = sql.Open("postgres", dsn)
	if err != nil {
		log.Fatalf("Failed to instantiate SQL descriptor: %v", err)
	}

	// Force instantiation via explicit ping test
	if err = DB.Ping(); err != nil {
		log.Fatalf("Persistence ignition failed (Target Unreachable): %v", err)
	}

	log.Println("Database pool stabilized and securely tethered.")
}

func getEnv(key, fallback string) string {
	if value, exists := os.LookupEnv(key); exists {
		return value
	}
	return fallback
}
