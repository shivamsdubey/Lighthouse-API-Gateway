#include <iostream>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <cstdlib> // for getenv
#include "TrieRouter.hpp"
#include "Middleware.hpp"
#include "RateLimiter.hpp"

// --- New Feature: Round-Robin Load Balancer ---
class LoadBalancer {
private:
    std::vector<std::string> backendServers;
    size_t currentServerIndex;
public:
    LoadBalancer() : currentServerIndex(0) {
        // Servers config file se load karne ki option
        std::ifstream srvFile("../servers.txt");
        if (srvFile) {
            std::string url;
            while (srvFile >> url) backendServers.push_back(url);
        }
        if (backendServers.empty()) {
            // Fallback demo servers
            backendServers = {"http://srv-prod-01.internal", "http://srv-prod-02.internal", "http://srv-prod-03.internal"};
        }
    }

    std::string getNextServer(const std::string& originalBackend) {
        if (originalBackend == "NOT_FOUND") return originalBackend;
        std::string selectedServer = backendServers[currentServerIndex];
        currentServerIndex = (currentServerIndex + 1) % backendServers.size();
        return originalBackend + " -> [Balanced To: " + selectedServer + "]";
    }
};

// --- 1. IP Whitelist Middleware ---
class IPWhitelistMiddleware : public IMiddleware {
private:
    std::set<std::string> allowedIPs;
public:
    IPWhitelistMiddleware() {
        std::ifstream ipFile("../whitelist.txt");
        std::string ip;
        while (ipFile >> ip) allowedIPs.insert(ip);
        if (allowedIPs.empty()) {
            // Fallback demo IPs
            allowedIPs = {"127.0.0.1"};
        }
    }

    bool execute(const std::string& path) override {
        std::string currentIP = getenv("REQUEST_IP") ? getenv("REQUEST_IP") : "127.0.0.1";
        if (allowedIPs.find(currentIP) != allowedIPs.end()) {
            std::cout << "[IP CHECK]: Access Granted for " << currentIP << std::endl;
            return true;
        }
        std::cout << "[SECURITY]: IP " << currentIP << " is BLOCKED!" << std::endl;
        return false;
    }
};

// --- 2. Advanced JWT Authentication Middleware ---
class JWTAuthMiddleware : public IMiddleware {
private:
    std::string validToken;
public:
    JWTAuthMiddleware() {
        const char* token = getenv("VALID_JWT_TOKEN");
        validToken = token ? token : "";
    }

    bool execute(const std::string& path) override {
        const char* incoming = getenv("INCOMING_JWT_TOKEN");
        std::string incomingToken = incoming ? incoming : "";

        if (incomingToken.empty()) {
            std::cout << "[AUTH ERROR]: 401 Unauthorized - Missing JWT Token!" << std::endl;
            return false;
        }
        if (incomingToken != validToken) {
            std::cout << "[AUTH ERROR]: 403 Forbidden - Invalid/Expired Token!" << std::endl;
            return false;
        }

        std::cout << "[JWT AUTH]: Token Verified Successfully. User Authorized." << std::endl;
        return true;
    }
};

// --- 3. Response Caching Middleware ---
class CacheMiddleware : public IMiddleware {
private:
    std::map<std::string, std::string> cache;
public:
    bool execute(const std::string& path) override {
        if (cache.count(path)) {
            std::cout << "[CACHE HIT]: Returning saved data for " << path << std::endl;
            return false;
        }
        cache[path] = "Success Response Data";
        std::cout << "[CACHE MISS]: Data fetched and stored for " << path << std::endl;
        return true;
    }
};

// Config file loader
void loadConfig(TrieRouter& router, const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "[ERROR]: Config file missing! Using default mock fallback." << std::endl;
        router.addRoute("/api/payments", "PaymentMicroserviceCluster");
        return;
    }
    std::string path, backend;
    while (file >> path >> backend) {
        router.addRoute(path, backend);
        std::cout << "[CONFIG]: Loaded " << path << std::endl;
    }
}

int main() {
    TrieRouter router;
    LoadBalancer balancer;

    std::cout << "--- Initializing Gateway v6.0 (Enterprise Auth & Load Balancing) ---" << std::endl;
    loadConfig(router, "../config.txt");

    std::vector<std::unique_ptr<IMiddleware>> chain;
    chain.push_back(std::make_unique<IPWhitelistMiddleware>());
    chain.push_back(std::make_unique<JWTAuthMiddleware>());
    chain.push_back(std::make_unique<CacheMiddleware>());
    chain.push_back(std::make_unique<LoggerMiddleware>());
    chain.push_back(std::make_unique<AuthMiddleware>());

    RateLimiter limiter(3, 10);

    std::cout << "\n--- API Gateway Live ---" << std::endl;
    std::string testPath = "/api/payments";

    std::cout << "[REQUEST 1]: Processing standard lifecycle..." << std::endl;
    if (limiter.isAllowed()) {
        bool continueChain = true;
        for (auto& mw : chain) {
            if (!mw->execute(testPath)) { continueChain = false; break; }
        }
        if (continueChain) {
            std::string backend = router.getBackend(testPath);
            if (backend != "NOT_FOUND") {
                std::string balancedBackend = balancer.getNextServer(backend);
                std::cout << "[GATEWAY]: Forwarding to -> " << balancedBackend << std::endl;
            } else {
                std::cout << "[404]: Path Not Registered!" << std::endl;
            }
        }
    }

    std::cout << "\n--- Retrying same request (Testing Cache Engine) ---" << std::endl;
    for (auto& mw : chain) {
        if (!mw->execute(testPath)) break;
    }

    std::cout << "\n--- New Request Triggered (Testing Round-Robin Balance Shift) ---" << std::endl;
    std::string backendInstance = router.getBackend(testPath);
    std::cout << "[GATEWAY]: Forwarding to -> " << balancer.getNextServer(backendInstance) << std::endl;

    return 0;
}
