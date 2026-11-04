#include "compagnon.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace compagnon;

static void sleepTests() {
    SleepPolicy sleep;
    assert(sleep.configure({30000,30000}) == Result::InvalidArgument);
    assert(sleep.configure({31000,600000}) == Result::InvalidArgument);
    assert(sleep.evaluate(29999,false).state == SleepState::Awake);
    auto d = sleep.evaluate(30000,false);
    assert(d.changed && d.state == SleepState::Light && d.screen == SleepScreen::Retain);
    assert(!sleep.evaluate(30001,false).changed);
    // Technical wake / polling never resets the user's inactivity deadline.
    assert(!sleep.wake(WakeSource::Technical,500000).request_sync);
    d = sleep.evaluate(600000,false);
    assert(d.state == SleepState::Deep && d.screen == SleepScreen::Brand);
    assert(sleep.wake(WakeSource::Technical,600001).result == Result::Denied);
    assert(sleep.wake(WakeSource::Companion,600002).result == Result::Denied);
    assert(sleep.userActivity(600003) == Result::Denied);
    d = sleep.wake(WakeSource::Physical,600004);
    assert(d.changed && d.request_sync && d.state == SleepState::Awake);
    assert(sleep.evaluate(630003,true).state == SleepState::Awake);
    assert(sleep.evaluate(630004,true).state == SleepState::Light);
    assert(sleep.evaluate(630003,true).result == Result::InvalidTime);
    assert(sleep.userActivity(630005) == Result::Ok);
    assert(sleep.manualSleep(SleepState::Light,630006,false).screen == SleepScreen::Clock);
    assert(sleep.manualSleep(SleepState::Light,630007,true).screen == SleepScreen::PostIt);
    assert(sleep.manualSleep(SleepState::Deep,630008,true).screen == SleepScreen::PostIt);
    assert(sleep.configure({30000,0}) == Result::Ok);
    sleep.wake(WakeSource::Physical,630009);
    assert(sleep.evaluate(999999999,false).state == SleepState::Light);

    SleepPolicy blocked;
    assert(blocked.block(SleepBlocker::Recording,0,240001) == Result::Ok);
    assert(blocked.block(SleepBlocker::Saving,0,300001) == Result::InvalidArgument);
    assert(blocked.block(static_cast<SleepBlocker>(8),0,1) == Result::InvalidArgument);
    assert(blocked.evaluate(30000,false).result == Result::Busy);
    assert(blocked.manualSleep(SleepState::Deep,30001,false).result == Result::Busy);
    d = blocked.evaluate(240001,false);
    assert(d.blocker_expired && d.state == SleepState::Awake);
    assert(blocked.evaluate(240002,false).state == SleepState::Light);
    assert(blocked.block(SleepBlocker::Saving,240003,1000) == Result::Ok);
    blocked.unblock(SleepBlocker::Saving);
    assert(blocked.manualSleep(SleepState::Deep,240004,false).state == SleepState::Deep);
    SleepPolicy screen_change;
    assert(screen_change.evaluate(30000,false).screen == SleepScreen::Retain);
    d = screen_change.manualSleep(SleepState::Light,30001,false);
    assert(d.state == SleepState::Light && d.screen == SleepScreen::Clock && d.changed);
    assert(!screen_change.manualSleep(SleepState::Light,30002,false).changed);
    SleepPolicy overflow;
    assert(overflow.block(SleepBlocker::Update,std::numeric_limits<Millis>::max(),1) == Result::InvalidArgument);
}

static void recordingTests() {
    RecordingWorkflow r;
    assert(r.reopenMenus() == Result::InvalidState);
    assert(r.chooseType(Category::Note) == Result::InvalidState);
    assert(r.start("",CaptureMode::Hold,0) == Result::InvalidArgument);
    assert(r.start("audio-1",CaptureMode::Hold,100) == Result::Ok);
    assert(r.reopenMenus() == Result::InvalidState);
    assert(r.start("audio-2",CaptureMode::Long,101) == Result::Busy);
    assert(r.reset() == Result::InvalidState);
    assert(r.tick(99) == Result::InvalidTime);
    assert(r.release(1100) == Result::Ok);
    assert(r.phase() == RecordingPhase::Saving && r.durationMs() == 1000);
    assert(!r.originalSaved());
    assert(r.chooseType(Category::Delete) == Result::InvalidState);
    assert(r.saved(false) == Result::Ok);
    assert(r.phase() == RecordingPhase::SaveFailed);
    assert(r.reopenMenus() == Result::InvalidState);
    assert(r.leaveMenus() == Result::InvalidState);
    assert(r.retrySave() == Result::Ok && r.saved(true) == Result::Ok);
    assert(r.originalSaved() && r.phase() == RecordingPhase::TypeMenu);
    assert(r.chooseType(Category::Note) == Result::Ok);
    assert(r.phase() == RecordingPhase::ActionMenu);
    assert(r.chooseAction(RecordingAction::PrepareEvent) == Result::InvalidArgument);
    assert(r.back() == Result::Ok && !r.category());
    assert(r.chooseType(Category::Agenda) == Result::Ok);
    assert(r.chooseAction(RecordingAction::PrepareEventModification) == Result::Ok);
    assert(r.phase() == RecordingPhase::ActionRequested);
    assert(r.reopenMenus() == Result::InvalidState);
    assert(r.actionRequest()->recording_id == "audio-1");
    assert(r.actionRequest()->category == Category::Agenda && r.originalSaved());
    assert(r.reset() == Result::Ok);

    assert(r.start("audio-long",CaptureMode::Long,2000) == Result::Ok);
    assert(r.release(3000) == Result::Ok && r.phase() == RecordingPhase::Capturing);
    assert(r.tick(241999) == Result::Ok && r.phase() == RecordingPhase::Capturing);
    assert(r.tick(242000) == Result::Ok && r.phase() == RecordingPhase::Saving);
    assert(r.durationMs() == 240000);
    assert(r.saved(true) == Result::Ok);
    assert(r.leaveMenus() == Result::Ok && r.phase() == RecordingPhase::Pending);
    assert(!r.actionRequest() && r.originalSaved());
    assert(r.reopenMenus() == Result::Ok && r.phase() == RecordingPhase::TypeMenu);
    assert(r.recordingId() == "audio-long" && r.durationMs() == 240000 && r.originalSaved());
    assert(r.reopenMenus() == Result::InvalidState);
    assert(r.chooseType(Category::Note) == Result::Ok);
    assert(r.reopenMenus() == Result::InvalidState);
    assert(r.chooseAction(RecordingAction::KeepAudio) == Result::Ok);
    assert(r.category() == Category::Note);
    assert(r.reopenMenus() == Result::Ok && !r.category());
    assert(r.chooseType(Category::Task) == Result::Ok);
    assert(r.chooseAction(RecordingAction::Transcribe) == Result::Ok);
    assert(r.actionRequest()->recording_id == "audio-long" && r.originalSaved());
    assert(r.reset() == Result::Ok);
    assert(r.start("audio-delete",CaptureMode::Hold,300000) == Result::Ok);
    assert(r.stop(300010) == Result::Ok && r.saved(true) == Result::Ok);
    assert(r.chooseType(Category::Delete) == Result::Ok);
    assert(r.phase() == RecordingPhase::DeleteConfirmation && r.originalSaved());
    assert(r.deleted(true) == Result::InvalidState);
    assert(r.confirmDelete(false) == Result::Ok && r.phase() == RecordingPhase::TypeMenu);
    assert(r.chooseType(Category::Delete) == Result::Ok && r.confirmDelete(true) == Result::Ok);
    assert(r.originalSaved() && r.phase() == RecordingPhase::Deleting);
    assert(r.deleted(false) == Result::Ok && r.originalSaved());
    assert(r.confirmDelete(true) == Result::Ok && r.deleted(true) == Result::Ok);
    assert(!r.originalSaved() && r.phase() == RecordingPhase::Finished);
    assert(r.reset() == Result::Ok);
    assert(r.start("late-tick",CaptureMode::Hold,400000) == Result::Ok);
    assert(r.tick(900000) == Result::Ok && r.durationMs() == 240000);
    assert(r.saved(true) == Result::Ok && r.chooseType(Category::Task) == Result::Ok);
    assert(r.chooseAction(RecordingAction::KeepAudio) == Result::Ok);
    assert(r.phase() == RecordingPhase::Pending && !r.actionRequest());
    assert(RecordingWorkflow::actionsFor(Category::Note).size() == 5);
    assert(RecordingWorkflow::actionsFor(Category::Reminder).size() == 2);
    assert(RecordingWorkflow::actionsFor(Category::PostIt).size() == 3);
    assert(RecordingWorkflow::actionsFor(Category::Delete).empty());
}

static void sessionTests() {
    const std::string a = "adapter-random-capability-A", b = "adapter-random-capability-B";
    CompanionSession session;
    assert(!session.authorizePrivate(a,0));
    assert(session.physicalApprove(0) == Result::InvalidState);
    assert(session.request("",0) == Result::InvalidArgument);
    assert(session.request(a,0) == Result::Ok);
    assert(session.status(a,1) == SessionState::Pending);
    assert(!session.authorizePrivate(a,1));
    assert(session.request(b,2) == Result::Busy);
    assert(session.acceptedUserActivity(a,3) == Result::Denied);
    assert(session.physicalApprove(59999) == Result::Ok);
    assert(session.authorizePrivate(a,60000));
    assert(!session.authorizePrivate(b,60000));
    assert(session.request(b,60000) == Result::Busy);
    assert(session.acceptedUserActivity(b,60000) == Result::Denied);
    assert(session.authorizePrivate(a,659998));
    assert(!session.authorizePrivate(a,659999)); // polls did not renew
    assert(session.status(a,659999) == SessionState::Expired);
    assert(session.request(a,659999) == Result::InvalidArgument);
    assert(session.request(b,660000) == Result::Ok);
    assert(session.physicalApprove(720000) == Result::InvalidState); // exact 60s expiry
    assert(!session.authorizePrivate(b,720000));
    assert(session.request(a,720001) == Result::Ok);
    assert(session.physicalReject(720002) == Result::Ok);
    assert(session.status(a,720002) == SessionState::Denied);
    assert(session.request(b,720003) == Result::Ok);
    assert(session.physicalApprove(720004) == Result::Ok);
    assert(session.acceptedUserActivity(b,1000000) == Result::Ok);
    assert(session.authorizePrivate(b,1599999));
    assert(!session.authorizePrivate(b,1599998)); // backward clock fails closed
    assert(session.authorizePrivate(b,1599999));
    assert(session.revoke(1599999) == Result::Ok);
    assert(!session.authorizePrivate(b,1599999));
    assert(!session.authorizePrivate(a,1599999));
    assert(session.request(a,1600000) == Result::Ok);
    assert(session.physicalApprove(1600001) == Result::Ok);
    assert(session.acceptedUserActivity(a,2200001) == Result::Denied); // expiry cannot renew
}

int main() {
    sleepTests(); recordingTests(); sessionTests();
    std::cout << "core_tests: all assertions passed\n";
}
