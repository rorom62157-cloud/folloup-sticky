#include "compagnon.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace compagnon {
namespace {
template <std::size_t N>
bool contains(const std::array<Millis, N>& values, Millis value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
bool validBlocker(SleepBlocker value) { return static_cast<unsigned>(value) < 4; }
}
SleepPolicy::SleepPolicy(Millis now) : last_time_(now), last_activity_(now) {}
Result SleepPolicy::configure(SleepConfig c) {
    constexpr std::array<Millis, 10> light{15000,30000,60000,120000,300000,420000,600000,900000,1200000,1800000};
    constexpr std::array<Millis, 9> deep{0,60000,120000,300000,600000,900000,1200000,1800000,3600000};
    if (!contains(light,c.light_ms) || !contains(deep,c.deep_ms) ||
        (c.deep_ms != 0 && c.deep_ms <= c.light_ms)) return Result::InvalidArgument;
    config_ = c;
    return Result::Ok;
}
bool SleepPolicy::advance(Millis now) {
    if (now < last_time_) return false;
    last_time_ = now;
    return true;
}
SleepDecision SleepPolicy::decision(Result r) const { return {state_,screen_,false,false,false,r}; }
Result SleepPolicy::userActivity(Millis now) {
    if (!advance(now)) return Result::InvalidTime;
    if (state_ == SleepState::Deep) return Result::Denied;
    last_activity_ = now;
    state_ = SleepState::Awake;
    screen_ = SleepScreen::Retain;
    return Result::Ok;
}
Result SleepPolicy::block(SleepBlocker b, Millis now, Millis duration) {
    if (!validBlocker(b) || duration == 0 || duration > 300000 ||
        duration > std::numeric_limits<Millis>::max() - now) return Result::InvalidArgument;
    if (!advance(now)) return Result::InvalidTime;
    if (state_ == SleepState::Deep) return Result::Denied;
    blockers_[static_cast<unsigned>(b)] = now + duration;
    return Result::Ok;
}
void SleepPolicy::unblock(SleepBlocker b) { if (validBlocker(b)) blockers_[static_cast<unsigned>(b)].reset(); }
bool SleepPolicy::expireBlockers(Millis now) {
    bool expired = false;
    for (auto& b : blockers_) if (b && now >= *b) { b.reset(); expired = true; }
    return expired;
}
bool SleepPolicy::blocked() const {
    return std::any_of(blockers_.begin(),blockers_.end(),[](const auto& b){ return b.has_value(); });
}
SleepDecision SleepPolicy::evaluate(Millis now, bool post_it) {
    if (!advance(now)) return decision(Result::InvalidTime);
    if (state_ == SleepState::Deep) return decision();
    if (expireBlockers(now)) { auto d = decision(); d.blocker_expired = true; return d; }
    if (blocked()) return decision(Result::Busy);
    auto old = state_;
    const auto idle = now - last_activity_;
    if (config_.deep_ms && idle >= config_.deep_ms) {
        state_ = SleepState::Deep; screen_ = post_it ? SleepScreen::PostIt : SleepScreen::Brand;
    } else if (state_ == SleepState::Awake && idle >= config_.light_ms) {
        state_ = SleepState::Light; screen_ = SleepScreen::Retain;
    }
    auto d = decision(); d.changed = old != state_; return d;
}
SleepDecision SleepPolicy::manualSleep(SleepState target, Millis now, bool post_it) {
    if (!advance(now)) return decision(Result::InvalidTime);
    if (target != SleepState::Light && target != SleepState::Deep) return decision(Result::InvalidArgument);
    if (state_ == SleepState::Deep) return decision(Result::Denied);
    if (expireBlockers(now)) { auto d = decision(Result::Busy); d.blocker_expired = true; return d; }
    if (blocked()) return decision(Result::Busy);
    auto old = state_;
    auto old_screen = screen_;
    state_ = target;
    screen_ = post_it ? SleepScreen::PostIt : (target == SleepState::Light ? SleepScreen::Clock : SleepScreen::Brand);
    auto d = decision(); d.changed = old != state_ || old_screen != screen_; return d;
}
SleepDecision SleepPolicy::wake(WakeSource source, Millis now) {
    if (!advance(now)) return decision(Result::InvalidTime);
    if (source != WakeSource::Physical && source != WakeSource::Technical && source != WakeSource::Companion)
        return decision(Result::InvalidArgument);
    if (state_ == SleepState::Deep && source != WakeSource::Physical) return decision(Result::Denied);
    if (source == WakeSource::Technical) return decision();
    const bool changed = state_ != SleepState::Awake;
    state_ = SleepState::Awake; screen_ = SleepScreen::Retain; last_activity_ = now;
    auto d = decision(); d.changed = changed; d.request_sync = source == WakeSource::Physical;
    return d;
}

std::vector<RecordingAction> RecordingWorkflow::actionsFor(Category c) {
    using A = RecordingAction;
    switch (c) {
    case Category::Note: return {A::KeepAudio,A::Transcribe,A::Reorganize,A::Summarize,A::DraftPostIt};
    case Category::Agenda: return {A::KeepAudio,A::PrepareEvent,A::PrepareEventModification};
    case Category::Task: return {A::KeepAudio,A::Transcribe,A::ExtractTasks};
    case Category::Reminder: return {A::KeepAudio,A::PrepareReminder};
    case Category::PostIt: return {A::KeepAudio,A::PreparePostIt,A::PrepareAndDisplayPostIt};
    case Category::Delete: return {};
    }
    return {};
}
Result RecordingWorkflow::start(std::string id, CaptureMode mode, Millis now) {
    if (phase_ != RecordingPhase::Idle) return Result::Busy;
    if (id.empty() || (mode != CaptureMode::Hold && mode != CaptureMode::Long)) return Result::InvalidArgument;
    if (now < last_time_) return Result::InvalidTime;
    id_ = std::move(id); mode_ = mode; start_ = last_time_ = now; phase_ = RecordingPhase::Capturing;
    return Result::Ok;
}
Result RecordingWorkflow::updateTime(Millis now) {
    if (now < last_time_) return Result::InvalidTime;
    last_time_ = now;
    if (phase_ == RecordingPhase::Capturing) {
        duration_ = std::min(now - start_, max_capture_ms);
        if (duration_ == max_capture_ms) phase_ = RecordingPhase::Saving;
    }
    return Result::Ok;
}
Result RecordingWorkflow::tick(Millis now) { return updateTime(now); }
Result RecordingWorkflow::release(Millis now) {
    auto r = updateTime(now); if (r != Result::Ok) return r;
    if (phase_ == RecordingPhase::Saving) return Result::Ok;
    if (phase_ != RecordingPhase::Capturing) return Result::InvalidState;
    if (mode_ == CaptureMode::Hold) phase_ = RecordingPhase::Saving;
    return Result::Ok;
}
Result RecordingWorkflow::stop(Millis now) {
    auto r = updateTime(now); if (r != Result::Ok) return r;
    if (phase_ == RecordingPhase::Saving) return Result::Ok;
    if (phase_ != RecordingPhase::Capturing) return Result::InvalidState;
    phase_ = RecordingPhase::Saving; return Result::Ok;
}
Result RecordingWorkflow::saved(bool success) {
    if (phase_ != RecordingPhase::Saving) return Result::InvalidState;
    saved_ = success; phase_ = success ? RecordingPhase::TypeMenu : RecordingPhase::SaveFailed; return Result::Ok;
}
Result RecordingWorkflow::retrySave() {
    if (phase_ != RecordingPhase::SaveFailed) return Result::InvalidState;
    phase_ = RecordingPhase::Saving; return Result::Ok;
}
Result RecordingWorkflow::chooseType(Category c) {
    if (phase_ != RecordingPhase::TypeMenu) return Result::InvalidState;
    if (c != Category::Delete && actionsFor(c).empty()) return Result::InvalidArgument;
    category_ = c; phase_ = c == Category::Delete ? RecordingPhase::DeleteConfirmation : RecordingPhase::ActionMenu;
    return Result::Ok;
}
Result RecordingWorkflow::chooseAction(RecordingAction a) {
    if (phase_ != RecordingPhase::ActionMenu || !category_) return Result::InvalidState;
    const auto actions = actionsFor(*category_);
    if (std::find(actions.begin(),actions.end(),a) == actions.end()) return Result::InvalidArgument;
    if (a == RecordingAction::KeepAudio) phase_ = RecordingPhase::Pending;
    else { request_ = ActionRequest{id_,*category_,a}; phase_ = RecordingPhase::ActionRequested; }
    return Result::Ok;
}
Result RecordingWorkflow::back() {
    if (phase_ != RecordingPhase::ActionMenu && phase_ != RecordingPhase::DeleteConfirmation && phase_ != RecordingPhase::DeleteFailed)
        return Result::InvalidState;
    category_.reset(); phase_ = RecordingPhase::TypeMenu; return Result::Ok;
}
Result RecordingWorkflow::leaveMenus() {
    if (phase_ != RecordingPhase::TypeMenu && phase_ != RecordingPhase::ActionMenu &&
        phase_ != RecordingPhase::DeleteConfirmation && phase_ != RecordingPhase::DeleteFailed) return Result::InvalidState;
    if (category_ == Category::Delete) category_.reset();
    phase_ = RecordingPhase::Pending; return Result::Ok;
}
Result RecordingWorkflow::reopenMenus() {
    if (phase_ != RecordingPhase::Pending || !saved_) return Result::InvalidState;
    category_.reset();
    phase_ = RecordingPhase::TypeMenu;
    return Result::Ok;
}
Result RecordingWorkflow::confirmDelete(bool confirmed) {
    if (phase_ != RecordingPhase::DeleteConfirmation && phase_ != RecordingPhase::DeleteFailed) return Result::InvalidState;
    if (!confirmed) return back();
    phase_ = RecordingPhase::Deleting; return Result::Ok;
}
Result RecordingWorkflow::deleted(bool success) {
    if (phase_ != RecordingPhase::Deleting) return Result::InvalidState;
    phase_ = success ? RecordingPhase::Finished : RecordingPhase::DeleteFailed;
    if (success) saved_ = false;
    return Result::Ok;
}
Result RecordingWorkflow::reset() {
    if (phase_ != RecordingPhase::Pending && phase_ != RecordingPhase::ActionRequested && phase_ != RecordingPhase::Finished)
        return Result::InvalidState;
    phase_ = RecordingPhase::Idle; id_.clear(); category_.reset(); request_.reset(); duration_ = 0; saved_ = false;
    return Result::Ok;
}

bool CompanionSession::advance(Millis now) {
    if (now < last_time_) return false;
    last_time_ = now;
    if ((state_ == SessionState::Pending && now - started_ >= request_lifetime_ms) ||
        (state_ == SessionState::Approved && now - active_activity_ >= inactivity_ms)) state_ = SessionState::Expired;
    return true;
}
bool CompanionSession::matches(const std::string& token) const {
    // Compare full length without early exit on a differing character.
    if (token.size() != token_.size() || token.empty()) return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < token.size(); ++i) difference |= static_cast<unsigned char>(token[i]) ^ static_cast<unsigned char>(token_[i]);
    return difference == 0;
}
Result CompanionSession::request(std::string token, Millis now) {
    if (!advance(now)) return Result::InvalidTime;
    if (state_ == SessionState::Pending || state_ == SessionState::Approved) return Result::Busy;
    if (token.empty() || token.size() > 256) return Result::InvalidArgument;
    // Refuse immediate reuse of the previous capability, including after expiry.
    if (matches(token)) return Result::InvalidArgument;
    token_ = std::move(token); started_ = now; state_ = SessionState::Pending; return Result::Ok;
}
Result CompanionSession::physicalApprove(Millis now) {
    if (!advance(now)) return Result::InvalidTime;
    if (state_ != SessionState::Pending) return Result::InvalidState;
    state_ = SessionState::Approved; active_activity_ = now; return Result::Ok;
}
Result CompanionSession::physicalReject(Millis now) {
    if (!advance(now)) return Result::InvalidTime;
    if (state_ != SessionState::Pending) return Result::InvalidState;
    state_ = SessionState::Denied; return Result::Ok;
}
Result CompanionSession::revoke(Millis now) {
    if (!advance(now)) return Result::InvalidTime;
    state_ = SessionState::Denied; return Result::Ok;
}
SessionState CompanionSession::status(const std::string& token, Millis now) {
    if (!advance(now) || !matches(token)) return SessionState::Denied;
    return state_;
}
bool CompanionSession::authorizePrivate(const std::string& token, Millis now) {
    return status(token,now) == SessionState::Approved;
}
Result CompanionSession::acceptedUserActivity(const std::string& token, Millis now) {
    if (!advance(now)) return Result::InvalidTime;
    if (!matches(token) || state_ != SessionState::Approved) return Result::Denied;
    active_activity_ = now; return Result::Ok;
}
} // namespace compagnon
