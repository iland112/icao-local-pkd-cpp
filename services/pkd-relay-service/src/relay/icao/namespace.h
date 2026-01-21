#pragma once

/**
 * @file namespace.h
 * @brief Centralized namespace definitions for ICAO Relay module
 *
 * This file provides a single source of truth for namespace structure
 * and common type aliases used throughout the ICAO module.
 */

// Forward declarations
namespace icao {
namespace relay {
namespace icao_module {
namespace domain {
namespace models {
    struct IcaoVersion;
}
}
namespace repositories {
    class IcaoVersionRepository;
}
namespace services {
    class IcaoSyncService;
}
namespace handlers {
    class IcaoHandler;
}
namespace infrastructure {
namespace http {
    class HttpClient;
}
namespace notification {
    class EmailSender;
}
}
namespace utils {
    class HtmlParser;
}
}}}

// Convenient aliases for use within the module
namespace icao_relay = icao::relay::icao_module;
