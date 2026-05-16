#include <iostream>
#include <string>
#include <vector>
#include <map>

enum class ProposalStage {
    Draft, SG_Review, EWG_Review, LEWG_Review,
    CWG_Review, LWG_Review, PlenaryVote, Accepted
};

std::string stage_to_string(ProposalStage stage) {
    switch (stage) {
        case ProposalStage::Draft: return "Draft";
        case ProposalStage::SG_Review: return "SG Review";
        case ProposalStage::EWG_Review: return "EWG Review";
        case ProposalStage::LEWG_Review: return "LEWG Review";
        case ProposalStage::CWG_Review: return "CWG Review";
        case ProposalStage::LWG_Review: return "LWG Review";
        case ProposalStage::PlenaryVote: return "Plenary Vote";
        case ProposalStage::Accepted: return "Accepted";
    }
    return "Unknown";
}

struct ProposalRevision {
    std::string version;
    std::string meeting;
    ProposalStage stage;
    std::string note;
};

int main() {
    std::vector<ProposalRevision> history = {
        {"P0645R0", "(no meeting)", ProposalStage::Draft, "Initial submission"},
        {"P0645R1", "Kona 2017", ProposalStage::SG_Review, "SG feedback"},
        {"P0645R7", "Cologne 2019", ProposalStage::PlenaryVote, "Plenary vote"},
        {"P0645R8", "(C++20)", ProposalStage::Accepted, "Accepted into C++20"},
    };
    for (const auto& rev : history) {
        std::cout << "[" << rev.version << "] " << rev.meeting
                  << " | " << stage_to_string(rev.stage)
                  << " | " << rev.note << "\n";
    }
    return 0;
}
