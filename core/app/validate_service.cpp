#include "core/app/validate_service.h"

#include "core/domain/validator.h"
#include "core/storage/json_io.h"

#include <utility>

namespace app {

util::Result<domain::Report> validateDataDir(const std::filesystem::path& dataDir) {
    util::Result<storage::LoadedModel> loaded = storage::loadModel(dataDir);
    if (!loaded) {
        return loaded.error();
    }

    storage::LoadedModel data = std::move(loaded).value();

    // 파싱 단계에서 나온 발견(알 수 없는 요일 코드, version 범위)과 의미 검사 결과를 합친다.
    domain::Report report = std::move(data.report);
    report.merge(domain::validate(data.model));
    return report;
}

}  // namespace app
