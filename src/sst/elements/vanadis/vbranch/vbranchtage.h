// Direction prediction with tagged geometric history tables (TAGE; Seznec and Michaud, "A case for (partially) tagged
// geometric history length branch predictors", JILP 2006), a target table and a return address stack.

#ifndef _H_VANADIS_BRANCH_TAGE
#define _H_VANADIS_BRANCH_TAGE

#include "vbranch/vbranchunit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace SST {
namespace Vanadis {

class VanadisTageBranchUnit : public VanadisBranchUnit {

public:
    SST_ELI_REGISTER_SUBCOMPONENT(
        VanadisTageBranchUnit,
    #ifdef VANADIS_BUILD_DEBUG
        "vanadisdbg",
    #else
        "vanadis",
    #endif
        "VanadisTageBranchUnit",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Predicts a conditional branch from a bimodal table and tagged tables indexed by the global branch history, a "
        "target from a table of last targets and a return from a return address stack",
        SST::Vanadis::VanadisBranchUnit)

    SST_ELI_DOCUMENT_PARAMS(
        { "branch_entries", "Entries of the table of last targets", "4096" },
        { "bimodal_entries", "Entries of the bimodal table, a power of two", "16384" },
        { "tagged_entries", "Entries of each tagged table, a power of two", "4096" },
        { "history_lengths", "Global history length of each tagged table, shortest first, up to 256 branches", "[4, 8, 16, 32, 64, 128, 256]" },
        { "return_stack_entries", "Entries of the return address stack", "32" })

    SST_ELI_DOCUMENT_STATISTICS(
        { "direction_predictions", "Conditional branches predicted", "branches", 1 },
        { "direction_mispredictions", "Conditional branches whose direction was wrong at retire", "branches", 1 })

    VanadisTageBranchUnit(ComponentId_t id, Params& params) : VanadisBranchUnit(id, params)
    {
        max_targets_ = params.find<uint32_t>("branch_entries", 4096);
        const uint32_t bimodal = params.find<uint32_t>("bimodal_entries", 16384);
        const uint32_t tagged = params.find<uint32_t>("tagged_entries", 4096);
        return_stack_entries_ = params.find<uint32_t>("return_stack_entries", 32);
        std::vector<uint32_t> lengths;
        params.find_array<uint32_t>("history_lengths", lengths);
        if ( lengths.empty() ) lengths = { 4, 8, 16, 32, 64, 128, 256 };

        output_ = new SST::Output("[tage]: ", 0, 0, SST::Output::STDOUT);
        if ( ! powerOfTwo(bimodal) || ! powerOfTwo(tagged) ) {
            output_->fatal(CALL_INFO, -1, "bimodal_entries and tagged_entries must be powers of two\n");
        }
        for ( const uint32_t length : lengths ) {
            if ( length == 0 || length > HISTORY_BITS ) {
                output_->fatal(CALL_INFO, -1, "a history length must be in 1 to %u\n", HISTORY_BITS);
            }
        }

        bimodal_.assign(bimodal, 0);
        bimodal_mask_ = bimodal - 1;
        tagged_mask_ = tagged - 1;
        index_bits_ = log2Of(tagged);
        tables_.resize(lengths.size());
        for ( size_t t = 0; t < lengths.size(); ++t ) {
            tables_[t].history_length = lengths[t];
            tables_[t].entries.assign(tagged, TaggedEntry{});
        }

        stat_predictions_ = registerStatistic<uint64_t>("direction_predictions", "1");
        stat_mispredictions_ = registerStatistic<uint64_t>("direction_mispredictions", "1");
    }

    ~VanadisTageBranchUnit() override { delete output_; }

    // A target learned at retire. The branch has resolved, so the retired state moves forward here.
    void push(const uint64_t ins_addr, const uint64_t next_addr) override
    {
        if ( ! in_flight_.empty() && in_flight_.front().address == ins_addr ) {
            const InFlight branch = in_flight_.front();
            in_flight_.pop_front();
            const bool taken = next_addr != branch.fall_through;
            train(branch, taken);
            shiftIn(retired_history_, taken);
            if ( taken ) rememberTarget(ins_addr, next_addr);
            return;
        }
        // Unconditional and indirect branches.
        if ( ! in_flight_unconditional_.empty() && in_flight_unconditional_.front().address == ins_addr ) {
            const InFlightOther other = in_flight_unconditional_.front();
            in_flight_unconditional_.pop_front();
            if ( VanadisBranchClass::CALL == other.branch_class ) {
                retired_returns_.push_back(other.fall_through);
                if ( retired_returns_.size() > return_stack_entries_ ) retired_returns_.pop_front();
            }
            else if ( VanadisBranchClass::RETURN == other.branch_class && ! retired_returns_.empty() ) {
                retired_returns_.pop_back();
            }
        }
        rememberTarget(ins_addr, next_addr);
    }

    uint64_t predictAddress(const uint64_t addr) override
    {
        const auto found = targets_.find(addr);
        return found == targets_.end() ? 0 : found->second;
    }

    bool contains(const uint64_t addr) override { return targets_.find(addr) != targets_.end(); }

    uint64_t predictNext(const uint64_t addr, const uint64_t fall_through, const VanadisBranchClass branch_class) override
    {
        switch ( branch_class ) {
        case VanadisBranchClass::CONDITIONAL:
            return predictConditional(addr, fall_through);
        case VanadisBranchClass::RETURN:
        {
            in_flight_unconditional_.push_back({ addr, fall_through, branch_class });
            if ( ! returns_.empty() ) {
                const uint64_t target = returns_.back();
                returns_.pop_back();
                return target;
            }
            return contains(addr) ? predictAddress(addr) : fall_through;
        }
        case VanadisBranchClass::CALL:
            in_flight_unconditional_.push_back({ addr, fall_through, branch_class });
            returns_.push_back(fall_through);
            if ( returns_.size() > return_stack_entries_ ) returns_.pop_front();
            return contains(addr) ? predictAddress(addr) : fall_through;
        default:
            in_flight_unconditional_.push_back({ addr, fall_through, branch_class });
            return contains(addr) ? predictAddress(addr) : fall_through;
        }
    }

    void flush() override
    {
        in_flight_.clear();
        in_flight_unconditional_.clear();
        history_ = retired_history_;
        returns_ = retired_returns_;
    }

    // The speculative history and return stack are rebuilt from the retired ones and the predictions that stay; the
    // resolved branch adds its outcome instead of its prediction.
    void flushYounger(const size_t conditional_kept, const size_t other_kept, const bool resolved_conditional,
        const uint64_t next) override
    {
        while ( in_flight_.size() > conditional_kept ) in_flight_.pop_back();
        while ( in_flight_unconditional_.size() > other_kept ) in_flight_unconditional_.pop_back();
        history_ = retired_history_;
        for ( size_t k = 0; k < in_flight_.size(); ++k ) {
            const bool last = k + 1 == in_flight_.size();
            const bool taken = last && resolved_conditional ? next != in_flight_[k].fall_through : in_flight_[k].prediction;
            shiftIn(history_, taken);
        }
        returns_ = retired_returns_;
        for ( const InFlightOther& other : in_flight_unconditional_ ) {
            if ( VanadisBranchClass::CALL == other.branch_class ) {
                returns_.push_back(other.fall_through);
                if ( returns_.size() > return_stack_entries_ ) returns_.pop_front();
            }
            else if ( VanadisBranchClass::RETURN == other.branch_class && ! returns_.empty() ) {
                returns_.pop_back();
            }
        }
    }


private:
    //! Bits of global history kept, the largest allowed history length.
    static constexpr uint32_t HISTORY_BITS = 256;
    static constexpr uint32_t HISTORY_WORD_BITS = 64;
    using History = std::array<uint64_t, HISTORY_BITS / HISTORY_WORD_BITS>;

    //! Bits of a tag.
    static constexpr uint32_t TAG_BITS = 11;
    //! The counter of a tagged entry is -4 to 3; 0 or -1 is a weak prediction.
    static constexpr int8_t COUNTER_MAX = 3;
    static constexpr int8_t COUNTER_MIN = -4;
    //! The bimodal counter is -2 to 1.
    static constexpr int8_t BIMODAL_MAX = 1;
    static constexpr int8_t BIMODAL_MIN = -2;
    //! The usefulness counter is 0 to 3, and every entry loses one this often.
    static constexpr uint8_t USEFUL_MAX = 3;
    static constexpr uint64_t USEFUL_AGE_PERIOD = 262144;
    //! No tagged table provides.
    static constexpr int NO_TABLE = -1;

    struct TaggedEntry
    {
        uint16_t tag = 0;
        int8_t counter = 0;
        uint8_t useful = 0;
    };

    struct Table
    {
        uint32_t history_length = 0;
        std::vector<TaggedEntry> entries;
    };

    //! A predicted conditional branch, oldest first: retire matches it to its outcome.
    struct InFlight
    {
        uint64_t address;
        uint64_t fall_through;
        History history;
        int provider;
        int alternate;
        bool prediction;
        bool alternate_prediction;
    };

    //! Another predicted branch: retire moves the return address stack forward.
    struct InFlightOther
    {
        uint64_t address;
        uint64_t fall_through;
        VanadisBranchClass branch_class;
    };

    static bool powerOfTwo(const uint32_t value) { return value != 0 && (value & (value - 1)) == 0; }

    static uint32_t log2Of(uint32_t value)
    {
        uint32_t bits = 0;
        while ( value > 1 ) {
            value >>= 1;
            ++bits;
        }
        return bits;
    }

    static void shiftIn(History& history, const bool taken)
    {
        for ( size_t w = history.size() - 1; w > 0; --w ) {
            history[w] = (history[w] << 1) | (history[w - 1] >> (HISTORY_WORD_BITS - 1));
        }
        history[0] = (history[0] << 1) | (taken ? 1 : 0);
    }

    //! The newest length bits of the history, folded into bits bits by exclusive or.
    static uint32_t fold(const History& history, const uint32_t length, const uint32_t bits)
    {
        uint64_t folded = 0;
        uint32_t position = 0;
        while ( position < length ) {
            const uint32_t take = std::min(bits, length - position);
            const uint32_t word = position / HISTORY_WORD_BITS;
            const uint32_t offset = position % HISTORY_WORD_BITS;
            uint64_t chunk = history[word] >> offset;
            if ( offset != 0 && word + 1 < history.size() ) chunk |= history[word + 1] << (HISTORY_WORD_BITS - offset);
            folded ^= chunk & ((1ull << take) - 1);
            position += take;
        }
        return static_cast<uint32_t>(folded);
    }

    uint32_t tableIndex(const uint64_t addr, const History& history, const size_t table) const
    {
        const uint64_t pc = addr >> 2;
        return static_cast<uint32_t>((pc ^ (pc >> index_bits_) ^ fold(history, tables_[table].history_length, index_bits_)
            ^ (table << 1)) & tagged_mask_);
    }

    uint16_t tableTag(const uint64_t addr, const History& history, const size_t table) const
    {
        const uint64_t pc = addr >> 2;
        const uint32_t length = tables_[table].history_length;
        return static_cast<uint16_t>((pc ^ fold(history, length, TAG_BITS) ^ (fold(history, length, TAG_BITS - 1) << 1))
            & ((1u << TAG_BITS) - 1));
    }

    uint32_t bimodalIndex(const uint64_t addr) const { return static_cast<uint32_t>((addr >> 2) & bimodal_mask_); }

    uint64_t predictConditional(const uint64_t addr, const uint64_t fall_through)
    {
        InFlight branch{ addr, fall_through, history_, NO_TABLE, NO_TABLE, false, false };
        for ( int t = static_cast<int>(tables_.size()) - 1; t >= 0; --t ) {
            const TaggedEntry& entry = tables_[t].entries[tableIndex(addr, history_, t)];
            if ( entry.tag != tableTag(addr, history_, t) ) continue;
            if ( NO_TABLE == branch.provider ) branch.provider = t;
            else if ( NO_TABLE == branch.alternate ) { branch.alternate = t; break; }
        }
        const bool base = bimodal_[bimodalIndex(addr)] >= 0;
        if ( NO_TABLE == branch.provider ) {
            branch.prediction = base;
            branch.alternate_prediction = base;
        }
        else {
            const TaggedEntry& provider = tables_[branch.provider].entries[tableIndex(addr, history_, branch.provider)];
            branch.alternate_prediction = NO_TABLE == branch.alternate
                ? base : tables_[branch.alternate].entries[tableIndex(addr, history_, branch.alternate)].counter >= 0;
            // A new entry says little yet, so the next table decides.
            const bool newly_allocated = (provider.counter == 0 || provider.counter == -1) && provider.useful == 0;
            branch.prediction = newly_allocated ? branch.alternate_prediction : provider.counter >= 0;
        }
        in_flight_.push_back(branch);
        stat_predictions_->addData(1);
        shiftIn(history_, branch.prediction);

        if ( branch.prediction && contains(addr) ) return predictAddress(addr);
        return fall_through;
    }

    void train(const InFlight& branch, const bool taken)
    {
        if ( branch.prediction != taken ) stat_mispredictions_->addData(1);

        const History& history = branch.history;
        if ( NO_TABLE == branch.provider ) {
            step(bimodal_[bimodalIndex(branch.address)], taken, BIMODAL_MIN, BIMODAL_MAX);
        }
        else {
            TaggedEntry& provider = tables_[branch.provider].entries[tableIndex(branch.address, history, branch.provider)];
            const bool provider_prediction = provider.counter >= 0;
            step(provider.counter, taken, COUNTER_MIN, COUNTER_MAX);
            if ( provider_prediction != branch.alternate_prediction ) {
                if ( provider_prediction == taken ) {
                    if ( provider.useful < USEFUL_MAX ) ++provider.useful;
                }
                else if ( provider.useful > 0 ) {
                    --provider.useful;
                }
            }
        }

        if ( branch.prediction != taken ) allocate(branch, taken);

        if ( ++updates_ % USEFUL_AGE_PERIOD == 0 ) {
            for ( Table& table : tables_ ) {
                for ( TaggedEntry& entry : table.entries ) {
                    if ( entry.useful > 0 ) --entry.useful;
                }
            }
        }
    }

    //! A wrong prediction takes an entry in the shortest table longer than the provider that has a free one.
    void allocate(const InFlight& branch, const bool taken)
    {
        bool allocated = false;
        for ( size_t t = static_cast<size_t>(branch.provider + 1); t < tables_.size(); ++t ) {
            TaggedEntry& entry = tables_[t].entries[tableIndex(branch.address, branch.history, t)];
            if ( entry.useful == 0 ) {
                entry.tag = tableTag(branch.address, branch.history, t);
                entry.counter = taken ? 0 : -1;
                allocated = true;
                break;
            }
        }
        if ( allocated ) return;
        for ( size_t t = static_cast<size_t>(branch.provider + 1); t < tables_.size(); ++t ) {
            TaggedEntry& entry = tables_[t].entries[tableIndex(branch.address, branch.history, t)];
            if ( entry.useful > 0 ) --entry.useful;
        }
    }

    static void step(int8_t& counter, const bool taken, const int8_t low, const int8_t high)
    {
        if ( taken ) {
            if ( counter < high ) ++counter;
        }
        else if ( counter > low ) {
            --counter;
        }
    }

    void rememberTarget(const uint64_t addr, const uint64_t target)
    {
        auto found = targets_.find(addr);
        if ( found != targets_.end() ) {
            found->second = target;
            return;
        }
        if ( targets_.size() >= max_targets_ ) {
            targets_.erase(target_order_.front());
            target_order_.pop_front();
        }
        target_order_.push_back(addr);
        targets_.emplace(addr, target);
    }

    SST::Output* output_ = nullptr;
    uint32_t max_targets_ = 0;
    uint32_t return_stack_entries_ = 0;
    uint32_t bimodal_mask_ = 0;
    uint32_t tagged_mask_ = 0;
    uint32_t index_bits_ = 0;
    uint64_t updates_ = 0;

    std::vector<int8_t> bimodal_;
    std::vector<Table> tables_;
    std::unordered_map<uint64_t, uint64_t> targets_;
    std::deque<uint64_t> target_order_;

    History history_{};
    History retired_history_{};
    std::deque<uint64_t> returns_;
    std::deque<uint64_t> retired_returns_;
    std::deque<InFlight> in_flight_;
    std::deque<InFlightOther> in_flight_unconditional_;

    Statistic<uint64_t>* stat_predictions_ = nullptr;
    Statistic<uint64_t>* stat_mispredictions_ = nullptr;
};

} // namespace Vanadis
} // namespace SST

#endif
