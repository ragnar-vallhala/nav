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

	// Automated Dynamic Lifecycle Migrations (Ensures effortless field synthesis)
	migrationQuery := `
		CREATE TABLE IF NOT EXISTS community_posts (
			id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
			user_id UUID NOT NULL REFERENCES users(id),
			username VARCHAR(64) NOT NULL,
			title VARCHAR(255) NOT NULL,
			content TEXT,
			votes INT DEFAULT 0,
			upvotes INT DEFAULT 0,
			downvotes INT DEFAULT 0,
			comments INT DEFAULT 0,
			created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
		);
	`
	if _, err := DB.Exec(migrationQuery); err != nil {
		log.Printf("Warning: System subspace auto-migration encountered warning: %v", err)
	}
	
	// Ensure dynamic backward compatibility
	DB.Exec(`ALTER TABLE community_posts ADD COLUMN IF NOT EXISTS content TEXT;`)
	DB.Exec(`ALTER TABLE community_posts ADD COLUMN IF NOT EXISTS upvotes INT DEFAULT 0;`)
	DB.Exec(`ALTER TABLE community_posts ADD COLUMN IF NOT EXISTS downvotes INT DEFAULT 0;`)

	// Provision Discussion Lattice Node
	commentMigration := `
		CREATE TABLE IF NOT EXISTS community_comments (
			id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
			post_id UUID NOT NULL REFERENCES community_posts(id) ON DELETE CASCADE,
			user_id UUID NOT NULL REFERENCES users(id),
			username VARCHAR(64) NOT NULL,
			content TEXT NOT NULL,
			parent_id UUID REFERENCES community_comments(id) ON DELETE CASCADE,
			created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
		);
	`
	DB.Exec(commentMigration)
	
	// Handle backward-compatible column expansion for existing DB pools
	DB.Exec(`ALTER TABLE community_comments ADD COLUMN IF NOT EXISTS parent_id UUID REFERENCES community_comments(id) ON DELETE CASCADE;`)

	// Provision Strict Identity Reputation Matrix (Ensures singular distinct voter claims)
	voteMigration := `
		CREATE TABLE IF NOT EXISTS community_votes (
			post_id UUID NOT NULL REFERENCES community_posts(id) ON DELETE CASCADE,
			user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
			vote INT NOT NULL, -- 1 or -1
			PRIMARY KEY (post_id, user_id)
		);
	`
	DB.Exec(voteMigration)

	log.Println("Database pool stabilized and securely tethered.")
}

func getEnv(key, fallback string) string {
	if value, exists := os.LookupEnv(key); exists {
		return value
	}
	return fallback
}
