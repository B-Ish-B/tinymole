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
