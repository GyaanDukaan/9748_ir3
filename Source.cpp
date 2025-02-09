#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <cassert>
#include <thread>
#include <chrono>

template <typename K, typename V>
struct Order {
    std::atomic<V> lotSize;  // Use atomic for lotSize to support thread-safe operations
    int price;

    // Default constructor
    Order() : lotSize(0), price(0) {}

    // Constructor with parameters
    Order(V lotSize, int price) : lotSize(lotSize), price(price) {}

    // Disable copy constructor and assignment operator
    Order(const Order& other) = delete;
    Order& operator=(const Order& other) = delete;

    // Move constructor and move assignment operator
    Order(Order&& other) noexcept : price(other.price) {
        lotSize.store(other.lotSize.load(), std::memory_order_relaxed);  // Correct atomic transfer
    }

    Order& operator=(Order&& other) noexcept {
        if (this != &other) {
            price = other.price;
            lotSize.store(other.lotSize.load(), std::memory_order_relaxed);  // Correct atomic transfer
        }
        return *this;
    }

    // Destructor: no dynamic memory allocation, no need to clean up
    ~Order() = default;
};

template <typename K, typename V>
class ConcurrentHashMap {
public:
    // Insert or update an order based on the symbol and price
    void insert(const K& symbol, Order<K, V>&& order) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto& orders = map_[symbol];

        // Try to find an order with the same price
        auto it = orders.find(order.price);
        if (it != orders.end()) {
            // Found an existing order, so update the lotSize atomically
            it->second.lotSize.fetch_add(order.lotSize.load(), std::memory_order_relaxed);
        } else {
            // Insert the new order if price doesn't exist
            orders[order.price] = std::move(order);
        }
    }

    // Remove all orders associated with the symbol
    void remove(const K& symbol) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_.erase(symbol);
    }

    // Display all orders in the map
    void display() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& pair : map_) {
            std::cout << pair.first << ": ";
            for (const auto& order : pair.second) {
                std::cout << "{lotSize: " << order.second.lotSize.load() << ", price: " << order.first << "} ";
            }
            std::cout << std::endl;
        }
    }

    // Run all test cases
    void test() {
        assert(testInsert());
        assert(testRemove());
        assert(testDisplay());
        assert(testConcurrentAccess());  // New test for concurrency
    }

private:
    std::unordered_map<K, std::unordered_map<int, Order<K, V>>> map_;  // Symbol -> (Price -> Order)
    mutable std::shared_mutex mutex_;  // Shared mutex for concurrent read/write access

    // Test case for inserting orders
    bool testInsert() {
        insert("TEST", Order<K, V>(10, 2));
        {
            const auto& orders = map_.at("TEST");
            assert(orders.size() == 1);
            assert(orders.at(2).lotSize.load() == 10);
        }
        insert("TEST", Order<K, V>(20, 2));  // Should add to existing lotSize
        {
            const auto& orders = map_.at("TEST");
            assert(orders.size() == 1);
            assert(orders.at(2).lotSize.load() == 30);  // lotSize should be 30
        }
        insert("TEST", Order<K, V>(5, 3));  // New order with different price
        {
            const auto& orders = map_.at("TEST");
            assert(orders.size() == 2);  // Two different prices
            assert(orders.at(2).lotSize.load() == 30);  // Existing price order
            assert(orders.at(3).lotSize.load() == 5);   // New price order
        }
        return true;
    }

    // Test case for removing orders
    bool testRemove() {
        insert("TEST", Order<K, V>(10, 2));
        remove("TEST");
        assert(map_.find("TEST") == map_.end());  // "TEST" should be removed
        return true;
    }

    // Test case for displaying orders
    bool testDisplay() const {
        insert("TEST", Order<K, V>(10, 2));
        insert("TEST", Order<K, V>(15, 3));
        display();  // Should not assert, just display
        return true;
    }

    // Test case for concurrent access (new)
    bool testConcurrentAccess() {
        const int numThreads = 10;
        const std::string symbol = "CONCURRENCY_TEST";
        std::vector<std::thread> threads;

        // Spawn multiple threads to perform insertions and updates concurrently
        for (int i = 0; i < numThreads; ++i) {
            threads.push_back(std::thread([&, i]() {
                insert(symbol, Order<K, V>(i + 1, 2));  // Incremental lotSize for testing
            }));
        }

        // Wait for all threads to finish
        for (auto& t : threads) {
            t.join();
        }

        // Verify the final state of the orders
        const auto& orders = map_.at(symbol);
        assert(orders.size() == 1);  // Only one price (2)
        assert(orders.at(2).lotSize.load() == (numThreads * (numThreads + 1)) / 2);  // Sum of 1..numThreads
        return true;
    }
};

int main() {
    ConcurrentHashMap<std::string, int> concurrentMap;

    // Sample symbols and initial orders
    std::vector<std::string> symbols = {
        "NESTLEIND", "HDFCBANK", "RELIANCE", "TCS", "INFY",
        "SBIN", "ICICIBANK", "LT", "BAJFINANCE", "HINDUNILVR"
    };

    // Insert initial orders
    for (const auto& symbol : symbols) {
        concurrentMap.insert(symbol, Order<std::string, int>(10, 2));
    }

    // Test adding to existing order and adding new order
    concurrentMap.insert("NESTLEIND", Order<std::string, int>(20, 2)); // This should add to the existing lotSize
    concurrentMap.insert("HDFCBANK", Order<std::string, int>(15, 4));  // This should add a new order

    // Display current orders
    concurrentMap.display();

    // Remove an order
    concurrentMap.remove("NESTLEIND");

    // Display after removal
    concurrentMap.display();

    // Run test cases
    concurrentMap.test();

    return 0;
}
