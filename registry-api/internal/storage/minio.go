package storage

import (
	"context"
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
		log.Printf("Provisioning primary storage bucket: %s\n", bucketName)
		err = c.mc.MakeBucket(ctx, bucketName, minio.MakeBucketOptions{})
		if err != nil {
			return err
		}
	}
	return nil
}

// GeneratePresignedUploadURL issues restricted, time-bound vector for direct binary push
func (c *Client) GeneratePresignedUploadURL(ctx context.Context, bucketName, objectName string, expiry time.Duration) (string, error) {
	presignedURL, err := c.mc.PresignedPutObject(ctx, bucketName, objectName, expiry)
	if err != nil {
		return "", err
	}
	
	return presignedURL.String(), nil
}
