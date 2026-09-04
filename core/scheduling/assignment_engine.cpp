#include "core/scheduling/assignment_engine.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace scheduling {
namespace {

// 정책을 넘기지 않은 엔진이 쓰는 기본값.
const RoundRobinPolicy& defaultPolicy() {
    static const RoundRobinPolicy kPolicy;
    return kPolicy;
}

using domain::Task;
using domain::TaskId;
using domain::WorkerId;

// 배타 그래프. conflictsWith 가 한쪽에만 적혀 있어도 양쪽에 적용해야 한다.
// storage 가 로드할 때 정규화하지만 엔진은 그것에 기대지 않는다 — 손으로 만든 입력도 옳게
// 동작해야 하고, 대칭성은 엔진이 지켜야 할 규칙이기 때문이다.
class ConflictGraph {
public:
    explicit ConflictGraph(const std::vector<Task>& tasks) {
        for (const Task& task : tasks) {
            edges_[task.id];  // 배타가 없는 작업도 자리를 만들어 둔다
            for (const TaskId& other : task.conflictsWith) {
                if (other == task.id) {
                    // 자기 자신과의 배타는 암묵적이다. Ledger 가 같은 작업 재배정을 막는다.
                    continue;
                }
                edges_[task.id].insert(other);
                edges_[other].insert(task.id);
            }
        }
    }

    bool conflicts(const TaskId& a, const TaskId& b) const {
        const auto it = edges_.find(a);
        return it != edges_.end() && it->second.count(b) > 0;
    }

    const std::set<TaskId>& neighbours(const TaskId& id) const {
        static const std::set<TaskId> kEmpty;
        const auto it = edges_.find(id);
        return (it != edges_.end()) ? it->second : kEmpty;
    }

private:
    std::map<TaskId, std::set<TaskId>> edges_;
};

// 이번 호출로 채울 작업. 작업집합에 속하고 그날 요일에 해당하는 것만.
// 요일에서 빠진 작업은 미배정으로도 기록하지 않는다 — 오늘 존재하지 않는 일이다.
std::vector<const Task*> targetTasks(const AssignInput& input) {
    const domain::Weekday day = domain::fromChrono(input.date.weekday());
    std::vector<const Task*> out;
    for (const Task& task : input.slotTasks) {
        const bool inSet = std::find(task.taskSetIds.begin(), task.taskSetIds.end(),
                                     input.taskSetId) != task.taskSetIds.end();
        if (!inSet) {
            continue;
        }
        // 비어 있으면 근무일 전체라는 뜻이다.
        if (!task.weekdays.empty() && !domain::contains(task.weekdays, day)) {
            continue;
        }
        out.push_back(&task);
    }
    return out;
}

// 배타 그래프의 연결 요소. 제약이 강한 그룹부터 배정해야 실패가 줄어든다 (DESIGN 3.2).
struct Group {
    std::vector<const Task*> tasks;  // 정의 순서
    int requiredSum{0};
    std::size_t firstIndex{0};  // 그룹 내 첫 작업의 정의 순서. 필요 인원이 같을 때의 기준
};

std::vector<Group> splitIntoGroups(const std::vector<const Task*>& tasks,
                                   const ConflictGraph& graph) {
    std::map<TaskId, std::size_t> indexOf;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        indexOf.emplace(tasks[i]->id, i);
    }

    std::set<TaskId> visited;
    std::vector<Group> groups;
    for (const Task* start : tasks) {
        if (visited.count(start->id) > 0) {
            continue;
        }
        // 연결 요소를 모은다. 대상 작업 밖으로는 나가지 않는다.
        std::set<TaskId> component;
        std::vector<TaskId> stack{start->id};
        while (!stack.empty()) {
            const TaskId current = stack.back();
            stack.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            component.insert(current);
            for (const TaskId& neighbour : graph.neighbours(current)) {
                if (indexOf.count(neighbour) > 0 && visited.count(neighbour) == 0) {
                    stack.push_back(neighbour);
                }
            }
        }

        Group group;
        group.firstIndex = tasks.size();
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            if (component.count(tasks[i]->id) > 0) {
                group.tasks.push_back(tasks[i]);
                group.requiredSum += std::max(tasks[i]->requiredCount, 0);
                group.firstIndex = std::min(group.firstIndex, i);
            }
        }
        groups.push_back(std::move(group));
    }

    // 필요 인원 합 내림차순. 같으면 그룹 내 첫 작업의 정의 순서.
    // stable_sort 라 같은 입력이면 항상 같은 순서가 나온다.
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        if (a.requiredSum != b.requiredSum) {
            return a.requiredSum > b.requiredSum;
        }
        return a.firstIndex < b.firstIndex;
    });
    return groups;
}

// 배정 상태. 누가 어느 작업을 맡았는지와, 시간대 안에서 몇 건을 맡았는지.
// 키를 강타입으로 두어 WorkerId 와 TaskId 를 바꿔 넣으면 컴파일이 막는다 (CLAUDE.md 7장).
class Ledger {
public:
    void add(const WorkerId& workerId, const TaskId& taskId) {
        byWorker_[workerId].insert(taskId);
        load_[workerId] += 1;
    }

    bool holds(const WorkerId& workerId, const TaskId& taskId) const {
        const auto it = byWorker_.find(workerId);
        return it != byWorker_.end() && it->second.count(taskId) > 0;
    }

    // taskId 와 배타인 작업을 이미 맡고 있는가.
    bool blockedBy(const WorkerId& workerId, const TaskId& taskId,
                   const ConflictGraph& graph) const {
        const auto it = byWorker_.find(workerId);
        if (it == byWorker_.end()) {
            return false;
        }
        for (const TaskId& held : it->second) {
            if (graph.conflicts(held, taskId)) {
                return true;
            }
        }
        return false;
    }

    int load(const WorkerId& workerId) const {
        const auto it = load_.find(workerId);
        return (it != load_.end()) ? it->second : 0;
    }

private:
    std::map<WorkerId, std::set<TaskId>> byWorker_;
    std::map<WorkerId, int> load_;
};

}  // namespace

AssignmentEngine::AssignmentEngine() : policy_(&defaultPolicy()) {}

AssignmentEngine::AssignmentEngine(const IAssignmentPolicy& policy) : policy_(&policy) {}

AssignResult AssignmentEngine::assign(const AssignInput& input) const {
    AssignResult result;

    const int workerCount = static_cast<int>(input.availableWorkers.size());
    if (workerCount <= 0) {
        // 가용 인원이 없으면 전부 미배정이다. 크래시하거나 예외를 던지지 않는다.
        result.nextCursor = input.cursor;
        for (const Task* task : targetTasks(input)) {
            for (int i = 0; i < std::max(task->requiredCount, 0); ++i) {
                result.unassigned.push_back(Unassigned{task->id});
            }
        }
        return result;
    }

    // 커서가 작업자 수를 벗어나 있어도 안전하게 감싼다. 작업자를 지우면 인덱스가 어긋난다.
    int cursor = ((input.cursor % workerCount) + workerCount) % workerCount;

    const ConflictGraph graph{input.slotTasks};

    Ledger ledger;
    // 검사 범위가 시간대 전체로 넓어졌을 때만 앞선 작업집합의 배정을 함께 본다 (D-002).
    // false 면 작업집합끼리 독립이므로 부하 계산에서도 빼야 한다 — 한쪽 집합의 선택이
    // 다른 집합의 선택을 좌우하면 "독립" 이 아니게 된다.
    if (input.options.crossSetConflicts) {
        for (const Assignment& prior : input.priorAssignments) {
            ledger.add(prior.workerId, prior.taskId);
        }
    }

    const std::vector<const Task*> targets = targetTasks(input);
    for (const Group& group : splitIntoGroups(targets, graph)) {
        for (const Task* task : group.tasks) {
            for (int filled = 0; filled < task->requiredCount; ++filled) {
                // 커서 위치부터 한 바퀴 돌며 가장 점수가 낮은 적격자를 고른다.
                int chosen = -1;
                int chosenScore = 0;
                for (int step = 0; step < workerCount; ++step) {
                    const int index = (cursor + step) % workerCount;
                    const domain::Worker& candidate =
                        input.availableWorkers[static_cast<std::size_t>(index)];

                    if (ledger.holds(candidate.id, task->id)) {
                        continue;  // 모든 작업은 암묵적으로 자기 자신과 배타다
                    }
                    if (ledger.blockedBy(candidate.id, task->id, graph)) {
                        continue;
                    }

                    // 점수가 낮을수록 먼저. 같으면 커서에 가까운 사람이 이긴다 —
                    // 순회를 커서에서 시작하므로 먼저 만난 쪽을 유지하면 된다.
                    PolicyContext context;
                    context.cursorDistance = step;
                    context.slotLoad = ledger.load(candidate.id);
                    const int score = policy_->score(candidate, *task, context);
                    if (chosen < 0 || score < chosenScore) {
                        chosen = index;
                        chosenScore = score;
                    }
                }

                if (chosen < 0) {
                    // 한 바퀴를 다 돌아도 적격자가 없다. 미배정으로 기록하고 넘어간다.
                    // 커서는 그대로 둔다 — 한 바퀴는 정확히 N칸이라 제자리로 돌아온 것과 같다.
                    result.unassigned.push_back(Unassigned{task->id});
                    continue;
                }

                const domain::Worker& worker =
                    input.availableWorkers[static_cast<std::size_t>(chosen)];
                result.assignments.push_back(Assignment{task->id, worker.id});
                ledger.add(worker.id, task->id);
                cursor = (chosen + 1) % workerCount;
            }
        }
    }

    result.nextCursor = cursor;
    return result;
}

}  // namespace scheduling
