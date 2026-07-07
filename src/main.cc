// SPDX-License-Identifier: GPL-2.0-or-later

#include <fcntl.h>
#include <unistd.h>

#include <CLI/CLI.hpp>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "core/caption_dumper.hpp"
#include "core/output_record_sink.hpp"
#include "core/packet_framer.hpp"

namespace {

// RAII guard that closes a file descriptor on destruction.
class ScopedFd {
   public:
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() {
        ::close(fd_);
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

   private:
    int fd_;
};

[[nodiscard]] int Run(std::uint16_t sid, const std::string& file,
                      aribcap_dump::CaptionDumperOptions options) {
    int input_fd = STDIN_FILENO;
    std::optional<ScopedFd> scoped_fd;

    if (!file.empty()) {
        const int fd = ::open(file.c_str(), O_RDONLY);

        if (fd < 0) {
            std::cerr << "aribcap-dump: failed to open " << file << '\n';

            return 1;
        }

        scoped_fd.emplace(fd);
        input_fd = fd;
    }

    aribcap_dump::JsonlOutputRecordSink sink(std::cout);
    aribcap_dump::CaptionDumper caption_dumper(sid, sink, options);
    const aribcap_dump::ReadFrameStatus status =
        aribcap_dump::ReadAndFramePackets(input_fd, [&](const auto& packet) {
            caption_dumper.FeedPacket(packet);

            return std::cout.good();
        });

    if (std::cout.good()) {
        caption_dumper.Flush();
    }

    if (!std::cout.good()) {
        std::cerr << "aribcap-dump: failed to write JSONL output\n";

        return 1;
    }

    switch (status) {
        case aribcap_dump::ReadFrameStatus::kOk:
            return 0;
        case aribcap_dump::ReadFrameStatus::kNoSync:
            std::cerr << "aribcap-dump: could not confirm a TS sync byte sequence near the start; "
                         "input may be truncated or not an MPEG-TS stream\n";

            return 1;
        case aribcap_dump::ReadFrameStatus::kFailed:
            std::cerr << "aribcap-dump: failed while reading TS input\n";

            return 1;
    }

    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"Collect and decode ARIB captions and EIT as JSONL"};
    std::uint16_t sid = 0;
    std::string file;
    bool emit_empty_captions = false;
    app.add_option("--sid", sid, "Service id to collect")->required()->check(CLI::Range(1, 65535));
    app.add_flag("--emit-empty-captions", emit_empty_captions,
                 "Emit caption records even when decoded text is empty");
    app.add_option("file", file, "Path to a TS file. Reads from stdin when omitted");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& err) {
        return app.exit(err);
    }

    return Run(sid, file,
               aribcap_dump::CaptionDumperOptions{
                   .emit_empty_captions = emit_empty_captions,
               });
}
