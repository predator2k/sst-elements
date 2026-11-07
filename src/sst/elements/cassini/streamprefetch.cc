#include "sst_config.h"
#include "streamprefetch.h"

#include <sst/core/params.h>

using namespace SST;
using namespace SST::MemHierarchy;
using namespace SST::Cassini;

StreamPrefetcher::StreamPrefetcher(ComponentId_t id, Params& params) : CacheListener(id, params)
{
    requireLibrary("memHierarchy");

    line_bytes_ = params.find<uint64_t>("cache_line_size", 64);
    lines_per_page_ = params.find<uint64_t>("page_size", 4096) / line_bytes_;
    degree_ = params.find<int64_t>("degree", 4);
    streams_.resize(params.find<uint32_t>("stream_entries", 32));
    train_on_writes_ = params.find<bool>("train_on_writes", true);

    stat_prefetches_ = registerStatistic<uint64_t>("prefetches_issued");
}

StreamPrefetcher::Stream&
StreamPrefetcher::streamOf(const uint64_t page, const int64_t line)
{
    Stream* oldest = &streams_[0];
    Stream* neighbour = nullptr;
    for ( Stream& stream : streams_ ) {
        if ( stream.valid && stream.page == page ) return stream;
        if ( ! stream.valid || stream.last_use < oldest->last_use ) oldest = &stream;
        // A confirmed stream whose next line falls in this page goes on here.
        if ( stream.valid && stream.confirmed && (stream.last_line + stream.stride) / static_cast<int64_t>(lines_per_page_)
             == static_cast<int64_t>(page) ) {
            neighbour = &stream;
        }
    }
    const int64_t stride = neighbour ? neighbour->stride : 0;
    const int64_t fetched_to = neighbour ? neighbour->fetched_to : line;
    const bool confirmed = neighbour != nullptr;
    *oldest = Stream{};
    oldest->page = page;
    oldest->last_line = line - stride;
    oldest->stride = stride;
    oldest->fetched_to = fetched_to;
    oldest->valid = true;
    oldest->confirmed = confirmed;
    return *oldest;
}

void
StreamPrefetcher::fetchAhead(Stream& stream, const int64_t line)
{
    const int64_t page_first = static_cast<int64_t>(stream.page * lines_per_page_);
    const int64_t page_last = page_first + static_cast<int64_t>(lines_per_page_) - 1;
    for ( int64_t k = 1; k <= degree_; ++k ) {
        const int64_t target = line + k * stream.stride;
        if ( target < page_first || target > page_last ) break;
        const bool already = stream.stride > 0 ? target <= stream.fetched_to : target >= stream.fetched_to;
        if ( already ) continue;
        stream.fetched_to = target;
        const Addr address = static_cast<Addr>(target) * line_bytes_;
        for ( Event::HandlerBase* callback : registered_callbacks_ ) {
            MemEvent* event = new MemEvent(getName(), address, address, Command::GetS);
            event->setSize(line_bytes_);
            event->setPrefetchFlag(true);
            stat_prefetches_->addData(1);
            (*callback)(event);
        }
    }
}

void
StreamPrefetcher::notifyAccess(const CacheListenerNotification& notify)
{
    const NotifyAccessType type = notify.getAccessType();
    if ( type != READ && type != WRITE ) return;
    if ( type == WRITE && ! train_on_writes_ ) return;

    const int64_t line = static_cast<int64_t>(notify.getPhysicalAddress() / line_bytes_);
    const uint64_t page = static_cast<uint64_t>(line) / lines_per_page_;

    Stream& stream = streamOf(page, line);
    stream.last_use = ++use_clock_;

    const int64_t stride = line - stream.last_line;
    if ( stride == 0 ) return;
    stream.confirmed = stride == stream.stride;
    if ( ! stream.confirmed ) {
        stream.stride = stride;
        stream.fetched_to = line;
    }
    stream.last_line = line;
    if ( stream.confirmed ) fetchAhead(stream, line);
}

void
StreamPrefetcher::registerResponseCallback(Event::HandlerBase* handler)
{
    registered_callbacks_.push_back(handler);
}

void
StreamPrefetcher::printStats(Output& out)
{
}

void
StreamPrefetcher::serialize_order(SST::Core::Serialization::serializer& ser)
{
    CacheListener::serialize_order(ser);
    SST_SER(registered_callbacks_);
    SST_SER(streams_);
    SST_SER(line_bytes_);
    SST_SER(lines_per_page_);
    SST_SER(degree_);
    SST_SER(use_clock_);
    SST_SER(stat_prefetches_);
}
