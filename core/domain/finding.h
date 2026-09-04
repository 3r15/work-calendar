#pragma once

#include <string>
#include <vector>

namespace domain {

enum class Severity {
    Error,    // 이 상태로는 실행할 수 없다
    Warning,  // 실행은 되지만 의도한 결과가 아닐 수 있다
};

// 검증에서 발견한 문제 하나. 사용자에게 그대로 보여줄 수 있는 한국어 문장이어야 한다.
struct Finding {
    Severity severity{Severity::Error};
    std::string where;    // "tasks.json" 처럼 어느 파일인지. 파일과 무관하면 비워 둔다
    std::string message;  // 무엇이 잘못됐는지
};

// 첫 오류에서 멈추지 않고 발견한 것을 전부 모은다 (DATA-SCHEMA.md 검증 규칙).
class Report {
public:
    void add(Severity severity, std::string where, std::string message) {
        findings_.push_back(Finding{severity, std::move(where), std::move(message)});
    }
    void error(std::string where, std::string message) {
        add(Severity::Error, std::move(where), std::move(message));
    }
    void warn(std::string where, std::string message) {
        add(Severity::Warning, std::move(where), std::move(message));
    }
    void merge(const Report& other) {
        findings_.insert(findings_.end(), other.findings_.begin(), other.findings_.end());
    }

    const std::vector<Finding>& findings() const noexcept { return findings_; }
    std::size_t count(Severity severity) const {
        std::size_t n = 0;
        for (const Finding& f : findings_) {
            if (f.severity == severity) {
                ++n;
            }
        }
        return n;
    }
    std::size_t errorCount() const { return count(Severity::Error); }
    std::size_t warningCount() const { return count(Severity::Warning); }
    bool hasErrors() const { return errorCount() > 0; }
    bool empty() const noexcept { return findings_.empty(); }

private:
    std::vector<Finding> findings_;
};

}  // namespace domain
