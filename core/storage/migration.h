#pragma once

#include "core/domain/finding.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace storage {

// 업데이트 뒤 첫 실행에서 스키마 버전을 확인하고 필요하면 올린다 (DESIGN 7.2).
//
// 지금은 버전이 1 하나뿐이라 올릴 것이 없다. 그래도 자리를 만들어 두는 이유는, 스키마가
// 바뀌는 순간 로드 경로 전체를 뒤지지 않고 여기에만 단계를 추가하면 되게 하려는 것이다.
// priority 와 done 필드를 미리 넣어 둔 것과 같은 이유다 (D-006, D-010).
//
// 방향에 따라 할 수 있는 일이 다르다.
//   낮은 버전 → 단계를 차례로 적용해 올린다
//   같은 버전 → 아무것도 하지 않는다
//   높은 버전 → 올릴 수 없다. 이 프로그램이 더 오래된 것이므로 그렇게 알린다
//
// 고칠 수 없는 문제는 report 에 오류로 남긴다. 파싱은 계속되므로 다른 문제도 함께 보고된다.
void migrateIfNeeded(nlohmann::json& root, const std::string& where, domain::Report& report);

}  // namespace storage
