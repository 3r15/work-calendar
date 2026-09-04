#pragma once

#include "core/domain/entities.h"

#include <string>
#include <vector>

namespace app {

// --worker 는 ID 와 이름을 모두 받는다 (CLI-SPEC.md 휴무).
//
// 이름이 여러 명과 일치하면 프로그램이 고를 수 없다. 후보를 돌려주고 사용자가 ID 로 다시
// 부르게 한다 — 조용히 한 명을 고르면 엉뚱한 사람이 쉬게 된다.
struct WorkerMatch {
    enum class Kind {
        Found,
        NotFound,
        Ambiguous,
    };

    Kind kind{Kind::NotFound};
    domain::Worker worker;                  // Found 일 때만 의미가 있다
    std::vector<domain::Worker> candidates;  // Ambiguous 일 때 보여줄 목록
};

// ID 가 정확히 맞으면 그것이 답이다. 아니면 이름으로 찾는다.
// active 가 false 인 작업자도 찾는다 — 휴무를 지우거나 다시 켤 수 있어야 한다.
WorkerMatch resolveWorker(const domain::Model& model, const std::string& query);

}  // namespace app
