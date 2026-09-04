#include "core/app/worker_lookup.h"

namespace app {

WorkerMatch resolveWorker(const domain::Model& model, const std::string& query) {
    WorkerMatch match;
    if (query.empty()) {
        return match;
    }

    // ID 가 먼저다. 누군가의 이름이 다른 사람의 ID 와 같아도 ID 쪽이 이긴다.
    for (const domain::Worker& worker : model.workers.workers) {
        if (worker.id.str() == query) {
            match.kind = WorkerMatch::Kind::Found;
            match.worker = worker;
            return match;
        }
    }

    for (const domain::Worker& worker : model.workers.workers) {
        if (worker.name == query) {
            match.candidates.push_back(worker);
        }
    }

    if (match.candidates.empty()) {
        match.kind = WorkerMatch::Kind::NotFound;
    } else if (match.candidates.size() == 1) {
        match.kind = WorkerMatch::Kind::Found;
        match.worker = match.candidates.front();
        match.candidates.clear();
    } else {
        match.kind = WorkerMatch::Kind::Ambiguous;
    }
    return match;
}

}  // namespace app
