#include "core/storage/migration.h"

#include "core/domain/entities.h"

#include <nlohmann/json.hpp>

namespace storage {
namespace {

using nlohmann::json;

// 버전 n 을 n+1 로 올리는 단계들. 스키마가 바뀌면 여기에 함수를 하나 추가하고
// kSteps 에 등록한다. 로드 경로는 건드릴 필요가 없다.
//
// 예시(아직 쓰이지 않음):
//   void v1ToV2(json& root) { root["새필드"] = 기본값; }
using Step = void (*)(json& root);

constexpr Step kSteps[] = {nullptr};  // [0] 이 1→2. 아직 없다.
constexpr int kStepCount = 0;

}  // namespace

void migrateIfNeeded(json& root, const std::string& where, domain::Report& report) {
    if (!root.is_object()) {
        report.error(where, "파일의 최상위가 객체가 아닙니다. 이 프로그램의 데이터 파일이 "
                            "맞는지 확인해 주세요.");
        return;
    }
    const auto it = root.find("version");
    if (it == root.end() || !it->is_number_integer()) {
        report.error(where, "version 필드가 없습니다. 이 프로그램의 데이터 파일이 맞는지 "
                            "확인해 주세요.");
        return;
    }

    const int found = it->get<int>();
    if (found == domain::kSchemaVersion) {
        return;
    }

    if (found > domain::kSchemaVersion) {
        // 새 버전이 쓴 파일을 옛 프로그램이 읽고 있다. 넘겨짚어 읽으면 데이터를 망가뜨린다.
        report.error(where, "이 파일은 더 새로운 형식입니다 (version " + std::to_string(found) +
                                ", 이 프로그램은 " + std::to_string(domain::kSchemaVersion) +
                                " 까지 읽습니다).");
        return;
    }

    if (found < 1) {
        report.error(where, "형식 버전이 " + std::to_string(found) + " 입니다. 1 이상이어야 합니다.");
        return;
    }

    // 낮은 버전은 단계를 차례로 적용해 올린다.
    for (int from = found; from < domain::kSchemaVersion; ++from) {
        const int index = from - 1;
        if (index >= kStepCount || kSteps[index] == nullptr) {
            report.error(where, "version " + std::to_string(from) + " 에서 " +
                                    std::to_string(from + 1) +
                                    " 로 올리는 방법을 모릅니다. 프로그램을 업데이트해 주세요.");
            return;
        }
        kSteps[index](root);
    }
    root["version"] = domain::kSchemaVersion;
}

}  // namespace storage
