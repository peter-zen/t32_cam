#ifndef PRODUCTCONFIG_H
#define PRODUCTCONFIG_H

// ProductConfig — read-only bag of factory-burned product identifiers.
//
// Phase-2 (T24) of the config.ini -> JSON migration carves the BOOT section and
// the DEVICE-static fields (CSSID/CPWD/SPKVOL) out of DeviceConfig into this new
// class. The file it loads is product.json (path from the PRODUCT_FILE env,
// symmetric to DeviceConfig's CONFIG_FILE).
//
// Read-only is enforced structurally: there is NO set() or flush() method (not
// even private). The only writer is CameraFactoryConfigImporter, which writes
// product.json directly via jsoncpp and then calls reload() to refresh the
// in-memory bag — it cannot go through ProductConfig because ProductConfig has
// no write surface. PID stays on DeviceConfig (MCU-authoritative, rewritten on
// every shutdown by syncWithMCU), so ProductConfig's DEVICE section only holds
// CSSID/CPWD/SPKVOL.

#include <set>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <sstream>

class ProductConfig {
    public:
        static std::shared_ptr<ProductConfig> getInstance();
        int get(const std::string &section, const std::string &key,
                int default_value);
        std::string get(const std::string &section, const std::string &key,
                        const char* default_value);
        std::string get(const std::string &section, const std::string &key,
                        std::string default_value);

        // T28 capability presence-set (um_*). Loaded from the top-level
        // "capabilities" field in product.json (comma-separated). Absent or
        // empty -> fail-safe {"um_live"} (design um-capability-advertising §3.7).
        bool hasCap(const std::string &token) const;
        std::set<std::string> getCaps() const;

        void reload();

    private:
        ProductConfig();
        ProductConfig(const ProductConfig &) = delete;
        ProductConfig &operator=(const ProductConfig &) = delete;
        bool load(const std::string &configFile);
        void parseCapabilities(const std::string &raw);

        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> config_data;
        mutable std::mutex config_mutex;
        std::string config_filename;
        std::set<std::string> caps_set_;
};

#endif /* PRODUCTCONFIG_H */
