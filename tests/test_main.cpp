#include "core/concurrency/latest_atomic.hpp"
#include "core/concurrency/spsc_ring.hpp"
#include "core/diagnostics/diagnostics.hpp"
#include "core/result/result.hpp"
#include "core/session/session_state_machine.hpp"
#include "core/time/monotonic_clock.hpp"

#include <cstdint>
#include <iostream>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void test_result(TestContext &test)
{
    using namespace arssyut::core;

    auto good = Result<int>::success(42);
    test.expect(good.has_value(), "Result success contains value");
    test.expect(good.value() == 42, "Result success preserves value");
    test.expect(good.status().ok(), "Result success status is OK");

    auto bad = Result<int>::failure(
        Status::failure(StatusCode::InvalidArgument, 7));
    test.expect(!bad.has_value(), "Result failure has no value");
    test.expect(
        bad.status().code == StatusCode::InvalidArgument,
        "Result failure preserves status code");
    test.expect(bad.status().detail == 7, "Result failure preserves detail");
}

void test_spsc_ring(TestContext &test)
{
    arssyut::core::SpscRing<int, 3> ring;

    test.expect(ring.capacity() == 3, "SPSC reports fixed capacity");
    test.expect(ring.try_push(10), "SPSC accepts item 1");
    test.expect(ring.try_push(20), "SPSC accepts item 2");
    test.expect(ring.try_push(30), "SPSC accepts item 3");
    test.expect(!ring.try_push(40), "SPSC rejects overflow");
    test.expect(ring.size_approx() == 3, "SPSC size is bounded");

    int value = 0;
    test.expect(ring.try_pop(value) && value == 10, "SPSC preserves item 1");
    test.expect(ring.try_pop(value) && value == 20, "SPSC preserves item 2");
    test.expect(ring.try_pop(value) && value == 30, "SPSC preserves item 3");
    test.expect(!ring.try_pop(value), "SPSC reports empty");
}

void test_latest_atomic(TestContext &test)
{
    arssyut::core::LatestAtomic<std::uint64_t> latest;

    std::uint64_t seen = 0;
    std::uint64_t value = 0;
    test.expect(
        !latest.read_if_new(seen, value),
        "LatestAtomic has no initial publication");

    latest.publish(100);
    latest.publish(200);

    test.expect(
        latest.read_if_new(seen, value),
        "LatestAtomic exposes newest publication");
    test.expect(value == 200, "LatestAtomic is latest-wins");
    test.expect(seen == 2, "LatestAtomic advances generation");
    test.expect(
        !latest.read_if_new(seen, value),
        "LatestAtomic suppresses unchanged state");
}

void test_diagnostics(TestContext &test)
{
    using namespace arssyut::core;

    Diagnostics diagnostics;
    diagnostics.increment(DiagnosticMetric::CaptureFramesReceived);
    diagnostics.increment(DiagnosticMetric::CaptureFramesReceived, 2);
    diagnostics.observe_max(DiagnosticMetric::MaxVideoQueueDepth, 3);
    diagnostics.observe_max(DiagnosticMetric::MaxVideoQueueDepth, 2);
    diagnostics.observe_max(DiagnosticMetric::MaxVideoQueueDepth, 7);

    test.expect(
        diagnostics.load(DiagnosticMetric::CaptureFramesReceived) == 3,
        "Diagnostics counter accumulates");
    test.expect(
        diagnostics.load(DiagnosticMetric::MaxVideoQueueDepth) == 7,
        "Diagnostics max metric is monotonic");
}

void test_clock(TestContext &test)
{
    using arssyut::core::MonotonicClock;

    const auto first = MonotonicClock::now();
    auto previous = first;

    for (int i = 0; i < 1000; ++i) {
        const auto current = MonotonicClock::now();
        test.expect(
            !(current < previous),
            "Monotonic clock never moves backwards");
        previous = current;
    }

    test.expect(
        MonotonicClock::duration_ticks(first, previous) >= 0,
        "Monotonic duration is non-negative");
}

void test_session_state_machine(TestContext &test)
{
    using namespace arssyut::core;

    SessionStateMachine session;

    const auto invalid = session.transition(SessionState::Recording);
    test.expect(
        invalid.code == StatusCode::InvalidStateTransition,
        "Idle cannot jump directly to Recording");
    test.expect(
        session.state() == SessionState::Idle,
        "Invalid transition does not mutate state");

    test.expect(
        session.transition(SessionState::Preparing).ok(),
        "Idle -> Preparing");
    test.expect(session.active(), "Preparing is active");
    test.expect(
        session.transition(SessionState::Countdown).ok(),
        "Preparing -> Countdown");
    test.expect(
        session.transition(SessionState::Recording).ok(),
        "Countdown -> Recording");
    test.expect(
        session.transition(SessionState::Paused).ok(),
        "Recording -> Paused");
    test.expect(
        session.transition(SessionState::Recording).ok(),
        "Paused -> Recording");
    test.expect(
        session.transition(SessionState::Stopping).ok(),
        "Recording -> Stopping");
    test.expect(
        session.transition(SessionState::Finalizing).ok(),
        "Stopping -> Finalizing");
    test.expect(
        session.transition(SessionState::Ready).ok(),
        "Finalizing -> Ready");
    test.expect(!session.active(), "Ready is not an active recording state");
    test.expect(
        session.transition(SessionState::Idle).ok(),
        "Ready -> Idle");
}

} // namespace

int main()
{
    TestContext test;

    test_result(test);
    test_spsc_ring(test);
    test_latest_atomic(test);
    test_diagnostics(test);
    test_clock(test);
    test_session_state_machine(test);

    if (test.failures != 0) {
        std::cerr << test.failures << " of " << test.checks
                  << " checks failed\n";
        return 1;
    }

    std::cout << "PASS: " << test.checks << " deterministic checks\n";
    return 0;
}
