//---------------------------------------------------------
// Copyright 2015 Ontario Institute for Cancer Research
// Written by Jared Simpson (jared.simpson@oicr.on.ca)
//---------------------------------------------------------
//
// nanopolish_profile_hmm -- Profile Hidden Markov Model
//
#include <algorithm>
#include "nanopolish_profile_hmm.h"

//#define DEBUG_FILL
//#define PRINT_TRAINING_MESSAGES 1

void profile_hmm_forward_initialize(FloatMatrix& fm)
{
    // initialize forward calculation
    for(uint32_t si = 0; si < fm.n_cols; si++) {
        set(fm, 0, si, -INFINITY);
    }

    for(uint32_t ri = 0; ri < fm.n_rows; ri++) {
        set(fm, ri, PS_KMER_SKIP, -INFINITY);
        set(fm, ri, PS_EVENT_SPLIT, -INFINITY);
        set(fm, ri, PS_MATCH, -INFINITY);
    }

    set(fm, 0, PS_KMER_SKIP, -INFINITY);
    set(fm, 0, PS_EVENT_SPLIT, -INFINITY);
    set(fm, 0, PS_MATCH, 0.0f);
}

// Terminate the forward algorithm by calculating
// the probability of transitioning to the end state
// for all columns and a given row
float profile_hmm_forward_terminate(const FloatMatrix& fm,
                                    const FloatMatrix& tm,
                                    uint32_t row)
{
    assert(false);
    return -INFINITY;

    /*
    float sum = -INFINITY;
    uint32_t tcol = fm.n_cols - 1;
    for(uint32_t sk = 0; sk < fm.n_cols - 1; sk++) {
        // transition probability from state k to state l
        float t_kl = get(tm, sk, tcol);
        float fm_k = get(fm, row, sk);
        sum = add_logs(sum, t_kl + fm_k);
    }
    return sum;
    */
}

// convenience function to run the HMM over multiple inputs and sum the result
float profile_hmm_score(const std::string& consensus, const std::vector<HMMInputData>& data)
{
    float score = 0.0f;
    for(size_t i = 0; i < data.size(); ++i) {
        score += profile_hmm_score(consensus, data[i]);
    }
    return score;
}

float profile_hmm_score(const std::string& sequence, const HMMInputData& data)
{
    uint32_t n_kmers = sequence.size() - K + 1;

    uint32_t n_states = PS_NUM_STATES * (n_kmers + 2); // + 2 for explicit terminal states

    uint32_t e_start = data.event_start_idx;
    uint32_t e_end = data.event_stop_idx;
    uint32_t n_events = 0;
    if(e_end > e_start)
        n_events = e_end - e_start + 1;
    else
        n_events = e_start - e_end + 1;

    uint32_t n_rows = n_events + 1;

    // Allocate a matrix to hold the HMM result
    FloatMatrix fm;
    allocate_matrix(fm, n_rows, n_states);

    profile_hmm_forward_initialize(fm);

    ProfileHMMForwardOutput output(&fm);

    float score = profile_hmm_fill_generic(sequence.c_str(), data, e_start, output);

    // cleanup
    free_matrix(fm);
    return score;
}

void profile_hmm_viterbi_initialize(FloatMatrix& m)
{
    // Same as forward initialization
    profile_hmm_forward_initialize(m);
}

std::vector<AlignmentState> profile_hmm_align(const std::string& sequence, const HMMInputData& data)
{
    std::vector<AlignmentState> alignment;

    uint32_t n_kmers = sequence.size() - K + 1;
    uint32_t n_states = PS_NUM_STATES * (n_kmers + 2); // + 2 for explicit terminal states

    uint32_t e_start = data.event_start_idx;
    uint32_t e_end = data.event_stop_idx;
    uint32_t n_events = 0;
    if(e_end > e_start)
        n_events = e_end - e_start + 1;
    else
        n_events = e_start - e_end + 1;
    assert(n_events >= 2);

    uint32_t n_rows = n_events + 1;
    
    // Allocate matrices to hold the HMM result
    FloatMatrix vm;
    allocate_matrix(vm, n_rows, n_states);
    
    UInt8Matrix bm;
    allocate_matrix(bm, n_rows, n_states);

    ProfileHMMViterbiOutput output(&vm, &bm);

    profile_hmm_viterbi_initialize(vm);
    profile_hmm_fill_generic(sequence.c_str(), data, e_start, output);

    // Traverse the backtrack matrix to compute the results
    
    // start from the last event matched to the last kmer
    uint32_t row = n_rows - 1;
    uint32_t col = PS_NUM_STATES * n_kmers + PS_MATCH;

    while(row > 0) {
        
        uint32_t event_idx = e_start + (row - 1) * data.event_stride;
        uint32_t block = col / PS_NUM_STATES;
        assert(block > 0);
        assert(get(vm, row, col) != -INFINITY);

        uint32_t kmer_idx = block - 1;
        
        ProfileState curr_ps = (ProfileState) (col % PS_NUM_STATES);

        AlignmentState as;
        as.event_idx = event_idx;
        as.kmer_idx = kmer_idx;
        as.l_posterior = -INFINITY; // not computed
        as.l_fm = get(vm, row, col);
        as.log_transition_probability = -INFINITY; // not computed
        as.state = ps2char(curr_ps);
        alignment.push_back(as);

        // Update the event (row) and k-mer using the current state
        // The next state is encoded in the backtrack matrix for the current cell
        ProfileState next_ps = (ProfileState)get(bm, row, col);

#if DEBUG_BACKTRACK
        printf("Backtrack [%zu %zu] k: %zu block: %zu curr_ps: %c next_ps: %c\n", row, col, kmer_idx, block, ps2char(curr_ps), ps2char(next_ps));
#endif

        if(curr_ps == PS_MATCH) {
            row -= 1;
            kmer_idx -= 1;
        } else if(curr_ps == PS_EVENT_SPLIT) {
            row -= 1;
            // kmer stays the same
        } else {
            assert(curr_ps == PS_KMER_SKIP);
            // row stays the same
            kmer_idx -= 1;
        }

        col = PS_NUM_STATES * (kmer_idx + 1) + next_ps;
    }

    //
    std::reverse(alignment.begin(), alignment.end());

    //
    free_matrix(vm);
    free_matrix(bm);

    return alignment;
}

void profile_hmm_update_training(const std::string& consensus, 
                                 const HMMInputData& data)
{
    std::vector<AlignmentState> alignment = profile_hmm_align(consensus, data);

    const PoreModel& pm = data.read->pore_model[data.strand];
    TrainingData& training_data = data.read->parameters[data.strand].training_data;

    size_t n_kmers = consensus.size() - K + 1;
    uint32_t strand_idx = 0;
    char prev_s = 'M';

    for(size_t pi = 0; pi < alignment.size(); ++pi) {

        uint32_t ei = alignment[pi].event_idx;
        uint32_t ki = alignment[pi].kmer_idx;
        char s = alignment[pi].state;
    
        // Record transition observations
        // We do not record observations for merge states as there was no kmer transitions
        // We also do not record observations for the beginning of the matches as the
        // alignment may be poor due to edge effects
        if(pi > 5 && pi < alignment.size() - 5) {
 
            // skip transition training data
            // we do not process the E state here as no k-mer move was made
            if(s != 'E') {
                uint32_t transition_kmer_from = alignment[pi - 1].kmer_idx;
                uint32_t transition_kmer_to = alignment[pi].kmer_idx;

                // Specially handle skips
                // We only want to record the first k-mer skipped if multiple were skipped
                if(s == 'K') {
                    transition_kmer_from = alignment[pi - 1].kmer_idx;
                    transition_kmer_to = transition_kmer_from + 1;
                }
                
                assert(transition_kmer_from < n_kmers && transition_kmer_to < n_kmers);

                uint32_t rank_1 = get_rank(data, consensus.c_str(), transition_kmer_from);
                uint32_t rank_2 = get_rank(data, consensus.c_str(), transition_kmer_to);
            
                GaussianParameters level_1 = pm.get_scaled_parameters(rank_1);
                GaussianParameters level_2 = pm.get_scaled_parameters(rank_2);
            
#ifdef PRINT_TRAINING_MESSAGES
                printf("TRAIN_SKIP\t%d\t%.3lf\t%.3lf\t%c\n", strand_idx, level_1.mean, level_2.mean, s);
#endif
                KmerTransitionObservation to = { level_1.mean, level_2.mean, s };
                training_data.kmer_transitions.push_back(to);
            }

            // State-to-state transition
            add_state_transition(training_data, prev_s, s);

            // emission
            float level = data.read->get_drift_corrected_level(ei, data.strand);
            float sd = data.read->events[data.strand][ei].stdv;
            float duration = data.read->get_duration(ei, data.strand);
            if(ki >= n_kmers)
                printf("%zu %d %d %zu %.2lf %c\n", pi, ei, ki, n_kmers, alignment[pi].l_fm, s);
            
            assert(ki < n_kmers);
            uint32_t rank = get_rank(data, consensus.c_str(), ki);
        
            GaussianParameters model = pm.get_scaled_parameters(rank);
            float norm_level = (level - model.mean) / model.stdv;

            if(s == 'M')
                training_data.emissions_for_matches.push_back(norm_level);
            prev_s = s;

#ifdef PRINT_TRAINING_MESSAGES
            printf("TRAIN_EMISSION\t%d\t%d\t%.3lf\t%.3lf\t%.3lf\t%.3lf\t%.3lf\t%.3lf\t%c\n", strand_idx, ei, level, sd, model.mean, model.stdv, norm_level, duration, s);
#endif
        }

        // summary
        training_data.n_matches += (s == 'M');
        training_data.n_merges += (s == 'E');
        training_data.n_skips += (s == 'K');
    }
}
