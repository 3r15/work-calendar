#include "core/domain/snapshot.h"

namespace domain {

int countUnassigned(const DaySnapshot& snapshot) {
    int total = 0;
    for (const SnapshotSlot& slot : snapshot.slots) {
        for (const SnapshotTaskSet& set : slot.taskSets) {
            for (const SnapshotUnassigned& item : set.unassigned) {
                total += item.count;
            }
        }
    }
    return total;
}

int countAbsentAssignees(const DaySnapshot& snapshot) {
    int total = 0;
    for (const SnapshotSlot& slot : snapshot.slots) {
        for (const SnapshotTaskSet& set : slot.taskSets) {
            for (const SnapshotAssignment& assignment : set.assignments) {
                if (assignment.absentAssignee) {
                    total += 1;
                }
            }
        }
    }
    return total;
}

}  // namespace domain
