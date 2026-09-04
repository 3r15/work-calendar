#pragma once

#include "core/domain/entities.h"
#include "core/util/date.h"

#include <vector>

namespace domain {

// 그날 배정 대상이 되는 작업자.
//
//   active && 그날이 정기 휴무가 아님 && 그날 휴무가 등록되어 있지 않음
//
// **정의 순서를 그대로 유지한다.** 이 순서가 라운드 로빈의 기준이므로 정렬하면 결정론이 깨진다.
std::vector<Worker> availableWorkers(const Model& model, const util::Date& date);

// 그날 휴무가 등록되어 있는가. 기간이 겹쳐도 오류가 아니라 합집합이다.
bool isAbsentOn(const Model& model, const WorkerId& workerId, const util::Date& date);

}  // namespace domain
