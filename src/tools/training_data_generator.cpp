/*
  Tigerfish Training Data Generator
  Generates self-play training data in .binpack format for NNUE training.

  Unlike sf-tools which uses a separate Stockfish fork, this generator runs
  directly inside Tigerfish, using all Tiger search modifications (aggression,
  sharpness, anti-draw, etc.) during self-play.

  Usage (UCI command):
    generate_training_data depth 9 count 10000000 output_file_name data.binpack
*/

#include "training_data_generator.h"

#include "sfen_packer.h"
#include "packed_sfen.h"
#include "sfen_stream.h"

#include "../engine.h"
#include "../misc.h"
#include "../movegen.h"
#include "../position.h"
#include "../search.h"
#include "../thread.h"
#include "../tt.h"
#include "../types.h"
#include "../uci.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

namespace Stockfish::Tools {

// Simple PRNG (xorshift64)
struct PRNG_Tool {
    uint64_t s;

    PRNG_Tool(uint64_t seed = 0) {
        if (seed == 0) {
            seed = std::chrono::steady_clock::now().time_since_epoch().count();
            seed ^= (uint64_t)(uintptr_t)this;
        }
        s = seed;
    }

    uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }

    uint64_t rand(uint64_t n) { return n > 0 ? next() % n : 0; }
};

struct GenParams {
    int search_depth         = 9;
    uint64_t count           = 10000000;
    int eval_limit           = 3000;
    int random_move_count    = 8;
    int random_move_minply   = 1;
    int random_move_maxply   = 24;
    int random_multi_pv      = 4;
    int random_multi_pv_diff = 50;
    int write_minply         = 16;
    int write_maxply         = 400;
    bool keep_draws          = true;
    string output_file_name  = "training_data";
};

static constexpr int ADJ_DRAW_PLY   = 80;
static constexpr int ADJ_DRAW_CNT   = 8;
static constexpr int ADJ_DRAW_SCORE = 0;
static constexpr uint64_t REPORT_EVERY = 200000;

static const string StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Check if we should end the game based on scores and position
static optional<int8_t> check_game_end(
    const vector<int>& scores, Value current_score,
    const GenParams& params, int ply,
    int& resign_counter, bool should_resign)
{
    // Max ply → draw
    if (ply >= params.write_maxply)
        return int8_t(0);

    // Draw by consecutive low scores
    if (ply >= ADJ_DRAW_PLY && !scores.empty()) {
        int cons = 0;
        // Include current score
        if (abs(current_score) <= ADJ_DRAW_SCORE) cons++;
        for (auto it = scores.rbegin(); it != scores.rend() && cons < ADJ_DRAW_CNT; ++it) {
            if (abs(*it) <= ADJ_DRAW_SCORE) cons++;
            else break;
        }
        if (cons >= ADJ_DRAW_CNT)
            return int8_t(0);
    }

    // Eval limit / resign
    if (abs(current_score) >= params.eval_limit) {
        resign_counter++;
        if ((should_resign && resign_counter >= 4)
            || abs(current_score) >= VALUE_MATE_IN_MAX_PLY)
        {
            return (current_score >= params.eval_limit) ? int8_t(1) : int8_t(-1);
        }
    } else {
        resign_counter = 0;
    }

    return nullopt;
}

// Generate pre-computed flags for which plies get random moves
static vector<bool> make_random_flags(PRNG_Tool& rng, int minply, int maxply, int count) {
    vector<bool> flags(maxply + count, false);
    vector<int> candidates;
    candidates.reserve(maxply);
    for (int i = max(minply - 1, 0); i < maxply; ++i)
        candidates.push_back(i);

    for (int i = 0; i < min(count, (int)candidates.size()); ++i) {
        int j = i + (int)rng.rand((uint64_t)candidates.size() - i);
        swap(candidates[i], candidates[j]);
        if (candidates[i] < (int)flags.size())
            flags[candidates[i]] = true;
    }
    return flags;
}

static string now_string() {
    auto t = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string s(ctime(&t));
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

void generate_training_data(Engine& engine, std::istringstream& is)
{
    GenParams params;
    string token;

    while (is >> token) {
        if (token == "depth")                     is >> params.search_depth;
        else if (token == "count")                is >> params.count;
        else if (token == "eval_limit")           is >> params.eval_limit;
        else if (token == "random_move_count")    is >> params.random_move_count;
        else if (token == "random_move_min_ply")  is >> params.random_move_minply;
        else if (token == "random_move_max_ply")  is >> params.random_move_maxply;
        else if (token == "random_multi_pv")      is >> params.random_multi_pv;
        else if (token == "random_multi_pv_diff") is >> params.random_multi_pv_diff;
        else if (token == "write_min_ply")        is >> params.write_minply;
        else if (token == "write_max_ply")        is >> params.write_maxply;
        else if (token == "output_file_name")     is >> params.output_file_name;
        else if (token == "keep_draws")           is >> params.keep_draws;
        else {
            cout << "ERROR: Unknown option '" << token << "'" << endl;
            return;
        }
    }

    cout << "\nINFO: Tigerfish Training Data Generator" << endl;
    cout << "  depth       = " << params.search_depth << endl;
    cout << "  count       = " << params.count << endl;
    cout << "  eval_limit  = " << params.eval_limit << endl;
    cout << "  random_move = " << params.random_move_count << endl;
    cout << "  output      = " << params.output_file_name << endl;
    cout << "  keep_draws  = " << params.keep_draws << endl;

    // Verify NNUE is loaded
    engine.verify_networks();

    // Open output file in binpack format
    auto output = create_new_sfen_output(params.output_file_name, SfenOutputType::Binpack);
    if (!output) {
        cout << "ERROR: Failed to create output file " << params.output_file_name << endl;
        return;
    }

    // Suppress UCI search output during generation
    engine.set_on_bestmove([](string_view, string_view) {});
    engine.set_on_update_full([](const Search::InfoFull&) {});
    engine.set_on_update_no_moves([](const Search::InfoShort&) {});
    engine.set_on_iter([](const Search::InfoIteration&) {});
    engine.set_on_verify_networks([](string_view) {});

    PRNG_Tool rng;
    uint64_t total_sfens = 0;
    uint64_t last_report = 0;
    uint64_t total_games = 0;
    auto start_time = chrono::steady_clock::now();

    cout << "\nPRNG::seed = " << hex << rng.s << dec << endl;
    cout << "\n0 sfens, at " << now_string() << endl;

    while (total_sfens < params.count)
    {
        // --- Start a new self-play game ---
        vector<string> move_list;
        vector<int> move_scores;
        PSVector packed_sfens;
        packed_sfens.reserve(params.write_maxply);

        auto random_flags = make_random_flags(
            rng, params.random_move_minply, params.random_move_maxply,
            params.random_move_count);

        int resign_counter = 0;
        bool should_resign = rng.rand(10) > 1;
        bool game_ended = false;
        int8_t game_result = 0;
        Color result_color = WHITE;

        for (int ply = 0; ply < params.write_maxply + MAX_PLY && !game_ended; ++ply)
        {
            // Set position with all moves so far
            auto set_err = engine.set_position(StartFEN, move_list);
            if (set_err.has_value()) {
                game_ended = true;
                break;
            }

            // Fixed-depth search
            Search::LimitsType limits;
            limits.depth = params.search_depth;
            limits.startTime = now();

            engine.go(limits);
            engine.wait_for_search_finished();

            // Read search result
            Thread* bestThread = engine.get_best_thread_after_search();
            if (!bestThread || bestThread->worker->rootMoves.empty()) {
                game_result = 0;
                result_color = (ply % 2 == 0) ? WHITE : BLACK;
                game_ended = true;
                break;
            }

            Value score = bestThread->worker->rootMoves[0].score;
            Move best_move = bestThread->worker->rootMoves[0].pv[0];

            if (!best_move.is_ok()) {
                game_ended = true;
                break;
            }

            auto result = check_game_end(
                move_scores, score, params, ply, resign_counter, should_resign);

            if (result.has_value()) {
                game_result = result.value();
                result_color = (ply % 2 == 0) ? WHITE : BLACK;
                game_ended = true;
                break;
            }

            move_scores.push_back(score);

            // Pack this position for training output
            if (ply >= params.write_minply) {
                PackedSfenValue psv{};
                psv.sfen = sfen_pack(bestThread->worker->rootPos, false);
                psv.score = (int16_t)score;
                psv.move = (uint16_t)best_move.raw();
                psv.gamePly = (uint16_t)ply;
                psv.game_result = 0;
                psv.padding = 0;
                packed_sfens.push_back(psv);
            }

            // Choose next move (random or best)
            Move next_move = best_move;
            if (ply < (int)random_flags.size() && random_flags[ply]) {
                MoveList<LEGAL> legal(bestThread->worker->rootPos);
                if (legal.size() > 0)
                    next_move = *(legal.begin() + rng.rand(legal.size()));
            }

            move_list.push_back(
                UCIEngine::move(next_move, false));
        }

        // Assign game results to all recorded positions
        for (auto& psv : packed_sfens) {
            Color stm = (Color)(psv.sfen.data[0] & 1);
            psv.game_result = (stm == result_color) ? game_result : -game_result;
        }

        // Write game to output file
        if (params.keep_draws || game_result != 0) {
            if (!packed_sfens.empty())
                output->write(packed_sfens);
        }

        total_sfens += packed_sfens.size();
        total_games++;

        // Progress report
        if (total_sfens / REPORT_EVERY > last_report / REPORT_EVERY) {
            auto elapsed = chrono::steady_clock::now() - start_time;
            auto secs = chrono::duration_cast<chrono::seconds>(elapsed).count();
            uint64_t sps = secs > 0 ? total_sfens / secs : 0;

            cout << "\n" << total_sfens << " sfens, "
                 << sps << " sfens/second, at " << now_string() << endl;
        }
        last_report = total_sfens;
    }

    // Final report
    {
        auto elapsed = chrono::steady_clock::now() - start_time;
        auto secs = chrono::duration_cast<chrono::seconds>(elapsed).count();
        uint64_t sps = secs > 0 ? total_sfens / secs : 0;

        cout << "\n" << total_sfens << " sfens, "
             << sps << " sfens/second, at " << now_string() << endl;
        cout << "INFO: " << total_games << " games played." << endl;
        cout << "INFO: generate_training_data finished." << endl;
    }
}

} // namespace Stockfish::Tools
