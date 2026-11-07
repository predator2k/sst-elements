// A stream prefetcher: one entry per recently touched page finds the stride between lines, in either direction, and after a
// stride has repeated fetches the next lines along it inside the page. A stream that runs off a page continues in the
// neighbouring page without retraining.

#ifndef _H_SST_STREAM_PREFETCH
#define _H_SST_STREAM_PREFETCH

#include <cstdint>
#include <vector>

#include <sst/core/event.h>
#include <sst/core/sst_types.h>
#include <sst/core/component.h>
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/elements/memHierarchy/cacheListener.h>

namespace SST {
namespace Cassini {

class StreamPrefetcher : public SST::MemHierarchy::CacheListener {
public:
    StreamPrefetcher(ComponentId_t id, Params& params);
    ~StreamPrefetcher() override = default;

    void notifyAccess(const SST::MemHierarchy::CacheListenerNotification& notify) override;
    void registerResponseCallback(Event::HandlerBase* handler) override;
    void printStats(Output& out) override;

    SST_ELI_REGISTER_SUBCOMPONENT(
        StreamPrefetcher,
        "cassini",
        "StreamPrefetcher",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "One stride per page, in either direction, with a fetch distance in lines",
        SST::MemHierarchy::CacheListener
    )

    SST_ELI_DOCUMENT_PARAMS(
        { "cache_line_size", "Size of the cache line the prefetcher is attached to", "64" },
        { "page_size", "Prefetches stay inside this page size", "4096" },
        { "stream_entries", "Pages tracked at once", "32" },
        { "degree", "Lines ahead of the access to fetch", "4" },
        { "train_on_writes", "Writes train the streams and issue prefetches as reads do; false lets only reads do it", "true" }
    )

    SST_ELI_DOCUMENT_STATISTICS(
        { "prefetches_issued", "Number of prefetch requests issued", "prefetches", 1 }
    )

    StreamPrefetcher() : SST::MemHierarchy::CacheListener() {} // For serialization
    void serialize_order(SST::Core::Serialization::serializer& ser) override;
    ImplementSerializable(SST::Cassini::StreamPrefetcher)

private:
    //! One tracked page. Line numbers count lines from address zero.
    struct Stream
    {
        uint64_t page = 0;
        int64_t last_line = 0;
        int64_t stride = 0;
        int64_t fetched_to = 0;
        uint64_t last_use = 0;
        bool valid = false;
        bool confirmed = false;

        void serialize_order(SST::Core::Serialization::serializer& ser)
        {
            SST_SER(page);
            SST_SER(last_line);
            SST_SER(stride);
            SST_SER(fetched_to);
            SST_SER(last_use);
            SST_SER(valid);
            SST_SER(confirmed);
        }
    };

    Stream& streamOf(uint64_t page, int64_t line);
    void fetchAhead(Stream& stream, int64_t line);

    std::vector<Event::HandlerBase*> registered_callbacks_;
    std::vector<Stream> streams_;
    uint64_t line_bytes_ = 64;
    uint64_t lines_per_page_ = 64;
    int64_t degree_ = 4;
    // train_on_writes: writes train the streams and issue prefetches.
    bool train_on_writes_ = true;
    uint64_t use_clock_ = 0;

    Statistic<uint64_t>* stat_prefetches_ = nullptr;
};

}
}

#endif
