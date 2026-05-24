# Shared Ingress Deployment

This manifest deploys Nav Registry into the existing Kubernetes cluster without
claiming host ports `80` or `443`. The installed ingress-nginx controller and
cert-manager `letsencrypt-prod` issuer terminate HTTPS for
`nav.navrobotec.online`.

The manifest deliberately does not contain secrets. Create the secret from a
server-local `.env` file before applying the workloads:

```bash
kubectl create namespace nav-registry --dry-run=client -o yaml | kubectl apply -f -
kubectl -n nav-registry create secret generic nav-registry-secrets \
  --from-env-file=.env --dry-run=client -o yaml | kubectl apply -f -
kubectl -n nav-registry create configmap nav-database-init \
  --from-file=001-init.sql=database/init.sql --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f deploy/kubernetes/nav-registry.yaml
```

On this server the application images are built locally and imported into k3s:

```bash
docker build -f backend/Dockerfile -t nav-registry-backend:dev .
docker build -f frontend/Dockerfile.production \
  --build-arg VITE_API_BASE=https://nav.navrobotec.online/api \
  -t nav-registry-frontend:dev frontend
docker save nav-registry-backend:dev | k3s ctr images import -
docker save nav-registry-frontend:dev | k3s ctr images import -
```

The backend ingress strips `/api` before forwarding requests, matching the API
routes and OAuth callback URLs used by the application.

## Development Artifact Catalog

This deployment intentionally mirrors a compact set of real artifacts rather
than every SDK and vendor archive. It is enough to validate package resolution,
checksums, extraction, compilation, and flashing while the registry is in
development. Add larger SDK families only after MinIO is moved to dedicated or
external object storage with capacity monitoring and backups.

## Scaling Boundary

The backend stores OAuth session state in Postgres, so a restart no longer
loses sign-in state. Horizontal pod autoscaling is deliberately not enabled on
the current development host: its only AMD64 schedulable node already has all
allocatable CPU reserved by existing workloads. Add CPU capacity or publish
multi-architecture images for available workers before enabling an HPA.

The backend also remains configured at one replica while startup catalog
seeding is coupled to the web process.

Postgres and MinIO use single-node persistent volumes in this development
cluster. They persist data, but they are not highly available. Before a public
production launch, use managed or replicated Postgres and S3-compatible object
storage, publish multi-architecture images to a registry, and move catalog
seeding into a single-run deployment job before horizontally scaling the
backend.
