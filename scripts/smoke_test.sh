#!/usr/bin/env bash
set -e

echo "[INFO] Initializing Nav Integration Smoke Test Framework..."

API_URL="http://localhost:8081"
TEST_TOKEN="nav_testvector_secure"

echo "[STEP 1] Seeding Relational Space with Regression Data..."
# Safely inject standard UUID and static token direct to running container instance
docker exec -i nav-db psql -U nav_admin -d nav_registry < $(dirname "$0")/../infra/postgres/seed_test_data.sql > /dev/null
echo "   [OK] Seed Injected Successfully."

echo -e "\n[STEP 2] Executing Basic Health Reachability Scan..."
HEALTH_RESP=$(curl -s $API_URL/health)
echo "   [RECV] API Response: $HEALTH_RESP"
if [[ $HEALTH_RESP == *"healthy"* ]]; then
    echo "   [OK] Target Alive."
else
    echo "   [ERROR] Failed: Service Unreachable or Unhealthy."
    exit 1
fi

echo -e "\n[STEP 3] Verification: Negatory Firewall Barrier (Blocked Request)..."
STATUS_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST $API_URL/api/v1/publish)
echo "   [RECV] Unauthenticated Endpoint returned HTTP $STATUS_CODE"
if [[ "$STATUS_CODE" == "401" ]]; then
    echo "   [OK] Correctly Refused Access."
else
    echo "   [ERROR] FAILED: Firewall Leaking (Expected 401, Got $STATUS_CODE)"
    exit 1
fi

echo -e "\n[STEP 4] Verification: Affirmative Authorization Vector (Access Granted)..."
# We utilize the seeded pre-hashed vector. API should derive correct SHA256 match and grant pass-through
STATUS_CODE_AUTH=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "Authorization: Bearer $TEST_TOKEN" $API_URL/api/v1/publish)
echo "   [RECV] Authenticated Endpoint returned HTTP $STATUS_CODE_AUTH"

# Current implementation yields 501 (Not Implemented) for valid routes passing middleware
if [[ "$STATUS_CODE_AUTH" == "501" ]] || [[ "$STATUS_CODE_AUTH" == "200" ]]; then
    echo "   [OK] Authentication Shield Authenticated Effectively!"
else
    echo "   [ERROR] FAILED: Security Gateway Rejected Legitimate Payload (Got $STATUS_CODE_AUTH)"
    exit 1
fi

echo -e "\n[STEP 5] Verification: Object Storage Lifecycle (Presigned Lease)..."
LEASE_JSON='{"namespace":"navrobotec","name":"smoke-test-pkg","version":"1.0.0"}'
LEASE_RESP=$(curl -s -X POST -H "Authorization: Bearer $TEST_TOKEN" -H "Content-Type: application/json" -d "$LEASE_JSON" $API_URL/api/v1/publish/init)
echo "   [RECV] Presigned Response Length: ${#LEASE_RESP} characters"

if [[ $LEASE_RESP == *"upload_url"* ]] && [[ $LEASE_RESP == *"http"* ]]; then
    echo "   [OK] Storage System successfully generated lease vector!"
else
    echo "   [ERROR] FAILED: Storage Handshake collapsed or returned invalid JSON."
    echo "           Response Data: $LEASE_RESP"
    exit 1
fi

echo -e "\n[DONE] COMPLETED: ALL INTEGRATION CHANNELS FUNCTIONAL."
