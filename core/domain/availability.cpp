#include "core/domain/availability.h"

namespace domain {

bool isAbsentOn(const Model& model, const WorkerId& workerId, const util::Date& date) {
    const std::string text = date.toString();
    for (const Absence& absence : model.absences.absences) {
        if (!(absence.workerId == workerId)) {
            continue;
        }
        // 날짜가 "YYYY-MM-DD" 로 0 을 채운 형식이라 문자열 비교가 곧 날짜 비교다.
        if (absence.from <= text && text <= absence.to) {
            return true;
        }
    }
    return false;
}

std::vector<Worker> availableWorkers(const Model& model, const util::Date& date) {
    const Weekday day = fromChrono(date.weekday());
    std::vector<Worker> out;
    for (const Worker& worker : model.workers.workers) {
        if (!worker.active) {
            continue;
        }
        if (contains(worker.weeklyOff, day)) {
            continue;
        }
        if (isAbsentOn(model, worker.id, date)) {
            continue;
        }
        out.push_back(worker);
    }
    return out;
}

}  // namespace domain
