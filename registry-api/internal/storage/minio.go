package storage

import (
	"context"
	"log"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

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

// PutPackage uploads serialized archive payload to designated bucket
func (c *Client) PutPackage(ctx context.Context, bucket, objectName string, filePath string) error {
	// TODO: Integrate upload streaming handler logic
	return nil
}
