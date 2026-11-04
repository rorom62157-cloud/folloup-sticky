#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Caller serializes access. Times are monotonic milliseconds, never wall time.
// This layer emits decisions only: adapters own persistence, audio and networking.
namespace compagnon {
using Millis = std::uint64_t;

enum class Result { Ok, InvalidArgument, InvalidTime, InvalidState, Busy, Denied };
enum class SleepState { Awake, Light, Deep };
enum class SleepScreen { Retain, Clock, PostIt, Brand };
enum class WakeSource { Physical, Technical, Companion };
enum class SleepBlocker { Recording, Saving, Update, CriticalTransfer };
struct SleepConfig { Millis light_ms = 30000; Millis deep_ms = 600000; };
struct SleepDecision {
    SleepState state;
    SleepScreen screen;
    bool changed = false;
    bool request_sync = false;
    bool blocker_expired = false;
    Result result = Result::Ok;
};
class SleepPolicy {
public:
    explicit SleepPolicy(Millis now = 0);
    Result configure(SleepConfig config);
    Result userActivity(Millis now);
    // Leases are bounded to five minutes. Expiry reports a fault and suppresses
    // sleep for that evaluation; adapter must stop/recover the critical operation.
    Result block(SleepBlocker blocker, Millis now, Millis duration_ms);
    void unblock(SleepBlocker blocker);
    SleepDecision evaluate(Millis now, bool post_it);
    SleepDecision manualSleep(SleepState target, Millis now, bool post_it);
    SleepDecision wake(WakeSource source, Millis now);
    SleepState state() const { return state_; }
    SleepConfig config() const { return config_; }
private:
    bool advance(Millis now);
    bool expireBlockers(Millis now);
    bool blocked() const;
    SleepDecision decision(Result result = Result::Ok) const;
    SleepConfig config_;
    Millis last_time_, last_activity_;
    SleepState state_ = SleepState::Awake;
    SleepScreen screen_ = SleepScreen::Retain;
    std::array<std::optional<Millis>, 4> blockers_{};
};

enum class CaptureMode { Hold, Long };
enum class RecordingPhase {
    Idle, Capturing, Saving, SaveFailed, TypeMenu, ActionMenu,
    DeleteConfirmation, Deleting, DeleteFailed, Pending, ActionRequested, Finished
};
enum class Category { Note, Agenda, Task, Reminder, PostIt, Delete };
enum class RecordingAction {
    KeepAudio, Transcribe, Reorganize, Summarize, DraftPostIt,
    PrepareEvent, PrepareEventModification, ExtractTasks, PrepareReminder,
    PreparePostIt, PrepareAndDisplayPostIt
};
struct ActionRequest {
    std::string recording_id;
    Category category;
    RecordingAction action;
    // A proposal, never permission to run AI or execute an external mutation.
};
class RecordingWorkflow {
public:
    static constexpr Millis max_capture_ms = 240000;
    static std::vector<RecordingAction> actionsFor(Category category);
    Result start(std::string recording_id, CaptureMode mode, Millis now);
    Result release(Millis now); // Stops only hold capture.
    Result stop(Millis now);    // Explicit stop for either mode.
    Result tick(Millis now);    // At 4 minutes emits Saving, even without input.
    Result saved(bool success); // Success means original durably saved by adapter.
    Result retrySave();
    Result chooseType(Category category);
    Result chooseAction(RecordingAction action);
    Result back();
    Result leaveMenus(); // No selection: original remains pending.
    Result reopenMenus(); // Reclassify this saved pending original without recapture.
    Result confirmDelete(bool confirmed);
    Result deleted(bool success); // Only after an adapter actually deletes.
    // Caller must persist pending metadata / requested work before reset.
    Result reset();
    RecordingPhase phase() const { return phase_; }
    const std::string& recordingId() const { return id_; }
    std::optional<Category> category() const { return category_; }
    const std::optional<ActionRequest>& actionRequest() const { return request_; }
    Millis durationMs() const { return duration_; }
    bool originalSaved() const { return saved_; }
private:
    Result updateTime(Millis now);
    RecordingPhase phase_ = RecordingPhase::Idle;
    CaptureMode mode_ = CaptureMode::Hold;
    std::string id_;
    std::optional<Category> category_;
    std::optional<ActionRequest> request_;
    Millis start_ = 0, last_time_ = 0, duration_ = 0;
    bool saved_ = false;
};

enum class SessionState { Denied, Pending, Approved, Expired };
class CompanionSession {
public:
    static constexpr Millis request_lifetime_ms = 60000;
    static constexpr Millis inactivity_ms = 600000;
    // Adapter supplies an unpredictable opaque token (>=128 bits entropy),
    // binds it to a secure cookie and implements CSRF/origin/transport controls.
    // Tokens are never exposed through state/accessor APIs.
    Result request(std::string token, Millis now);
    Result physicalApprove(Millis now);
    Result physicalReject(Millis now);
    Result revoke(Millis now);
    SessionState status(const std::string& token, Millis now);
    bool authorizePrivate(const std::string& token, Millis now);
    // Only call after an authenticated, accepted user mutation; polling cannot
    // renew session lifetime. Authorization must precede actual side effects.
    Result acceptedUserActivity(const std::string& token, Millis now);
private:
    bool advance(Millis now);
    bool matches(const std::string& token) const;
    std::string token_;
    SessionState state_ = SessionState::Denied;
    Millis last_time_ = 0, started_ = 0, active_activity_ = 0;
};
} // namespace compagnon
