package api

import (
	"context"
	"database/sql"
	"fmt"
	"math/rand"
	"strings"
	"time"

	"github.com/gofiber/fiber/v2"
	"github.com/navrobotec/nav/registry-api/internal/auth"
	"github.com/navrobotec/nav/registry-api/internal/database"
	"github.com/navrobotec/nav/registry-api/internal/storage"
	"golang.org/x/crypto/bcrypt"
)

// Optional resolver utility retrieving valid operator identities passively
func getOptionalUserID(c *fiber.Ctx) string {
	authHeader := c.Get("Authorization")
	if authHeader == "" || !strings.HasPrefix(authHeader, "Bearer ") { return "" }
	token := strings.TrimPrefix(authHeader, "Bearer ")
	if !strings.HasPrefix(token, "nav_") { return "" }
	
	hashed := auth.HashToken(token)
	var uID string
	err := database.DB.QueryRow("SELECT user_id FROM tokens WHERE token_hash = $1 AND (expires_at IS NULL OR expires_at > NOW())", hashed).Scan(&uID)
	if err == nil { return uID }
	return ""
}

// RegisterRoutes hooks up specific application endpoints
func RegisterRoutes(app *fiber.App) {
	v1 := app.Group("/api/v1")

	// Open Vectors (Identity Operations)
	v1.Post("/auth/register", handleRegister)
	v1.Post("/auth/verify", handleVerifyEmail)
	v1.Post("/auth/login", handleLogin)

	// Protected Management & Supply Chain Vectors
	v1.Post("/publish/init", auth.RequireToken(), initPublish)
	v1.Post("/publish", auth.RequireToken(), publishPackage)
	
	// Hyper-Secure Identifier Oversights (Token Management)
	v1.Get("/tokens", auth.RequireToken(), listTokens)
	v1.Post("/tokens", auth.RequireToken(), createToken)
	v1.Delete("/tokens/:id", auth.RequireToken(), revokeToken)

	// Distributed Narrative Silos (Community)
	v1.Get("/community/posts", listPosts)
	v1.Get("/community/posts/:id", getPostDetail)
	v1.Post("/community/posts", auth.RequireToken(), createPost)
	v1.Post("/community/posts/:id/vote", auth.RequireToken(), votePost)
	v1.Post("/community/upload", auth.RequireToken(), uploadAsset)
	
	// Interactive Thread Sub-Silos
	v1.Get("/community/posts/:id/comments", listComments)
	v1.Post("/community/posts/:id/comments", auth.RequireToken(), createComment)

	v1.Get("/search", searchPackages)
	v1.Get("/package/:name", getPackageMetadata)
}

type AuthRequest struct {
	Username string `json:"username"`
	Email    string `json:"email"`
	Password string `json:"password"`
	Code     string `json:"code"`
}

func generateVerificationCode() string {
	r := rand.New(rand.NewSource(time.Now().UnixNano()))
	return fmt.Sprintf("%06d", r.Intn(1000000))
}

func handleRegister(c *fiber.Ctx) error {
	var req AuthRequest
	if err := c.BodyParser(&req); err != nil || req.Username == "" || req.Password == "" || req.Email == "" {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Missing required vectors: username, email, password"})
	}

	hashed, err := bcrypt.GenerateFromPassword([]byte(req.Password), bcrypt.DefaultCost)
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Encryption lattice fault"})
	}

	// Generate primary identification challenge
	vCode := generateVerificationCode()

	var userID string
	query := `INSERT INTO users (username, email, password_hash, verification_code) VALUES ($1, $2, $3, $4) RETURNING id`
	err = database.DB.QueryRow(query, req.Username, req.Email, string(hashed), vCode).Scan(&userID)
	if err != nil {
		return c.Status(fiber.StatusConflict).JSON(fiber.Map{"error": "Identity collision detected: user or email already locked in registry."})
	}

	// Dispatch non-blocking challenge vector
	go func(targetEmail, targetUser, code string) {
		_ = auth.SendVerificationEmail(targetEmail, targetUser, code)
	}(req.Email, req.Username, vCode)
	
	return c.Status(fiber.StatusAccepted).JSON(fiber.Map{
		"message": "Identity provision initiated. Challenge packet transmitted.",
		"status":  "requires_verification",
	})
}

func handleVerifyEmail(c *fiber.Ctx) error {
	var req AuthRequest
	if err := c.BodyParser(&req); err != nil || req.Username == "" || req.Code == "" {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Verification request fragmented"})
	}

	var userID, dbCode string
	query := `SELECT id, verification_code FROM users WHERE username = $1 OR email = $1`
	err := database.DB.QueryRow(query, req.Username).Scan(&userID, &dbCode)
	if err != nil {
		return c.Status(fiber.StatusNotFound).JSON(fiber.Map{"error": "Target missing"})
	}

	// Direct secure sequence comparison
	if strings.TrimSpace(req.Code) != strings.TrimSpace(dbCode) {
		return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "Identification vector rejected: Invalid code"})
	}

	// Elevate authorization clearace
	_, err = database.DB.Exec("UPDATE users SET email_verified = TRUE, verification_code = NULL WHERE id = $1", userID)
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Permission elevation failed"})
	}

	// Secure instant generation of genesis token
	rawToken, _, _ := auth.GenerateTokenForUser(userID, "System Created Initial Key")

	return c.JSON(fiber.Map{
		"message": "Verification Complete. Grid Access Activated.",
		"token":   rawToken,
	})
}

func handleLogin(c *fiber.Ctx) error {
	var req AuthRequest
	if err := c.BodyParser(&req); err != nil || req.Username == "" || req.Password == "" {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Invalid credentials payload"})
	}

	var userID, passwordHash string
	var emailVerified bool
	
	query := `SELECT id, password_hash, email_verified FROM users WHERE username = $1 OR email = $1`
	err := database.DB.QueryRow(query, req.Username).Scan(&userID, &passwordHash, &emailVerified)
	
	if err == sql.ErrNoRows {
		return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "Authentication denied: Identity missing"})
	} else if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Internal verification bottleneck"})
	}

	// Evaluate hash validity
	if err := bcrypt.CompareHashAndPassword([]byte(passwordHash), []byte(req.Password)); err != nil {
		return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "Authentication denied: Cipher mismatch"})
	}

	// Enforce rigid identity gate
	if !emailVerified {
		return c.Status(fiber.StatusForbidden).JSON(fiber.Map{
			"error": "Channel unverified.",
			"status": "requires_verification",
		})
	}

	// Provision/Fetch standard active token
	rawToken, _, err := auth.GenerateTokenForUser(userID, "Web Session Entry")
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Token fabrication failure"})
	}

	return c.JSON(fiber.Map{
		"status": "access_granted",
		"token":  rawToken,
	})
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

type SearchResult struct {
	Namespace      string `json:"namespace"`
	Name           string `json:"name"`
	Description    string `json:"description"`
	Downloads      string `json:"downloads"`
	Version        string `json:"version"`
}

func searchPackages(c *fiber.Ctx) error {
	query := c.Query("q")
	searchTerm := "%" + query + "%"

	sqlQuery := `
		SELECT 
			p.namespace, 
			p.name, 
			COALESCE(p.description, ''), 
			p.total_downloads::text,
			COALESCE((SELECT version FROM package_versions WHERE package_id = p.id ORDER BY created_at DESC LIMIT 1), 'v0.0.0')
		FROM packages p
		WHERE p.name ILIKE $1 OR p.namespace ILIKE $1 OR p.description ILIKE $1
		ORDER BY p.total_downloads DESC
		LIMIT 100
	`

	rows, err := database.DB.Query(sqlQuery, searchTerm)
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Inventory database lookup failure"})
	}
	defer rows.Close()

	results := []SearchResult{}
	for rows.Next() {
		var r SearchResult
		if err := rows.Scan(&r.Namespace, &r.Name, &r.Description, &r.Downloads, &r.Version); err == nil {
			results = append(results, r)
		}
	}

	return c.JSON(fiber.Map{
		"query":   query,
		"results": results,
	})
}

func getPackageMetadata(c *fiber.Ctx) error {
	name := c.Params("name")
	return c.JSON(fiber.Map{
		"package": name,
		"version": "0.0.0",
	})
}

// COMMUNITY DATAGRID MODELS & CONTROLLERS

type CommunityPost struct {
	ID        string    `json:"id"`
	Username  string    `json:"username"`
	Title     string    `json:"title"`
	Content   string    `json:"content"`
	Votes     int       `json:"votes"`
	Upvotes   int       `json:"upvotes"`
	Downvotes int       `json:"downvotes"`
	Comments  int       `json:"comments"`
	UserVote  int       `json:"user_vote"` // Contextual reflection indicator
	CreatedAt time.Time `json:"created_at"`
}

type NewPostRequest struct {
	Title   string `json:"title"`
	Content string `json:"content"`
}

type VoteRequest struct {
	Delta int `json:"delta"` // 1 or -1
}

func listPosts(c *fiber.Ctx) error {
	uID := getOptionalUserID(c)
	
	query := `
		SELECT id, username, title, COALESCE(content, ''), votes, upvotes, downvotes, comments, created_at,
		       COALESCE((SELECT vote FROM community_votes WHERE post_id = community_posts.id AND user_id = $1), 0) as user_vote
		FROM community_posts 
		ORDER BY created_at DESC 
		LIMIT 100
	`
	rows, err := database.DB.Query(query, uID)
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Failed to synchronize community thread grid"})
	}
	defer rows.Close()

	posts := []CommunityPost{}
	for rows.Next() {
		var p CommunityPost
		if err := rows.Scan(&p.ID, &p.Username, &p.Title, &p.Content, &p.Votes, &p.Upvotes, &p.Downvotes, &p.Comments, &p.CreatedAt, &p.UserVote); err == nil {
			posts = append(posts, p)
		}
	}
	return c.JSON(posts)
}

func createPost(c *fiber.Ctx) error {
	userID := c.Locals("user_id").(string)
	
	var req NewPostRequest
	if err := c.BodyParser(&req); err != nil || req.Title == "" {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Broadcast packet malformed (Title required)"})
	}

	// Identify active operator
	var username string
	if err := database.DB.QueryRow("SELECT username FROM users WHERE id = $1", userID).Scan(&username); err != nil {
		return c.Status(fiber.StatusUnauthorized).JSON(fiber.Map{"error": "Invalid operator claim"})
	}

	query := `INSERT INTO community_posts (user_id, username, title, content) VALUES ($1, $2, $3, $4) RETURNING id`
	var newID string
	if err := database.DB.QueryRow(query, userID, username, req.Title, req.Content).Scan(&newID); err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Failed to anchor broadcast onto the lattice"})
	}

	return c.JSON(fiber.Map{"id": newID, "status": "broadcast_locked"})
}

func votePost(c *fiber.Ctx) error {
	userID := c.Locals("user_id").(string)
	postID := c.Params("id")
	
	var req VoteRequest
	if err := c.BodyParser(&req); err != nil {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Rating vector malformed"})
	}

	// Force binary polarity validation
	if req.Delta != 1 && req.Delta != -1 {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Invalid polarity vector"})
	}

	// Initiate strict atomic synchronization transaction
	tx, err := database.DB.Begin()
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Transaction lock failed"})
	}
	defer tx.Rollback() // Secure auto-recovery rollback safeguard

	var currentVote int
	exists := true
	err = tx.QueryRow("SELECT vote FROM community_votes WHERE post_id = $1 AND user_id = $2", postID, userID).Scan(&currentVote)
	if err == sql.ErrNoRows {
		exists = false
	} else if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Lattice collision detection failure"})
	}

	if !exists {
		// CASE 1: NO PREVIOUS VECTOR DETECTED. Deploy new assignment.
		_, err = tx.Exec("INSERT INTO community_votes (post_id, user_id, vote) VALUES ($1, $2, $3)", postID, userID, req.Delta)
		if err != nil { return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Anchor injection failed"}) }

		if req.Delta == 1 {
			tx.Exec("UPDATE community_posts SET upvotes = upvotes + 1, votes = votes + 1 WHERE id = $1", postID)
		} else {
			tx.Exec("UPDATE community_posts SET downvotes = downvotes + 1, votes = votes - 1 WHERE id = $1", postID)
		}
	} else {
		// EXISTING VECTOR DISCOVERED
		if currentVote == req.Delta {
			// CASE 2: RETRACTION - Clicked active state again. Force decouple.
			tx.Exec("DELETE FROM community_votes WHERE post_id = $1 AND user_id = $2", postID, userID)
			if req.Delta == 1 {
				tx.Exec("UPDATE community_posts SET upvotes = GREATEST(0, upvotes - 1), votes = votes - 1 WHERE id = $1", postID)
			} else {
				tx.Exec("UPDATE community_posts SET downvotes = GREATEST(0, downvotes - 1), votes = votes + 1 WHERE id = $1", postID)
			}
		} else {
			// CASE 3: INVERSION - Switched polarities. Atomically shift matrices.
			tx.Exec("UPDATE community_votes SET vote = $3 WHERE post_id = $1 AND user_id = $2", postID, userID, req.Delta)
			if req.Delta == 1 {
				// Swapping Down to Up: Up+1, Down-1, TotalNet +2
				tx.Exec("UPDATE community_posts SET upvotes = upvotes + 1, downvotes = GREATEST(0, downvotes - 1), votes = votes + 2 WHERE id = $1", postID)
			} else {
				// Swapping Up to Down: Up-1, Down+1, TotalNet -2
				tx.Exec("UPDATE community_posts SET upvotes = GREATEST(0, upvotes - 1), downvotes = downvotes + 1, votes = votes - 2 WHERE id = $1", postID)
			}
		}
	}

	if err := tx.Commit(); err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Final synchronization failed"})
	}

	return c.JSON(fiber.Map{"status": "rating_propagated"})
}

func uploadAsset(c *fiber.Ctx) error {
	file, err := c.FormFile("file")
	if err != nil {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Missing attachment payload"})
	}

	src, err := file.Open()
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Could not initialize stream reading"})
	}
	defer src.Close()

	// Create unique collision-resistant key
	t := time.Now().UnixNano()
	objectName := fmt.Sprintf("img_%d_%s", t, file.Filename)
	bucket := "community-assets"

	// Deploy directly onto MinIO grid
	ctx := context.Background()
	err = storage.Storage.Upload(ctx, bucket, objectName, src, file.Size, file.Header.Get("Content-Type"))
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Storage write failure"})
	}

	// Construct the definitive public asset locater URI (accessible from client browser)
	assetURL := fmt.Sprintf("http://localhost:9000/%s/%s", bucket, objectName)
	
	return c.JSON(fiber.Map{
		"url": assetURL,
		"name": objectName,
	})
}

// DISCUSSION LOGIC SUBSPACE

type CommunityComment struct {
	ID        string    `json:"id"`
	PostID    string    `json:"post_id"`
	Username  string    `json:"username"`
	Content   string    `json:"content"`
	ParentID  *string   `json:"parent_id"`
	CreatedAt time.Time `json:"created_at"`
}

type NewCommentRequest struct {
	Content  string  `json:"content"`
	ParentID *string `json:"parent_id"`
}

func listComments(c *fiber.Ctx) error {
	postID := c.Params("id")
	rows, err := database.DB.Query(`SELECT id, post_id, username, content, parent_id, created_at FROM community_comments WHERE post_id = $1 ORDER BY created_at ASC`, postID)
	if err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Trace fault aggregating replies"})
	}
	defer rows.Close()

	comments := []CommunityComment{}
	for rows.Next() {
		var cm CommunityComment
		if err := rows.Scan(&cm.ID, &cm.PostID, &cm.Username, &cm.Content, &cm.ParentID, &cm.CreatedAt); err == nil {
			comments = append(comments, cm)
		}
	}
	return c.JSON(comments)
}

func createComment(c *fiber.Ctx) error {
	postID := c.Params("id")
	userID := c.Locals("user_id").(string)

	var req NewCommentRequest
	if err := c.BodyParser(&req); err != nil || req.Content == "" {
		return c.Status(fiber.StatusBadRequest).JSON(fiber.Map{"error": "Echo payload incomplete"})
	}

	// Identify operator
	var username string
	database.DB.QueryRow("SELECT username FROM users WHERE id = $1", userID).Scan(&username)

	// Log Response
	query := `INSERT INTO community_comments (post_id, user_id, username, content, parent_id) VALUES ($1, $2, $3, $4, $5)`
	if _, err := database.DB.Exec(query, postID, userID, username, req.Content, req.ParentID); err != nil {
		return c.Status(fiber.StatusInternalServerError).JSON(fiber.Map{"error": "Reaction storage failure"})
	}

	// Increment master node counter
	database.DB.Exec(`UPDATE community_posts SET comments = comments + 1 WHERE id = $1`, postID)

	return c.Status(fiber.StatusCreated).JSON(fiber.Map{"status": "echo_locked"})
}

func getPostDetail(c *fiber.Ctx) error {
	uID := getOptionalUserID(c)
	postID := c.Params("id")
	var p CommunityPost
	
	query := `
		SELECT id, username, title, COALESCE(content, ''), votes, upvotes, downvotes, comments, created_at,
		       COALESCE((SELECT vote FROM community_votes WHERE post_id = community_posts.id AND user_id = $1), 0) as user_vote
		FROM community_posts 
		WHERE id = $2
	`
	err := database.DB.QueryRow(query, uID, postID).Scan(&p.ID, &p.Username, &p.Title, &p.Content, &p.Votes, &p.Upvotes, &p.Downvotes, &p.Comments, &p.CreatedAt, &p.UserVote)
	if err != nil {
		return c.Status(fiber.StatusNotFound).JSON(fiber.Map{"error": "Node address invalidated or non-existent."})
	}
	return c.JSON(p)
}
