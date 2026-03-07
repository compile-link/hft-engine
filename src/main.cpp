#include "market_data.pb.h"
#include "source_factory.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

extern "C" int32_t rust_decide(const uint8_t* ptr, size_t len);

static const char* action_to_str(int32_t a) {
    switch (a) {
    case hft::HOLD:
        return "HOLD";
    case hft::QUOTE_BOTH:
        return "QUOTE_BOTH";
    case hft::CANCEL_ALL:
        return "CANCEL_ALL";
    default:
        return "UNSPECIFIED";
    }
}

int main(int argc, char* argv[]) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    int exit_code = 0;
    try {
        std::string source = "replay"; // Default
                                       //
        // Parse command-line arguments, supports --replay and --source
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--source" && i + 1 < argc) {
                source = argv[++i];
            }
        }

        std::unique_ptr<MarketDataSource> mds;
        if (source == "replay") {
            mds = utils::make_source(utils::SourceType::Replay);
        } else if (source == "live") {
            mds = utils::make_source(utils::SourceType::Live);
        }

        if (!mds) {
            throw std::invalid_argument("Invalid source");
        }

        hft::TopOfBook tob;
        std::string payload;

        while (mds->next(tob)) {
            tob.SerializeToString(&payload);

            int32_t action = rust_decide(
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

            std::cout << "Strategy action: " << action_to_str(action) << " (" << action << ")\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        exit_code = 1;
    } catch (...) {
        std::cerr << "Error occurred\n";
        exit_code = 1;
    }

    google::protobuf::ShutdownProtobufLibrary();

    return exit_code;
}
