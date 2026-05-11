package storage

import (
	"context"
	"io"
	"log"
	"os"
	"time"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

var Storage *Client

// Connect reads global configurations and stabilizes persistent socket pool
func Connect() {
	endpoint := getEnv("MINIO_ENDPOINT", "localhost:9000")
	accessKey := getEnv("MINIO_ACCESS_KEY", "navroot")
	secretKey := getEnv("MINIO_SECRET_KEY", "navpassword123")
	
	var err error
	Storage, err = NewMinioClient(endpoint, accessKey, secretKey, false)
	if err != nil {
		log.Fatalf("Failure latching object storage conduit: %v", err)
	}

	// Verify and Provision Baseline Vault
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	
	if err := Storage.EnsureBucket(ctx, "nav-packages"); err != nil {
		log.Fatalf("Failed securing critical 'nav-packages' bucket repository: %v", err)
	}

	if err := Storage.EnsureBucket(ctx, "community-assets"); err != nil {
		log.Printf("Warning: Community-assets bucket initialization warning: %v", err)
	}
	
	log.Println("Storage pipeline solidified and locked.")
}

func getEnv(key, fallback string) string {
	if value, exists := os.LookupEnv(key); exists {
		return value
	}
	return fallback
}

type Client struct {
	mc *minio.Client
}

// NewMinioClient establishes raw socket connection to underlying storage system
func NewMinioClient(endpoint, accessKey, secretKey string, useSSL bool) (*Client, error) {
	minioClient, err := minio.New(endpoint, &minio.Options{
		Creds:  credentials.NewStaticV4(accessKey, secretKey, ""),
		Secure: useSSL,
	})
	if err != nil {
		return nil, err
	}

	log.Printf("Successfully latched onto MinIO server at %s\n", endpoint)
	return &Client{mc: minioClient}, nil
}

// EnsureBucket validates presence of target namespace, creating it immediately if non-existent
func (c *Client) EnsureBucket(ctx context.Context, bucketName string) error {
	exists, err := c.mc.BucketExists(ctx, bucketName)
	if err != nil {
		return err
	}
	if !exists {
		log.Printf("Provisioning storage bucket: %s\n", bucketName)
		err = c.mc.MakeBucket(ctx, bucketName, minio.MakeBucketOptions{})
		if err != nil {
			return err
		}
		
		// Apply Public Read-Only Policy so images can be viewed directly
		policy := `{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"AWS":["*"]},"Action":["s3:GetObject"],"Resource":["arn:aws:s3:::` + bucketName + `/*"]}]}`
		c.mc.SetBucketPolicy(ctx, bucketName, policy)
	}
	return nil
}

// Upload synchronously delivers explicit data payload directly onto storage cluster
func (c *Client) Upload(ctx context.Context, bucketName, objectName string, reader io.Reader, objectSize int64, contentType string) error {
	_, err := c.mc.PutObject(ctx, bucketName, objectName, reader, objectSize, minio.PutObjectOptions{
		ContentType: contentType,
	})
	return err
}

// GeneratePresignedUploadURL issues restricted, time-bound vector for direct binary push
func (c *Client) GeneratePresignedUploadURL(ctx context.Context, bucketName, objectName string, expiry time.Duration) (string, error) {
	presignedURL, err := c.mc.PresignedPutObject(ctx, bucketName, objectName, expiry)
	if err != nil {
		return "", err
	}
	
	return presignedURL.String(), nil
}
