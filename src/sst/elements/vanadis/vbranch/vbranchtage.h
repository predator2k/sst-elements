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
        { "history_lengths", "Global history length of each tagged table, shortest first, up to 2048 branches and 16 tables", "[4, 8, 16, 32, 64, 128, 256]" },
        { "return_stack_entries", "Entries of the return address stack", "32" },
        { "allocations_per_misprediction", "Tagged tables longer than the provider in which a wrong prediction takes a free entry, shortest first", "1" },
        { "local_history_entries", "Branches whose own outcomes a local predictor keeps, a power of two; 0 has no local predictor", "0" },
        { "local_history_bits", "Outcomes of its own that a branch's local history holds, up to 32", "16" },
        { "local_pattern_entries", "Entries of the table indexed by a branch and its local history, a power of two", "16384" },
        { "indirect_target_entries", "Entries of the target table indexed by a branch with a register target and the history of such targets, a power of two; 0 has none and such a branch takes its last target", "0" },
        { "indirect_history_targets", "Register targets in the history that indexes the target table, up to 16", "8" })

    SST_ELI_DOCUMENT_STATISTICS(
        { "direction_predictions", "Conditional branches predicted", "branches", 1 },
        { "direction_mispredictions", "Conditional branches whose direction was wrong at retire", "branches", 1 })

    VanadisTageBranchUnit(ComponentId_t id, Params& params) : VanadisBranchUnit(id, params)
    {
        max_targets_ = params.find<uint32_t>("branch_entries", 4096);
        const uint32_t bimodal = params.find<uint32_t>("bimodal_entries", 16384);
        const uint32_t tagged = params.find<uint32_t>("tagged_entries", 4096);
        return_stack_entries_ = params.find<uint32_t>("return_stack_entries", 32);
        allocations_ = std::max<uint32_t>(1, params.find<uint32_t>("allocations_per_misprediction", 1));
        const uint32_t local_histories = params.find<uint32_t>("local_history_entries", 0);
        local_history_bits_ = std::min<uint32_t>(32, std::max<uint32_t>(1, params.find<uint32_t>("local_history_bits", 16)));
        const uint32_t local_patterns = params.find<uint32_t>("local_pattern_entries", 16384);
        const uint32_t indirect_targets = params.find<uint32_t>("indirect_target_entries", 0);
        indirect_history_targets_ = std::min<uint32_t>(16, std::max<uint32_t>(1, params.find<uint32_t>("indirect_history_targets", 8)));
        if ( indirect_targets > 0 ) {
            if ( ! powerOfTwo(indirect_targets) ) output_->fatal(CALL_INFO, -1, "indirect_target_entries must be a power of two\n");
            indirect_entries_.assign(indirect_targets, IndirectEntry{});
        }
        std::vector<uint32_t> lengths;
        params.find_array<uint32_t>("history_lengths", lengths);
        if ( lengths.empty() ) lengths = { 4, 8, 16, 32, 64, 128, 256 };

        output_ = new SST::Output("[tage]: ", 0, 0, SST::Output::STDOUT);
        if ( ! powerOfTwo(bimodal) || ! powerOfTwo(tagged) ) {
            output_->fatal(CALL_INFO, -1, "bimodal_entries and tagged_entries must be powers of two\n");
        }
        if ( lengths.size() > MAX_TABLES ) output_->fatal(CALL_INFO, -1, "at most %u tagged tables\n", MAX_TABLES);
        for ( const uint32_t length : lengths ) {
            if ( length == 0 || length > HISTORY_BITS ) {
                output_->fatal(CALL_INFO, -1, "a history length must be in 1 to %u\n", HISTORY_BITS);
            }
        }

        if ( local_histories > 0 ) {
            if ( ! powerOfTwo(local_histories) || ! powerOfTwo(local_patterns) ) {
                output_->fatal(CALL_INFO, -1, "local_history_entries and local_pattern_entries must be powers of two\n");
            }
            local_histories_.assign(local_histories, 0);
            local_patterns_.assign(local_patterns, LocalEntry{});
            local_choosers_.assign(local_histories, 0);
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
            if ( other.register_target ) {
                if ( ! indirect_entries_.empty() ) trainIndirect(other, next_addr);
                retired_target_history_ = pushTarget(retired_target_history_, next_addr);
            }
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
            InFlightOther other{ addr, fall_through, branch_class };
            other.register_target = register_target_;
            uint64_t target = 0;
            if ( ! returns_.empty() ) {
                target = returns_.back();
                returns_.pop_back();
            }
            else {
                target = contains(addr) ? predictAddress(addr) : fall_through;
            }
            return recordOther(other, target);
        }
        case VanadisBranchClass::CALL:
        {
            InFlightOther other{ addr, fall_through, branch_class };
            other.register_target = register_target_;
            returns_.push_back(fall_through);
            if ( returns_.size() > return_stack_entries_ ) returns_.pop_front();
            return recordOther(other, predictOtherTarget(other));
        }
        default:
        {
            InFlightOther other{ addr, fall_through, branch_class };
            other.register_target = register_target_;
            return recordOther(other, predictOtherTarget(other));
        }
        }
    }

    void noteRegisterTarget(const bool register_target) override { register_target_ = register_target; }

    void flush() override
    {
        in_flight_.clear();
        in_flight_unconditional_.clear();
        history_ = retired_history_;
        target_history_ = retired_target_history_;
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
            in_flight_[k].history_outcome = taken;
            shiftIn(history_, taken);
        }
        target_history_ = retired_target_history_;
        for ( size_t k = 0; k < in_flight_unconditional_.size(); ++k ) {
            InFlightOther& other = in_flight_unconditional_[k];
            if ( k + 1 == in_flight_unconditional_.size() && ! resolved_conditional ) other.predicted_target = next;
            if ( other.register_target ) target_history_ = pushTarget(target_history_, other.predicted_target);
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
    static constexpr uint32_t HISTORY_BITS = 2048;
    //! Tagged tables at most.
    static constexpr uint32_t MAX_TABLES = 16;
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
        //! The index and tag of the branch in each tagged table, computed once at prediction.
        std::array<uint32_t, MAX_TABLES> index;
        std::array<uint16_t, MAX_TABLES> tag;
        //! The prediction of the tagged tables alone; prediction differs when the local predictor overrides it.
        bool tage_prediction = false;
        //! The outcome the histories took: the prediction, or the outcome once the branch resolved wrong.
        bool history_outcome = false;
        //! The local pattern entry, its tag, whether it matched, and its prediction.
        uint32_t local_index = 0;
        uint16_t local_tag = 0;
        bool local_valid = false;
        bool local_prediction = false;
    };

    //! An entry of the local pattern table: a tag and a counter of -4 to 3.
    struct LocalEntry
    {
        uint16_t tag = 0;
        int8_t counter = 0;
    };

    //! Another predicted branch: retire moves the return address stack forward.
    struct InFlightOther
    {
        uint64_t address;
        uint64_t fall_through;
        VanadisBranchClass branch_class;
        //! Whether the target comes from a register, the predicted target, and the target table entry and tag.
        bool register_target = false;
        uint64_t predicted_target = 0;
        uint32_t indirect_index = 0;
        uint16_t indirect_tag = 0;
    };

    //! An entry of the indirect target table: a tag, a target and a confidence of 0 to 3.
    struct IndirectEntry
    {
        uint16_t tag = 0;
        uint64_t target = 0;
        uint8_t confidence = 0;
    };

    /*
     * The target of a call or other branch that is not a return.
     * Handling: a branch with a register target whose entry in the target table matches and has some confidence takes
     * that entry's target; every other branch takes its last target, or falls through without one. Sets the table index
     * and tag of other. Emits nothing.
     */
    uint64_t predictOtherTarget(InFlightOther& other)
    {
        const uint64_t last = contains(other.address) ? targets_.find(other.address)->second : other.fall_through;
        if ( ! other.register_target || indirect_entries_.empty() ) return last;
        const uint64_t mixed = ((other.address >> 2) * 0x9E3779B97F4A7C15ull) ^ (target_history_ * 0xC2B2AE3D27D4EB4Full);
        other.indirect_index = static_cast<uint32_t>((mixed >> 24) & (indirect_entries_.size() - 1));
        other.indirect_tag = static_cast<uint16_t>((mixed >> 50) & ((1u << TAG_BITS) - 1));
        const IndirectEntry& entry = indirect_entries_[other.indirect_index];
        if ( entry.tag == other.indirect_tag && entry.confidence > 0 ) return entry.target;
        return last;
    }

    /*
     * Puts a predicted call, return or other branch in flight.
     * Handling: records the predicted target and, for a register target, shifts it into the speculative target history.
     * Emits nothing. Returns the target.
     */
    uint64_t recordOther(InFlightOther& other, const uint64_t target)
    {
        other.predicted_target = target;
        if ( other.register_target ) target_history_ = pushTarget(target_history_, target);
        in_flight_unconditional_.push_back(other);
        return target;
    }

    /*
     * Trains the target table with the target a branch with a register target took.
     * Handling: a matching entry gains confidence when its target was right and loses it otherwise, taking the new
     * target at no confidence; an entry of another branch and history with no confidence is replaced, one with some
     * loses it. Emits nothing.
     */
    void trainIndirect(const InFlightOther& other, const uint64_t target)
    {
        if ( VanadisBranchClass::RETURN == other.branch_class ) return;
        IndirectEntry& entry = indirect_entries_[other.indirect_index];
        if ( entry.tag == other.indirect_tag ) {
            if ( entry.target == target ) {
                if ( entry.confidence < 3 ) ++entry.confidence;
            }
            else if ( entry.confidence > 0 ) {
                --entry.confidence;
            }
            else {
                entry.target = target;
                entry.confidence = 1;
            }
        }
        else if ( entry.confidence == 0 ) {
            entry.tag = other.indirect_tag;
            entry.target = target;
            entry.confidence = 1;
        }
        else {
            --entry.confidence;
        }
    }

    //! The history of register targets with a target shifted in: four bits of each, newest lowest.
    uint64_t pushTarget(const uint64_t history, const uint64_t target) const
    {
        const uint64_t bits = ((target >> 2) ^ (target >> 6) ^ (target >> 10) ^ (target >> 14)) & 0xF;
        const uint32_t kept = 4 * indirect_history_targets_;
        const uint64_t mask = kept >= 64 ? ~0ull : ((1ull << kept) - 1);
        return ((history << 4) | bits) & mask;
    }

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
        InFlight branch{ addr, fall_through, history_, NO_TABLE, NO_TABLE, false, false, {}, {} };
        for ( size_t t = 0; t < tables_.size(); ++t ) {
            branch.index[t] = tableIndex(addr, history_, t);
            branch.tag[t] = tableTag(addr, history_, t);
        }
        for ( int t = static_cast<int>(tables_.size()) - 1; t >= 0; --t ) {
            const TaggedEntry& entry = tables_[t].entries[branch.index[t]];
            if ( entry.tag != branch.tag[t] ) continue;
            if ( NO_TABLE == branch.provider ) branch.provider = t;
            else if ( NO_TABLE == branch.alternate ) { branch.alternate = t; break; }
        }
        const bool base = bimodal_[bimodalIndex(addr)] >= 0;
        if ( NO_TABLE == branch.provider ) {
            branch.prediction = base;
            branch.alternate_prediction = base;
        }
        else {
            const TaggedEntry& provider = tables_[branch.provider].entries[branch.index[branch.provider]];
            branch.alternate_prediction = NO_TABLE == branch.alternate
                ? base : tables_[branch.alternate].entries[branch.index[branch.alternate]].counter >= 0;
            // A new entry says little yet, so the next table decides.
            const bool newly_allocated = (provider.counter == 0 || provider.counter == -1) && provider.useful == 0;
            branch.prediction = newly_allocated ? branch.alternate_prediction : provider.counter >= 0;
        }
        branch.tage_prediction = branch.prediction;
        if ( ! local_histories_.empty() ) predictLocal(branch);
        branch.history_outcome = branch.prediction;
        in_flight_.push_back(branch);
        stat_predictions_->addData(1);
        shiftIn(history_, branch.prediction);

        if ( branch.prediction && contains(addr) ) return predictAddress(addr);
        return fall_through;
    }

    /*
     * The local predictor: a branch's own recent outcomes, the retired ones and those of its older instances in flight,
     * index a tagged pattern table; a strong entry overrides the tagged tables while the branch's chooser favours it.
     * Handling: sets the local fields of branch and, when the local entry decides, its prediction. Emits nothing.
     */
    void predictLocal(InFlight& branch) const
    {
        const uint32_t slot = localSlot(branch.address);
        uint32_t history = local_histories_[slot];
        for ( const InFlight& older : in_flight_ ) {
            if ( older.address == branch.address ) history = (history << 1) | (older.history_outcome ? 1u : 0u);
        }
        history &= localMask();
        const uint64_t pc = branch.address >> 2;
        const uint64_t mixed = (pc * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(history) * 0xC2B2AE3D27D4EB4Full);
        branch.local_index = static_cast<uint32_t>((mixed >> 20) & (local_patterns_.size() - 1));
        branch.local_tag = static_cast<uint16_t>((mixed >> 48) & ((1u << TAG_BITS) - 1));
        const LocalEntry& entry = local_patterns_[branch.local_index];
        branch.local_valid = entry.tag == branch.local_tag;
        branch.local_prediction = entry.counter >= 0;
        const bool strong = entry.counter >= 2 || entry.counter <= -3;
        if ( branch.local_valid && strong && local_choosers_[slot] >= 2 ) branch.prediction = branch.local_prediction;
    }

    /*
     * Trains the local predictor at retire with the outcome of a branch.
     * Handling: steps or replaces the pattern entry, moves the chooser toward the predictor that was right when the two
     * disagreed, and shifts the outcome into the branch's retired local history. Emits nothing.
     */
    void trainLocal(const InFlight& branch, const bool taken)
    {
        const uint32_t slot = localSlot(branch.address);
        LocalEntry& entry = local_patterns_[branch.local_index];
        if ( branch.local_valid ) {
            if ( branch.local_prediction != branch.tage_prediction ) {
                int8_t& chooser = local_choosers_[slot];
                if ( branch.local_prediction == taken && chooser < 3 ) ++chooser;
                else if ( branch.tage_prediction == taken && chooser > 0 ) --chooser;
            }
            if ( entry.tag == branch.local_tag ) step(entry.counter, taken, COUNTER_MIN, COUNTER_MAX);
        }
        else if ( entry.counter == 0 || entry.counter == -1 ) {
            entry.tag = branch.local_tag;
            entry.counter = taken ? 0 : -1;
        }
        else {
            entry.counter += entry.counter > 0 ? -1 : 1;
        }
        local_histories_[slot] = ((local_histories_[slot] << 1) | (taken ? 1u : 0u)) & localMask();
    }

    uint32_t localSlot(const uint64_t addr) const { return static_cast<uint32_t>((addr >> 2) & (local_histories_.size() - 1)); }

    uint32_t localMask() const { return local_history_bits_ >= 32 ? 0xFFFFFFFFu : ((1u << local_history_bits_) - 1); }

    void train(const InFlight& branch, const bool taken)
    {
        if ( branch.prediction != taken ) stat_mispredictions_->addData(1);
        if ( ! local_histories_.empty() ) trainLocal(branch, taken);

        if ( NO_TABLE == branch.provider ) {
            step(bimodal_[bimodalIndex(branch.address)], taken, BIMODAL_MIN, BIMODAL_MAX);
        }
        else {
            TaggedEntry& provider = tables_[branch.provider].entries[branch.index[branch.provider]];
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

        if ( branch.tage_prediction != taken ) allocate(branch, taken);

        if ( ++updates_ % USEFUL_AGE_PERIOD == 0 ) {
            for ( Table& table : tables_ ) {
                for ( TaggedEntry& entry : table.entries ) {
                    if ( entry.useful > 0 ) --entry.useful;
                }
            }
        }
    }

    //! A wrong prediction takes an entry in each of the allocations_ shortest tables longer than the provider that have a
    //! free one.
    void allocate(const InFlight& branch, const bool taken)
    {
        uint32_t allocated = 0;
        for ( size_t t = static_cast<size_t>(branch.provider + 1); t < tables_.size() && allocated < allocations_; ++t ) {
            TaggedEntry& entry = tables_[t].entries[branch.index[t]];
            if ( entry.useful == 0 ) {
                entry.tag = branch.tag[t];
                entry.counter = taken ? 0 : -1;
                allocated++;
            }
        }
        if ( allocated > 0 ) return;
        for ( size_t t = static_cast<size_t>(branch.provider + 1); t < tables_.size(); ++t ) {
            TaggedEntry& entry = tables_[t].entries[branch.index[t]];
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
    //! allocations_per_misprediction.
    uint32_t allocations_ = 1;
    //! The local predictor: the retired local history and the chooser (0 to 3, 2 and above favour the local entry) of
    //! each branch slot, the pattern table, and local_history_bits. Empty without a local predictor.
    std::vector<uint32_t> local_histories_;
    std::vector<int8_t> local_choosers_;
    std::vector<LocalEntry> local_patterns_;
    uint32_t local_history_bits_ = 16;
    //! The indirect target table, the speculative and retired histories of register targets, indirect_history_targets,
    //! and whether the branch of the next prediction has a register target.
    std::vector<IndirectEntry> indirect_entries_;
    uint64_t target_history_ = 0;
    uint64_t retired_target_history_ = 0;
    uint32_t indirect_history_targets_ = 8;
    bool register_target_ = false;
};

} // namespace Vanadis
} // namespace SST

#endif
