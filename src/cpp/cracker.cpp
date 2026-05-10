/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: partition_candidates implementation. Distributes candidates
 * round-robin across num_threads buckets so each thread tries passwords at
 * every frequency rank rather than one thread getting all the top candidates.
 */

#include "src/cpp/cracker.hpp"

std::vector<std::vector<std::string>> partition_candidates(
    const std::vector<std::string>& candidates,
    int num_threads
) {
    std::vector<std::vector<std::string>> partitions(num_threads);
    for (size_t i = 0; i < candidates.size(); ++i)
        partitions[i % num_threads].push_back(candidates[i]);
    return partitions;
}
