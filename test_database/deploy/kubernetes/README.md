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
